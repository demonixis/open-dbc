#include "triangle_order.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <unordered_map>

namespace dbc
{
    namespace
    {
        // Phase, render priority and depth packed so that comparing two keys compares them in that order.
        struct OrderKey
        {
            uint64_t Key = 0;
            uint32_t Index = 0;
            // The triangle's StateHash, never zero, or zero when its result depends on its place in the order.
            uint32_t State = 0;
        };

        struct GroupScratch
        {
            std::vector<uint32_t> GroupOf;
            std::unordered_map<uint32_t, uint32_t> Groups;
            std::vector<uint32_t> Offsets;
            std::vector<OrderKey> Grouped;
        };

        constexpr uint32_t Mix(uint32_t Hash, uint64_t Value)
        {
            Hash ^= static_cast<uint32_t>(Value ^ (Value >> 32)) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
            return Hash;
        }

        int DrawPhase(const Triangle& TriangleValue)
        {
            if (TriangleValue.VeryEarly)
                return 0;
            return (TriangleValue.Transparent || TriangleValue.Additive) ? 2 : 1;
        }

        // Maps a depth onto an unsigned integer with the same order. A NaN would break the sort's ordering, so it
        // sorts last; negative zero sorts as zero, as the float comparison it replaces did.
        uint32_t SortableDepth(float Depth)
        {
            if (std::isnan(Depth))
                Depth = std::numeric_limits<float>::infinity();
            if (Depth == 0.0f)
                Depth = 0.0f;
            const auto Bits = std::bit_cast<uint32_t>(Depth);
            return (Bits & 0x80000000u) ? ~Bits : (Bits | 0x80000000u);
        }

        uint64_t PackKey(int Phase, int Priority, float Depth)
        {
            constexpr int64_t PriorityBias = int64_t{1} << 29;
            const auto Biased =
                static_cast<uint64_t>(std::clamp<int64_t>(Priority + PriorityBias, 0, 2 * PriorityBias - 1));
            return (static_cast<uint64_t>(Phase) << 62) | (Biased << 32) | SortableDepth(Depth);
        }

        // The index breaks ties, so the order matches a stable sort of the triangles.
        void SortKeys(std::vector<OrderKey>& Keys)
        {
            std::sort(Keys.begin(), Keys.end(), [](const OrderKey& A, const OrderKey& B)
                      { return A.Key != B.Key ? A.Key < B.Key : A.Index < B.Index; });
        }

        // Groups a run of order-independent triangles by state, states in order of first appearance; a counting
        // sort keeps the depth order within each state.
        void GroupRun(std::span<OrderKey> Run, GroupScratch& Scratch)
        {
            Scratch.GroupOf.resize(Run.size());
            Scratch.Groups.clear();
            for (std::size_t Position = 0; Position < Run.size(); ++Position)
            {
                if (Position > 0 && Run[Position].State == Run[Position - 1].State)
                {
                    Scratch.GroupOf[Position] = Scratch.GroupOf[Position - 1];
                    continue;
                }
                const auto Group = static_cast<uint32_t>(Scratch.Groups.size());
                Scratch.GroupOf[Position] = Scratch.Groups.try_emplace(Run[Position].State, Group).first->second;
            }
            if (Scratch.Groups.size() < 2)
                return;

            Scratch.Offsets.assign(Scratch.Groups.size() + 1, 0);
            for (std::size_t Position = 0; Position < Run.size(); ++Position)
                ++Scratch.Offsets[Scratch.GroupOf[Position] + 1];
            for (std::size_t Group = 1; Group < Scratch.Offsets.size(); ++Group)
                Scratch.Offsets[Group] += Scratch.Offsets[Group - 1];
            Scratch.Grouped.resize(Run.size());
            for (std::size_t Position = 0; Position < Run.size(); ++Position)
                Scratch.Grouped[Scratch.Offsets[Scratch.GroupOf[Position]]++] = Run[Position];
            std::copy(Scratch.Grouped.begin(), Scratch.Grouped.end(), Run.begin());
        }

        void WriteOrder(const std::vector<OrderKey>& Keys, std::vector<uint32_t>& Order)
        {
            Order.resize(Keys.size());
            for (std::size_t Position = 0; Position < Keys.size(); ++Position)
                Order[Position] = Keys[Position].Index;
        }
    } // namespace

