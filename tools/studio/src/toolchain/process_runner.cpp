#include "process_runner.hpp"
#include "core/i18n.hpp"
#include "path_utf8.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>

namespace dbc::studio
{
    namespace
    {
        // Enough context to diagnose a failed Gradle or notarytool run without flooding the error text.
        constexpr std::size_t ErrorTailLines = 20;
        // A tool asked to stop gracefully gets this long to exit before it is terminated.
        constexpr std::uint64_t GracefulStopMilliseconds = 5000;
        constexpr std::uint64_t OrphanedPipeMilliseconds = 2000;

        class LineSplitter
        {
          public:
            LineSplitter(const toolchain::ProcessLineSink& Sink, std::string* Capture) : Sink_(Sink), Capture_(Capture)
            {
            }

            void Feed(std::string_view Bytes)
            {
                if (Capture_ != nullptr)
                    Capture_->append(Bytes);
                for (const char Character : Bytes)
                {
                    if (Character == '\n')
                        Emit();
                    else if (Character != '\r')
                        Pending_ += Character;
                }
            }

            void Finish()
            {
                if (!Pending_.empty())
                    Emit();
            }

            std::string Tail() const
            {
                std::string Text;
                for (const std::string& Line : Tail_)
                    Text += "\n  " + Line;
                return Text;
            }

          private:
            void Emit()
            {
                if (Sink_)
                    Sink_(Pending_);
                Tail_.push_back(std::move(Pending_));
                if (Tail_.size() > ErrorTailLines)
                    Tail_.pop_front();
                Pending_.clear();
            }

            const toolchain::ProcessLineSink& Sink_;
            std::string* Capture_ = nullptr;
            std::string Pending_;
            std::deque<std::string> Tail_;
        };

        struct EnvironmentDeleter
        {
            void operator()(SDL_Environment* Environment) const
            {
                SDL_DestroyEnvironment(Environment);
            }
        };

        struct ProcessDeleter
        {
            void operator()(SDL_Process* Process) const
            {
                SDL_DestroyProcess(Process);
            }
        };
    } // namespace

    namespace toolchain
    {
        ProcessResult RunProcess(const ProcessRequest& Request, const ProcessLineSink& Sink,
                                 const std::atomic<bool>* Cancel, bool CaptureOutput)
        {
            ProcessResult Result;
            if (Request.Arguments.empty())
            {
                Result.Error = Tr("No tool to run");
                return Result;
            }
            const std::string& Tool = Request.Arguments.front();
            std::vector<const char*> Values;
            Values.reserve(Request.Arguments.size() + 1);
            for (const std::string& Argument : Request.Arguments)
                Values.push_back(Argument.c_str());
            Values.push_back(nullptr);

            std::unique_ptr<SDL_Environment, EnvironmentDeleter> Environment;
            if (!Request.Environment.empty())
            {
                Environment.reset(SDL_CreateEnvironment(true));
                if (!Environment)
                {
                    Result.Error = TrFmt("Cannot prepare the environment of '{0}': {1}", {Tool, SDL_GetError()});
                    return Result;
                }
                for (const auto& [Name, Value] : Request.Environment)
                {
                    if (!SDL_SetEnvironmentVariable(Environment.get(), Name.c_str(), Value.c_str(), true))
                    {
                        Result.Error = TrFmt("Cannot set {0} for '{1}': {2}", {Name, Tool, SDL_GetError()});
                        return Result;
                    }
                }
            }

            const SDL_PropertiesID Properties = SDL_CreateProperties();
            if (Properties == 0)
            {
                Result.Error = TrFmt("Cannot prepare process '{0}': {1}", {Tool, SDL_GetError()});
                return Result;
            }
            SDL_SetPointerProperty(Properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, Values.data());
            if (Environment)
                SDL_SetPointerProperty(Properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, Environment.get());
            const std::string WorkingDirectory = dbc::PathToUtf8(Request.WorkingDirectory);
            if (!WorkingDirectory.empty())
                SDL_SetStringProperty(Properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,
                                      WorkingDirectory.c_str());
            SDL_SetNumberProperty(Properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
            SDL_SetNumberProperty(Properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetBooleanProperty(Properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
            const std::unique_ptr<SDL_Process, ProcessDeleter> Process(SDL_CreateProcessWithProperties(Properties));
            SDL_DestroyProperties(Properties);
            if (!Process)
            {
                Result.Error = TrFmt("Cannot start '{0}': {1}", {Tool, SDL_GetError()});
                return Result;
            }

            LineSplitter Lines(Sink, CaptureOutput ? &Result.Output : nullptr);
            SDL_IOStream* Output = SDL_GetProcessOutput(Process.get());
            std::array<char, 4096> Buffer{};
            std::uint64_t StopRequestedAt = 0;
            std::uint64_t ExitedAt = 0;
            bool Forced = false;
            // The output stream is non-blocking: drain it until the tool closes it, or shortly after the
            // tool exits when a process it left behind still holds the pipe open.
            while (Output != nullptr)
            {
                const std::size_t Count = SDL_ReadIO(Output, Buffer.data(), Buffer.size());
                if (Count > 0)
                {
                    Lines.Feed(std::string_view(Buffer.data(), Count));
                    continue;
                }
                if (SDL_GetIOStatus(Output) != SDL_IO_STATUS_NOT_READY)
                    break;
                if (ExitedAt == 0 && SDL_WaitProcess(Process.get(), false, &Result.ExitCode))
                    ExitedAt = SDL_GetTicks();
                if (ExitedAt != 0 && SDL_GetTicks() - ExitedAt > OrphanedPipeMilliseconds)
                    break;
                if (Cancel != nullptr && Cancel->load(std::memory_order_relaxed))
                {
                    if (StopRequestedAt == 0)
                    {
                        Result.Cancelled = true;
                        StopRequestedAt = SDL_GetTicks();
                        SDL_KillProcess(Process.get(), false);
                    }
                    else if (!Forced && SDL_GetTicks() - StopRequestedAt > GracefulStopMilliseconds)
                    {
                        Forced = true;
                        SDL_KillProcess(Process.get(), true);
                    }
                }
                SDL_Delay(10);
            }
            Lines.Finish();

            if (!SDL_WaitProcess(Process.get(), true, &Result.ExitCode))
            {
                Result.Error = TrFmt("Cannot wait for '{0}': {1}", {Tool, SDL_GetError()});
                return Result;
            }
            if (Result.Cancelled)
                Result.Error = TrFmt("Cancelled while running '{0}'", {Tool});
            else if (Result.ExitCode != 0)
                Result.Error =
                    TrFmt("Command failed ({0}): {1}{2}", {std::to_string(Result.ExitCode), Tool, Lines.Tail()});
            return Result;
        }
    } // namespace toolchain

} // namespace dbc::studio
