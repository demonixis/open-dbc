#pragma once

#include "engine_builtin_context.hpp"
#include "vm/execution_session.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace dbc::interpreter::detail
{
    class EngineBuiltinHost final : public vm::BuiltinHost
    {
      public:
        explicit EngineBuiltinHost(BuiltinContext& Context);

        vm::BuiltinResult Invoke(const vm::BuiltinInvocation& Invocation) override;
        bool Cancel(std::uint64_t InvocationId, std::string& Error) override;

      private:
        enum class Phase : std::uint8_t
        {
            SyncDeadline,
            Deadline,
            Key,
            MouseRelease,
            MousePress,
            TextInput,
        };

        struct Pending
        {
            Phase CurrentPhase = Phase::Deadline;
            std::chrono::steady_clock::time_point Deadline{};
            std::string Text{};
        };

        vm::BuiltinResult InvokePrint(const vm::BuiltinInvocation& Invocation);
        vm::BuiltinResult InvokeValue(const vm::BuiltinInvocation& Invocation);
        vm::BuiltinResult InvokeSync(const vm::BuiltinInvocation& Invocation);
        vm::BuiltinResult InvokeWait(const vm::BuiltinInvocation& Invocation);
        vm::BuiltinResult InvokeWaitKey(const vm::BuiltinInvocation& Invocation);
        vm::BuiltinResult InvokeWaitMouse(const vm::BuiltinInvocation& Invocation);
        vm::BuiltinResult InvokeInput(const vm::BuiltinInvocation& Invocation);
        vm::BuiltinResult InvokeFileRead(const vm::BuiltinInvocation& Invocation);
        vm::BuiltinResult InvokeArray(const vm::BuiltinInvocation& Invocation);
        vm::BuiltinResult InvokeCompatibilityStub(const vm::BuiltinInvocation& Invocation);
        std::optional<vm::BuiltinResult> InvokeInertResourceContinuation(const vm::BuiltinInvocation& Invocation);

        BuiltinContext& Context_;
        std::unordered_map<std::uint64_t, Pending> Pending_{};
        std::unordered_set<language::SymbolId> LoggedCompatibilityStubs_{};
    };
} // namespace dbc::interpreter::detail