    uint32_t SurfaceStyleHash(const SurfaceStyle& Style)
    {
        uint32_t Hash =
            Mix(0, (Style.Wireframe ? 1u : 0u) | (Style.Fog ? 2u : 0u) | (Style.BlackFog ? 4u : 0u) |
                       (Style.Ambient ? 8u : 0u) | (Style.Filter ? 16u : 0u) | (Style.ReflectionSurface ? 32u : 0u) |
                       (Style.ReflectionPass ? 64u : 0u) | (Style.ReflectionOverlay ? 128u : 0u) |
                       (Style.Backdrop ? 256u : 0u) | (Style.ShadowOverlay ? 512u : 0u));
        for (std::size_t Stage = 0; Stage < Style.TextureBlendModes.size(); ++Stage)
        {
            Hash = Mix(Hash, static_cast<uint32_t>(Style.TextureBlendModes[Stage]) |
                                 static_cast<uint64_t>(static_cast<uint32_t>(Style.TextureAddressModes[Stage])) << 32);
            Hash = Mix(Hash, static_cast<uint32_t>(Style.TextureMipModes[Stage]) |
                                 (Style.TextureCubeModes[Stage] ? uint64_t{1} << 32 : 0u));
        }
        Hash = Mix(Hash, static_cast<uint32_t>(Style.BuiltInEffect) |
                             static_cast<uint64_t>(static_cast<uint32_t>(Style.EffectId)) << 32);
        if (Style.BuiltInEffect == 4)
            Hash = Mix(Hash, std::bit_cast<uint32_t>(Style.CubeLevels));
        if (Style.Stage1UvScale[0] != 0.0f)
            Hash = Mix(Hash, std::bit_cast<uint32_t>(Style.Stage1UvScale[0]) |
                                 static_cast<uint64_t>(std::bit_cast<uint32_t>(Style.Stage1UvScale[1])) << 32);
        return Hash;
    }

    uint32_t DrawStateHash(const Triangle& TriangleValue, uint32_t StyleHash)
    {
        uint32_t Hash = Mix(StyleHash, reinterpret_cast<uintptr_t>(TriangleValue.GpuTexture));
        for (const SDL_GPUTexture* Texture : TriangleValue.AuxiliaryGpuTextures)
            Hash = Mix(Hash, reinterpret_cast<uintptr_t>(Texture));
        Hash = Mix(Hash, reinterpret_cast<uintptr_t>(TriangleValue.CubeGpuTexture));
        return Mix(Hash, (TriangleValue.Transparent ? 1u : 0u) | (TriangleValue.AlphaDepthWrite ? 2u : 0u) |
                             (TriangleValue.Additive ? 4u : 0u) | (TriangleValue.Is2d ? 8u : 0u) |
                             (TriangleValue.DepthTest ? 16u : 0u) | (TriangleValue.DepthWrite ? 32u : 0u) |
                             static_cast<uint64_t>(static_cast<uint32_t>(TriangleValue.MatrixGhostMode)) << 32);
    }

    bool OrderIndependent(const Triangle& TriangleValue)
    {
        // Alpha-write and ghost-matrix pipelines blend even outside the transparent phase.
        return !TriangleValue.Transparent && !TriangleValue.Additive && !TriangleValue.AlphaDepthWrite &&
               TriangleValue.MatrixGhostMode < 0 && TriangleValue.DepthTest && TriangleValue.DepthWrite &&
               !TriangleValue.Is2d;
    }

    void OrderWorldTriangles(OrderedTriangles& List)
    {
        const std::vector<Triangle>& Triangles = List.Triangles;
        std::vector<OrderKey> Keys(Triangles.size());
        for (std::size_t Index = 0; Index < Triangles.size(); ++Index)
        {
            const Triangle& TriangleValue = Triangles[Index];
            const int Phase = DrawPhase(TriangleValue);
            Keys[Index] = {
                PackKey(Phase, TriangleValue.RenderPriority, Phase == 2 ? -TriangleValue.Depth : TriangleValue.Depth),
                static_cast<uint32_t>(Index),
                OrderIndependent(TriangleValue) ? std::max(TriangleValue.StateHash, 1u) : 0u};
        }
        SortKeys(Keys);

        // A run shares its phase and priority, the upper half of the key.
        GroupScratch Scratch;
        for (std::size_t Begin = 0; Begin < Keys.size();)
        {
            if (Keys[Begin].State == 0)
            {
                ++Begin;
                continue;
            }
            const uint64_t Bucket = Keys[Begin].Key >> 32;
            std::size_t End = Begin + 1;
            while (End < Keys.size() && Keys[End].State != 0 && Keys[End].Key >> 32 == Bucket)
                ++End;
            GroupRun(std::span<OrderKey>(Keys).subspan(Begin, End - Begin), Scratch);
            Begin = End;
        }
        WriteOrder(Keys, List.Order);
    }

    void OrderOverlayTriangles(OrderedTriangles& List)
    {
        std::vector<OrderKey> Keys(List.Triangles.size());
        for (std::size_t Index = 0; Index < List.Triangles.size(); ++Index)
            Keys[Index] = {PackKey(0, 0, -List.Triangles[Index].Depth), static_cast<uint32_t>(Index), 0};
        SortKeys(Keys);
        WriteOrder(Keys, List.Order);
    }
} // namespace dbc
