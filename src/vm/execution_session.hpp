#pragma once

#include "bytecode.hpp"
#include "value.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>

namespace dbc::vm
{
    // Runaway recursion and GOSUB without RETURN stop with an error before they exhaust memory.
    inline constexpr std::size_t KMaxCallDepth = 100'000;
    inline constexpr std::size_t KMaxGosubDepth = 100'000;
    // One array holds at most 2^25 elements (2 GiB of values): a wrong DIM or ARRAY INSERT size fails with an
    // error instead of aborting the process on a failed allocation.
    inline constexpr std::size_t KMaxArrayElements = std::size_t{1} << 25;

    enum class BuiltinState : std::uint8_t
    {
        Completed,
        Pending,
        CompletedAndSuspend,
        Failed,
    };

    enum class SuspensionKind : std::uint8_t
    {
        None,
        NextTick,
        FrameBoundary,
        Deadline,
        Key,
        Mouse,
        TextInput,
        Io,
    };

    struct Suspension
    {
        SuspensionKind Kind = SuspensionKind::None;
        std::optional<std::chrono::steady_clock::time_point> Deadline{};
    };

    struct BuiltinResult
    {
        BuiltinState State = BuiltinState::Completed;
        std::optional<Value> ReturnValue{};
        std::string Error{};
        Suspension Wait{};
    };

    struct BuiltinInvocation
    {
        std::uint64_t Id = 0;
        language::SymbolId Symbol = language::SymbolId::Count;
        std::span<const Value> Arguments{};
        language::Type ResultType{};
        bool IsRetry = false;
        bool ContinuesPrint = false;
        const Value* ArrayArgument = nullptr;
        bool ReplacesArray = false;
    };

    class BuiltinHost
    {
      public:
        virtual ~BuiltinHost() = default;
        virtual BuiltinResult Invoke(const BuiltinInvocation& Invocation) = 0;
        virtual bool Cancel(std::uint64_t InvocationId, std::string& Error) = 0;
    };

    enum class SessionState : std::uint8_t
    {
        Empty,
        Ready,
        Running,
        Suspended,
        Completed,
        Failed,
        Cancelled,
    };

    struct TickResult
    {
        SessionState State = SessionState::Empty;
        std::size_t Instructions = 0;
        std::string Error{};
        Suspension Wait{};
    };

    // Why a session failed, with the span of the instruction that failed (empty for preparation errors).
    struct SessionFailure
    {
        std::string Code{};
        std::string Message{};
        language::SourceSpan Span{};
    };

    struct BuiltinCoverageEntry
    {
        language::SymbolId Symbol = language::SymbolId::Count;
        language::OverloadId Overload{};
        std::uint64_t Attempts = 0;
        std::uint64_t Completed = 0;
        std::uint64_t Failed = 0;
    };

    // Sees each builtin call once, before the host runs it, with its evaluated arguments.
    using BuiltinObserver =
        std::function<void(language::SymbolId, std::span<const Value> Arguments, const language::SourceSpan&)>;

    class ExecutionSession
    {
      public:
        void SetBuiltinObserver(BuiltinObserver Observer);
        bool Prepare(std::shared_ptr<const Module> Bytecode, BuiltinHost& Host, bool TrackBuiltinCoverage = false);
        TickResult Tick(std::size_t InstructionBudget);
        void RequestStop();
        SessionState State() const;
        const std::string& Error() const;
        const SessionFailure& Failure() const;
        const Value* Global(std::string_view Name) const;
        std::span<const BuiltinCoverageEntry> Coverage() const;

      private:
        struct Frame
        {
            std::size_t FunctionIndex = 0;
            std::size_t ProgramCounter = 0;
            std::vector<Value> Locals{};
            std::vector<Value> Stack{};
            std::vector<std::size_t> Subroutines{};
        };

        bool Execute(Frame& Current, const Instruction& Value);
        void BeginBuiltinCoverage(language::SymbolId Symbol, language::OverloadId Overload);
        void CompleteBuiltinCoverage(language::SymbolId Symbol, language::OverloadId Overload);
        void FailBuiltinCoverage();
        bool CancelActiveBuiltin(std::string& Error);
        void Fail(std::string Code, std::string Message, language::SourceSpan Span = {});

        std::shared_ptr<const Module> Bytecode_{};
        BuiltinHost* Host_ = nullptr;
        std::vector<Value> Globals_{};
        std::vector<Frame> Frames_{};
        std::size_t DataCursor_ = 0;
        std::atomic_bool StopRequested_ = false;
        std::uint64_t NextInvocationId_ = 1;
        std::optional<std::uint64_t> ActiveInvocation_{};
        BuiltinObserver Observer_{};
        std::vector<std::pair<language::SymbolId, language::OverloadId>> ActiveCoverage_{};
        std::vector<BuiltinCoverageEntry> Coverage_{};
        std::unordered_map<std::uint64_t, std::size_t> CoverageIndices_{};
        bool TrackBuiltinCoverage_ = false;
        Suspension Suspension_{};
        SessionState State_ = SessionState::Empty;
        std::string Error_{};
        SessionFailure Failure_{};
    };
} // namespace dbc::vm
