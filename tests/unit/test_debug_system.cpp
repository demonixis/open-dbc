#include "engine/systems/debug_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/log.hpp"
#include "engine/systems/text_system.hpp"
#include <doctest/doctest.h>

TEST_CASE("DebugSystem - overlay toggle")
{
    dbc::EngineKernel Kernel;
    // Don't call Initialize() — we only test DebugSystem in isolation.
    // DebugSystem can be constructed with an uninitialized kernel for these tests.
    dbc::DebugSystem Debug(Kernel);

    CHECK_FALSE(Debug.IsOverlayEnabled());

    Debug.SetOverlayEnabled(true);
    CHECK(Debug.IsOverlayEnabled());

    Debug.SetOverlayEnabled(false);
    CHECK_FALSE(Debug.IsOverlayEnabled());

    Debug.ToggleOverlay();
    CHECK(Debug.IsOverlayEnabled());

    Debug.ToggleOverlay();
    CHECK_FALSE(Debug.IsOverlayEnabled());
}

TEST_CASE("DebugSystem - frame stats defaults")
{
    dbc::EngineKernel Kernel;
    dbc::DebugSystem Debug(Kernel);

    const auto& Stats = Debug.CurrentStats();
    CHECK(Stats.Fps == 0);
    CHECK(Stats.DrawCalls == 0);
    CHECK(Stats.TotalVertices == 0);
    CHECK(Stats.TotalBatches == 0);
    CHECK(Stats.VisibleObjects == 0);
    CHECK(Stats.TotalObjects == 0);
}

TEST_CASE("DebugSystem - frame stats update")
{
    dbc::EngineKernel Kernel;
    dbc::DebugSystem Debug(Kernel);

    dbc::FrameStats Stats;
    Stats.Fps = 60;
    Stats.DrawCalls = 12;
    Stats.TotalVertices = 8432;
    Stats.TotalBatches = 6;
    Stats.VisibleObjects = 42;
    Stats.TotalObjects = 128;

    Debug.UpdateFrameStats(Stats);

    const auto& Result = Debug.CurrentStats();
    CHECK(Result.Fps == 60);
    CHECK(Result.DrawCalls == 12);
    CHECK(Result.TotalVertices == 8432);
    CHECK(Result.TotalBatches == 6);
    CHECK(Result.VisibleObjects == 42);
    CHECK(Result.TotalObjects == 128);
}

TEST_CASE("DebugSystem - render error state")
{
    dbc::EngineKernel Kernel;
    dbc::DebugSystem Debug(Kernel);

    CHECK_FALSE(Debug.HasRenderError());
    CHECK(Debug.LastRenderError().empty());

    Debug.SetLastRenderError("GPU device null");
    CHECK(Debug.HasRenderError());
    CHECK(Debug.LastRenderError() == "GPU device null");

    Debug.ClearRenderError();
    CHECK_FALSE(Debug.HasRenderError());
    CHECK(Debug.LastRenderError().empty());
}

TEST_CASE("DebugSystem - ring buffer basic")
{
    dbc::EngineKernel Kernel;
    dbc::DebugSystem Debug(Kernel);

    CHECK(Debug.RecentLogMessages().empty());

    Debug.PushLogMessage("msg1");
    Debug.PushLogMessage("msg2");
    Debug.PushLogMessage("msg3");

    auto Msgs = Debug.RecentLogMessages();
    REQUIRE(Msgs.size() == 3);
    CHECK(Msgs[0] == "msg1");
    CHECK(Msgs[1] == "msg2");
    CHECK(Msgs[2] == "msg3");
}

TEST_CASE("DebugSystem - ring buffer overflow (FIFO)")
{
    dbc::EngineKernel Kernel;
    dbc::DebugSystem Debug(Kernel);

    // Push 150 messages, buffer holds 100.
    for (int I = 0; I < 150; ++I)
    {
        Debug.PushLogMessage("msg_" + std::to_string(I));
    }

    auto Msgs = Debug.RecentLogMessages();
    REQUIRE(Msgs.size() == 100);

    // Oldest should be msg_50 (first 50 dropped).
    CHECK(Msgs[0] == "msg_50");
    CHECK(Msgs[99] == "msg_149");
}

TEST_CASE("DebugSystem - ring buffer order after wrap")
{
    dbc::EngineKernel Kernel;
    dbc::DebugSystem Debug(Kernel);

    // Fill exactly to capacity.
    for (int I = 0; I < 100; ++I)
    {
        Debug.PushLogMessage("a_" + std::to_string(I));
    }

    auto Msgs = Debug.RecentLogMessages();
    REQUIRE(Msgs.size() == 100);
    CHECK(Msgs[0] == "a_0");
    CHECK(Msgs[99] == "a_99");

    // Push one more — wraps around.
    Debug.PushLogMessage("b_100");
    Msgs = Debug.RecentLogMessages();
    REQUIRE(Msgs.size() == 100);
    CHECK(Msgs[0] == "a_1"); // a_0 was dropped
    CHECK(Msgs[99] == "b_100");
}

TEST_CASE("DebugSystem - overlay lines are transient")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 640, 480));

    Kernel.Debug().SetOverlayEnabled(true);
    Kernel.Debug().QueueOverlayText();
    CHECK(Kernel.Text().OverlayTextLineCount() == 4);

    Kernel.Text().ClearOverlayTextLines();
    Kernel.Debug().QueueOverlayText();
    CHECK(Kernel.Text().OverlayTextLineCount() == 4);
}

TEST_CASE("DebugSystem - sequential kernels detach their logging sink")
{
    {
        dbc::EngineKernel First;
        REQUIRE(First.Initialize("first", 16, 16));
        DBC_LOG_ERROR("first kernel message");
        REQUIRE_FALSE(First.Debug().RecentLogMessages().empty());
    }
    {
        dbc::EngineKernel Second;
        REQUIRE(Second.Initialize("second", 16, 16));
        DBC_LOG_ERROR("second kernel message");
        REQUIRE_FALSE(Second.Debug().RecentLogMessages().empty());
        CHECK(Second.Debug().RecentLogMessages().back().find("second kernel message") != std::string::npos);
    }
}
