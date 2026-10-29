#include "triangle_order.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <span>
#include <unordered_map>

namespace dbc
{
    namespace
    {
        struct OrderKey
        {
            int Phase = 0;
            int Priority = 0;
            float Depth = 0.0f;
            uint32_t Index = 0;
        };

        struct GroupScratch
        {
            std::vector<uint32_t> StateOf;
            std::vector<uint32_t> Representatives;
            std::unordered_multimap<std::size_t, uint32_t> StatesByHash;
            std::vector<uint32_t> Offsets;
            std::vector<OrderKey> Grouped;
        };

        int DrawPhase(const Triangle& TriangleValue)
        {
            if (TriangleValue.VeryEarly)
                return 0;
            return (TriangleValue.Transparent || TriangleValue.Additive) ? 2 : 1;
        }

        // A NaN depth would break the sort's ordering, so it sorts last.
        float SortDepth(float Depth)
        {
            return std::isnan(Depth) ? std::numeric_limits<float>::infinity() : Depth;
        }

        // The index breaks ties, so the order matches a stable sort of the triangles.
        void SortKeys(std::vector<OrderKey>& Keys)
        {
            std::sort(Keys.begin(), Keys.end(),
                      [](const OrderKey& A, const OrderKey& B)
                      {
                          if (A.Phase != B.Phase)
                              return A.Phase < B.Phase;
                          if (A.Priority != B.Priority)
                              return A.Priority < B.Priority;
                          if (A.Depth != B.Depth)
                              return A.Depth < B.Depth;
                          return A.Index < B.Index;
                      });
        }

        // Covers the textures and the most varied state; SameDrawState settles collisions.
        std::size_t DrawStateHash(const Triangle& TriangleValue)
        {
            std::size_t Hash = std::hash<const void*>{}(TriangleValue.GpuTexture);
            const auto Mix = [&Hash](std::size_t Value)
            { Hash ^= Value + static_cast<std::size_t>(0x9e3779b9u) + (Hash << 6) + (Hash >> 2); };
            for (const SDL_GPUTexture* Texture : TriangleValue.AuxiliaryGpuTextures)
                Mix(std::hash<const void*>{}(Texture));
            Mix(std::hash<const void*>{}(TriangleValue.CubeGpuTexture));
            Mix(std::hash<int>{}(TriangleValue.Style.EffectId));
            Mix(TriangleValue.Style.Wireframe ? 1u : 0u);
            return Hash;
        }

        // Groups a run of order-independent triangles by draw state, states in order of first appearance; a
        // counting sort keeps the depth order within each state.
        void GroupRun(const std::vector<Triangle>& Triangles, std::span<OrderKey> Run, GroupScratch& Scratch)
        {
            Scratch.StateOf.resize(Run.size());
            Scratch.Representatives.clear();
            Scratch.StatesByHash.clear();
            for (std::size_t Position = 0; Position < Run.size(); ++Position)
            {
                const Triangle& Current = Triangles[Run[Position].Index];
                if (Position > 0 && SameDrawState(Current, Triangles[Run[Position - 1].Index]))
                {
                    Scratch.StateOf[Position] = Scratch.StateOf[Position - 1];
                    continue;
                }
                const std::size_t Hash = DrawStateHash(Current);
                const auto [First, Last] = Scratch.StatesByHash.equal_range(Hash);
                const auto Match =
                    std::find_if(First, Last, [&](const auto& Entry)
                                 { return SameDrawState(Current, Triangles[Scratch.Representatives[Entry.second]]); });
                if (Match != Last)
                {
                    Scratch.StateOf[Position] = Match->second;
                    continue;
                }
                const auto State = static_cast<uint32_t>(Scratch.Representatives.size());
                Scratch.Representatives.push_back(Run[Position].Index);
                Scratch.StatesByHash.emplace(Hash, State);
                Scratch.StateOf[Position] = State;
            }
            if (Scratch.Representatives.size() < 2)
                return;

            Scratch.Offsets.assign(Scratch.Representatives.size() + 1, 0);
            for (std::size_t Position = 0; Position < Run.size(); ++Position)
                ++Scratch.Offsets[Scratch.StateOf[Position] + 1];
            for (std::size_t State = 1; State < Scratch.Offsets.size(); ++State)
                Scratch.Offsets[State] += Scratch.Offsets[State - 1];
            Scratch.Grouped.resize(Run.size());
            for (std::size_t Position = 0; Position < Run.size(); ++Position)
                Scratch.Grouped[Scratch.Offsets[Scratch.StateOf[Position]]++] = Run[Position];
            std::copy(Scratch.Grouped.begin(), Scratch.Grouped.end(), Run.begin());
        }

        void WriteOrder(const std::vector<OrderKey>& Keys, std::vector<uint32_t>& Order)
        {
            Order.resize(Keys.size());
            for (std::size_t Position = 0; Position < Keys.size(); ++Position)
                Order[Position] = Keys[Position].Index;
        }
    } // namespace

    bool SameDrawState(const Triangle& A, const Triangle& B)
    {
        return A.GpuTexture == B.GpuTexture && A.AuxiliaryGpuTextures == B.AuxiliaryGpuTextures &&
               A.CubeGpuTexture == B.CubeGpuTexture && A.Transparent == B.Transparent &&
               A.AlphaDepthWrite == B.AlphaDepthWrite && A.Additive == B.Additive && A.Is2d == B.Is2d &&
               A.MatrixGhostMode == B.MatrixGhostMode && A.DepthTest == B.DepthTest && A.DepthWrite == B.DepthWrite &&
               A.Style == B.Style;
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
            Keys[Index] = {Phase, TriangleValue.RenderPriority,
                           SortDepth(Phase == 2 ? -TriangleValue.Depth : TriangleValue.Depth),
                           static_cast<uint32_t>(Index)};
        }
        SortKeys(Keys);

        GroupScratch Scratch;
        for (std::size_t Begin = 0; Begin < Keys.size();)
        {
            if (!OrderIndependent(Triangles[Keys[Begin].Index]))
            {
                ++Begin;
                continue;
            }
            const int Phase = Keys[Begin].Phase;
            const int Priority = Keys[Begin].Priority;
            std::size_t End = Begin + 1;
            while (End < Keys.size() && Keys[End].Phase == Phase && Keys[End].Priority == Priority &&
                   OrderIndependent(Triangles[Keys[End].Index]))
                ++End;
            GroupRun(Triangles, std::span<OrderKey>(Keys).subspan(Begin, End - Begin), Scratch);
            Begin = End;
        }
        WriteOrder(Keys, List.Order);
    }

    void OrderOverlayTriangles(OrderedTriangles& List)
    {
        std::vector<OrderKey> Keys(List.Triangles.size());
        for (std::size_t Index = 0; Index < List.Triangles.size(); ++Index)
            Keys[Index] = {0, 0, SortDepth(-List.Triangles[Index].Depth), static_cast<uint32_t>(Index)};
        SortKeys(Keys);
        WriteOrder(Keys, List.Order);
    }
} // namespace dbc
