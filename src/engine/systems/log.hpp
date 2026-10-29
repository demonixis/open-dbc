#pragma once

#include "path_utf8.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>

namespace dbc
{
    class DebugSystem;
}

namespace dbc::log
{
    inline std::string& Directory()
    {
        static std::string Value = ".";
        return Value;
    }

    namespace detail
    {
        // Custom spdlog sink that forwards messages to the DebugSystem ring buffer.
        class RingBufferSink final : public spdlog::sinks::base_sink<std::mutex>
        {
          public:
            explicit RingBufferSink(DebugSystem* Debug) : Debug_(Debug) {}
            DebugSystem* Target() const
            {
                return Debug_;
            }

          protected:
            void sink_it_(const spdlog::details::log_msg& Msg) override;
            void flush_() override {}

          private:
            DebugSystem* Debug_ = nullptr;
        };

        inline spdlog::level::level_enum ParseLevel(const std::string& Level)
        {
            if (Level == "trace")
                return spdlog::level::trace;
            if (Level == "debug")
                return spdlog::level::debug;
            if (Level == "info")
                return spdlog::level::info;
            if (Level == "warn")
                return spdlog::level::warn;
            if (Level == "error")
                return spdlog::level::err;
            if (Level == "critical")
                return spdlog::level::critical;
            if (Level == "off")
                return spdlog::level::off;
#ifdef NDEBUG
            return spdlog::level::warn;
#else
            return spdlog::level::info;
#endif
        }
    } // namespace detail

    inline void Init(const std::string& Level = "", const std::string& LogDir = ".")
    {
        Directory() = LogDir;
        if (spdlog::get("dbc"))
        {
            return;
        }

        auto ConsoleSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();

        // Try to create a rotating file sink.  If the working directory is
        // not writable (e.g. when launched from a macOS .app bundle), fall
        // back to console-only logging instead of crashing.
        std::shared_ptr<spdlog::logger> Logger;
        try
        {
            auto FileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                (dbc::PathFromUtf8(LogDir) / "open-dbc.log").native(), 5 * 1024 * 1024, 3);
            Logger = std::make_shared<spdlog::logger>("dbc", spdlog::sinks_init_list{ConsoleSink, FileSink});
        }
        catch (const spdlog::spdlog_ex&)
        {
            // File sink failed — console only.
            Logger = std::make_shared<spdlog::logger>("dbc", ConsoleSink);
        }

        Logger->set_level(detail::ParseLevel(Level));
        Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::register_logger(Logger);
    }

    inline std::shared_ptr<spdlog::logger> Get()
    {
        auto L = spdlog::get("dbc");
        if (!L)
        {
            Init();
            L = spdlog::get("dbc");
        }
        return L;
    }

    inline void AttachRingBuffer(DebugSystem* Debug)
    {
        auto Logger = Get();
        if (!Logger || !Debug)
        {
            return;
        }
        auto Sink = std::make_shared<detail::RingBufferSink>(Debug);
        Sink->set_level(spdlog::level::trace);
        Logger->sinks().push_back(Sink);
    }

    inline void DetachRingBuffer(DebugSystem* Debug)
    {
        auto Logger = spdlog::get("dbc");
        if (!Logger || !Debug)
            return;
        std::erase_if(Logger->sinks(),
                      [Debug](const std::shared_ptr<spdlog::sinks::sink>& Sink)
                      {
                          const auto Ring = std::dynamic_pointer_cast<detail::RingBufferSink>(Sink);
                          return Ring && Ring->Target() == Debug;
                      });
    }

    inline void SetLevel(const std::string& Level)
    {
        auto Logger = Get();
        if (Logger)
            Logger->set_level(detail::ParseLevel(Level));
    }

    inline void Shutdown()
    {
        spdlog::drop("dbc");
    }

} // namespace dbc::log

// Convenience macros
#define DBC_LOG_TRACE(...) dbc::log::Get()->trace(__VA_ARGS__)
#define DBC_LOG_DEBUG(...) dbc::log::Get()->debug(__VA_ARGS__)
#define DBC_LOG_INFO(...) dbc::log::Get()->info(__VA_ARGS__)
#define DBC_LOG_WARN(...) dbc::log::Get()->warn(__VA_ARGS__)
#define DBC_LOG_ERROR(...) dbc::log::Get()->error(__VA_ARGS__)
#define DBC_LOG_CRITICAL(...) dbc::log::Get()->critical(__VA_ARGS__)
