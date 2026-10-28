#pragma once

#include "../runtime_compatibility.hpp"

#include <memory>
#include <string>

namespace dbc
{
    // Forward declarations — subsystems added incrementally.
    class FileIOSystem;
    class CameraSystem;
    class AudioSystem;
    class InputSystem;
    class MatrixManager;
    class BspWorldSystem;
    class LightManager;
    class ImageStore;
    class ObjectManager;
    class PhysicsSystem;
    class TextSystem;
    class MemblockSystem;
    class DrawSystem;
    class GraphicsDeviceSystem;
    class CoreSystem;
    class RendererSystem;
    class SpriteSystem;
    class DebugSystem;
    class VideoSystem;
    class NetworkSystem;
    class DllSystem;
    class VrSystem;

    class EngineKernel
    {
      public:
        EngineKernel();
        ~EngineKernel();

        EngineKernel(const EngineKernel&) = delete;
        EngineKernel& operator=(const EngineKernel&) = delete;

        bool Initialize(const std::string& Title, int Width, int Height);
        void Shutdown();
        bool IsRunning() const;

        void ConfigureRuntimeCompatibility(RuntimeCompatibilityPolicy Policy);
        [[nodiscard]] const RuntimeCompatibilityPolicy& RuntimeCompatibility() const;

        VrSystem& Vr();
        const VrSystem& Vr() const;

        // --- Subsystem accessors ---
        FileIOSystem& FileIO();
        const FileIOSystem& FileIO() const;
        CameraSystem& Camera();
        const CameraSystem& Camera() const;
        AudioSystem& Audio();
        const AudioSystem& Audio() const;
        InputSystem& Input();
        const InputSystem& Input() const;
        MatrixManager& Matrices();
        const MatrixManager& Matrices() const;
        BspWorldSystem& BspWorld();
        const BspWorldSystem& BspWorld() const;
        LightManager& Lights();
        const LightManager& Lights() const;
        ImageStore& ImageStoreRef();
        const ImageStore& ImageStoreRef() const;
        ObjectManager& Objects();
        const ObjectManager& Objects() const;
        PhysicsSystem& Physics();
        const PhysicsSystem& Physics() const;
        TextSystem& Text();
        const TextSystem& Text() const;
        MemblockSystem& Memblocks();
        const MemblockSystem& Memblocks() const;
        DrawSystem& Draw();
        const DrawSystem& Draw() const;
        GraphicsDeviceSystem& Gpu();
        const GraphicsDeviceSystem& Gpu() const;
        CoreSystem& Core();
        const CoreSystem& Core() const;
        RendererSystem& Renderer();
        const RendererSystem& Renderer() const;
        SpriteSystem& Sprites();
        const SpriteSystem& Sprites() const;
        DebugSystem& Debug();
        const DebugSystem& Debug() const;
        VideoSystem& Video();
        const VideoSystem& Video() const;
        NetworkSystem& Network();
        const NetworkSystem& Network() const;
        DllSystem& Dlls();
        const DllSystem& Dlls() const;

      private:
        bool Initialized_ = false;
        std::unique_ptr<const RuntimeCompatibilityPolicy> RuntimeCompatibility_;
        std::unique_ptr<FileIOSystem> FileIo_;
        std::unique_ptr<CameraSystem> Camera_;
        std::unique_ptr<AudioSystem> Audio_;
        std::unique_ptr<InputSystem> Input_;
        std::unique_ptr<MatrixManager> Matrices_;
        std::unique_ptr<BspWorldSystem> BspWorld_;
        std::unique_ptr<LightManager> Lights_;
        std::unique_ptr<ImageStore> ImageStore_;
        std::unique_ptr<ObjectManager> ObjectManager_;
        std::unique_ptr<PhysicsSystem> PhysicsSystem_;
        std::unique_ptr<TextSystem> TextSystem_;
        std::unique_ptr<MemblockSystem> MemblockSystem_;
        std::unique_ptr<DrawSystem> DrawSystem_;
        std::unique_ptr<GraphicsDeviceSystem> GraphicsDevice_;
        std::unique_ptr<CoreSystem> CoreSystem_;
        std::unique_ptr<RendererSystem> Renderer_;
        std::unique_ptr<SpriteSystem> SpriteSystem_;
        std::unique_ptr<DebugSystem> DebugSystem_;
        std::unique_ptr<VideoSystem> Video_;
        std::unique_ptr<NetworkSystem> Network_;
        std::unique_ptr<DllSystem> Dlls_;
        std::unique_ptr<VrSystem> Vr_;
    };

} // namespace dbc
