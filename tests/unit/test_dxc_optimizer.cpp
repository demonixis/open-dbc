#include <doctest/doctest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif
#include <dxc/dxcapi.h>

TEST_CASE("Statically linked DXC exposes its optimizer passes")
{
    IDxcOptimizer* RawOptimizer = nullptr;
    const auto Result = DxcCreateInstance(CLSID_DxcOptimizer, IID_PPV_ARGS(&RawOptimizer));
    const auto Release = [](IDxcOptimizer* Optimizer) { Optimizer->Release(); };
    const std::unique_ptr<IDxcOptimizer, decltype(Release)> Optimizer(RawOptimizer, Release);
    REQUIRE(SUCCEEDED(Result));
    REQUIRE(Optimizer != nullptr);

    UINT32 Count = 0;
    REQUIRE(SUCCEEDED(Optimizer->GetAvailablePassCount(&Count)));
    REQUIRE(Count > 0);
    for (UINT32 Index = 0; Index < Count; ++Index)
    {
        IDxcOptimizerPass* RawPass = nullptr;
        const auto PassResult = Optimizer->GetAvailablePass(Index, &RawPass);
        const auto ReleasePass = [](IDxcOptimizerPass* Pass) { Pass->Release(); };
        const std::unique_ptr<IDxcOptimizerPass, decltype(ReleasePass)> Pass(RawPass, ReleasePass);
        REQUIRE(SUCCEEDED(PassResult));
        REQUIRE(Pass != nullptr);

        UINT32 ArgumentCount = 0;
        CHECK(SUCCEEDED(Pass->GetOptionArgCount(&ArgumentCount)));
    }
}
