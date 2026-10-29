#include "engine_kernel.hpp"
#include "audio_system.hpp"
#include "bsp_world.hpp"
#include "camera_system.hpp"
#include "core_system.hpp"
#include "debug_system.hpp"
#include "dll_system.hpp"
#include "draw_system.hpp"
#include "file_io_system.hpp"
#include "graphics_device_system.hpp"
#include "image_store.hpp"
#include "input_system.hpp"
#include "light_manager.hpp"
#include "log.hpp"
#include "matrix_manager.hpp"
#include "memblock_system.hpp"
#include "network_system.hpp"
#include "object_manager.hpp"
#include "physics_system.hpp"
#include "renderer_system.hpp"
#include "sprite_system.hpp"
#include "text_system.hpp"
#include "video_system.hpp"
#include "vr_system.hpp"

#include <utility>

namespace dbc
{
    EngineKernel::EngineKernel()
        : RuntimeCompatibility_(
              std::make_unique<const RuntimeCompatibilityPolicy>(RuntimeCompatibilityPolicy::Professional()))
    {
    }

    EngineKernel::~EngineKernel()
    {
        if (Initialized_)
        {
            Shutdown();
        }
    }

    bool EngineKernel::Initialize(const std::string& /*title*/, int /*width*/, int /*height*/)
    {
        if (Initialized_)
            Shutdown();
        ConfigureRuntimeCompatibility(RuntimeCompatibilityPolicy::Professional());

        // Create subsystems in dependency order (independent first).
        CoreSystem_ = std::make_unique<CoreSystem>(*this);
        FileIo_ = std::make_unique<FileIOSystem>(*this);
        Camera_ = std::make_unique<CameraSystem>(*this);
        Audio_ = std::make_unique<AudioSystem>(*this);
        Input_ = std::make_unique<InputSystem>(*this);
        Matrices_ = std::make_unique<MatrixManager>(*this);
        BspWorld_ = std::make_unique<BspWorldSystem>(*this);
        Lights_ = std::make_unique<LightManager>(*this);
        GraphicsDevice_ = std::make_unique<GraphicsDeviceSystem>(*this);
        Vr_ = std::make_unique<VrSystem>(*this);
        ImageStore_ = std::make_unique<ImageStore>(*this);
        ObjectManager_ = std::make_unique<ObjectManager>(*this);
        PhysicsSystem_ = std::make_unique<PhysicsSystem>(*this);
        TextSystem_ = std::make_unique<TextSystem>(*this);
        MemblockSystem_ = std::make_unique<MemblockSystem>(*this);
        DrawSystem_ = std::make_unique<DrawSystem>(*this);
        Renderer_ = std::make_unique<RendererSystem>(*this);
        SpriteSystem_ = std::make_unique<SpriteSystem>(*this);
        DebugSystem_ = std::make_unique<DebugSystem>(*this);
        Video_ = std::make_unique<VideoSystem>(*this);
        Network_ = std::make_unique<NetworkSystem>();
        Dlls_ = std::make_unique<DllSystem>(*this);
        dbc::log::AttachRingBuffer(DebugSystem_.get());

        Initialized_ = true;
        return true;
    }

    void EngineKernel::Shutdown()
    {
        // Release systems that own GPU resources before CoreSystem tears down
        // the renderer and device.
        dbc::log::DetachRingBuffer(DebugSystem_.get());
        if (Vr_)
            Vr_->DetachDevice();
        Dlls_.reset();
        Network_.reset();
        Video_.reset();
        DebugSystem_.reset();
        SpriteSystem_.reset();
        Renderer_.reset();
        DrawSystem_.reset();
        MemblockSystem_.reset();
        PhysicsSystem_.reset();
        ObjectManager_.reset();
        BspWorld_.reset();
        Matrices_.reset();
        ImageStore_.reset();
        Input_.reset();

        if (CoreSystem_)
        {
            CoreSystem_->Shutdown();
        }

        TextSystem_.reset();
        Vr_.reset();
        GraphicsDevice_.reset();
        Lights_.reset();
        Audio_.reset();
        Camera_.reset();
        FileIo_.reset();
        CoreSystem_.reset();

        Initialized_ = false;
    }

    bool EngineKernel::IsRunning() const
    {
        return Initialized_;
    }

    void EngineKernel::ConfigureRuntimeCompatibility(RuntimeCompatibilityPolicy Policy)
    {
        RuntimeCompatibility_ = std::make_unique<const RuntimeCompatibilityPolicy>(std::move(Policy));
        if (Renderer_)
            Renderer_->ResetRuntimeCompatibilityState();
        if (SpriteSystem_)
            SpriteSystem_->ClearBobTrails();
        if (TextSystem_)
        {
            TextSystem_->ClearDebugTextLines();
            TextSystem_->ClearOverlayTextLines();
        }
    }

    const RuntimeCompatibilityPolicy& EngineKernel::RuntimeCompatibility() const
    {
        return *RuntimeCompatibility_;
    }

    VrSystem& EngineKernel::Vr()
    {
        return *Vr_;
    }

    const VrSystem& EngineKernel::Vr() const
    {
        return *Vr_;
    }

    // --- Subsystem accessors ---

    FileIOSystem& EngineKernel::FileIO()
    {
        return *FileIo_;
    }

    const FileIOSystem& EngineKernel::FileIO() const
    {
        return *FileIo_;
    }

    CameraSystem& EngineKernel::Camera()
    {
        return *Camera_;
    }

    const CameraSystem& EngineKernel::Camera() const
    {
        return *Camera_;
    }

    AudioSystem& EngineKernel::Audio()
    {
        return *Audio_;
    }

    const AudioSystem& EngineKernel::Audio() const
    {
        return *Audio_;
    }

    InputSystem& EngineKernel::Input()
    {
        return *Input_;
    }

    const InputSystem& EngineKernel::Input() const
    {
        return *Input_;
    }

    MatrixManager& EngineKernel::Matrices()
    {
        return *Matrices_;
    }

    const MatrixManager& EngineKernel::Matrices() const
    {
        return *Matrices_;
    }

    BspWorldSystem& EngineKernel::BspWorld()
    {
        return *BspWorld_;
    }

    const BspWorldSystem& EngineKernel::BspWorld() const
    {
        return *BspWorld_;
    }

    LightManager& EngineKernel::Lights()
    {
        return *Lights_;
    }

    const LightManager& EngineKernel::Lights() const
    {
        return *Lights_;
    }

    ImageStore& EngineKernel::ImageStoreRef()
    {
        return *ImageStore_;
    }

    const ImageStore& EngineKernel::ImageStoreRef() const
    {
        return *ImageStore_;
    }

    ObjectManager& EngineKernel::Objects()
    {
        return *ObjectManager_;
    }

    const ObjectManager& EngineKernel::Objects() const
    {
        return *ObjectManager_;
    }

    PhysicsSystem& EngineKernel::Physics()
    {
        return *PhysicsSystem_;
    }

    const PhysicsSystem& EngineKernel::Physics() const
    {
        return *PhysicsSystem_;
    }

    TextSystem& EngineKernel::Text()
    {
        return *TextSystem_;
    }

    const TextSystem& EngineKernel::Text() const
    {
        return *TextSystem_;
    }

    MemblockSystem& EngineKernel::Memblocks()
    {
        return *MemblockSystem_;
    }

    const MemblockSystem& EngineKernel::Memblocks() const
    {
        return *MemblockSystem_;
    }

    DrawSystem& EngineKernel::Draw()
    {
        return *DrawSystem_;
    }

    const DrawSystem& EngineKernel::Draw() const
    {
        return *DrawSystem_;
    }

    GraphicsDeviceSystem& EngineKernel::Gpu()
    {
        return *GraphicsDevice_;
    }

    const GraphicsDeviceSystem& EngineKernel::Gpu() const
    {
        return *GraphicsDevice_;
    }

    CoreSystem& EngineKernel::Core()
    {
        return *CoreSystem_;
    }

    const CoreSystem& EngineKernel::Core() const
    {
        return *CoreSystem_;
    }

    RendererSystem& EngineKernel::Renderer()
    {
        return *Renderer_;
    }

    const RendererSystem& EngineKernel::Renderer() const
    {
        return *Renderer_;
    }

    SpriteSystem& EngineKernel::Sprites()
    {
        return *SpriteSystem_;
    }

    const SpriteSystem& EngineKernel::Sprites() const
    {
        return *SpriteSystem_;
    }

    DebugSystem& EngineKernel::Debug()
    {
        return *DebugSystem_;
    }

    const DebugSystem& EngineKernel::Debug() const
    {
        return *DebugSystem_;
    }

    VideoSystem& EngineKernel::Video()
    {
        return *Video_;
    }

    const VideoSystem& EngineKernel::Video() const
    {
        return *Video_;
    }

    NetworkSystem& EngineKernel::Network()
    {
        return *Network_;
    }

    const NetworkSystem& EngineKernel::Network() const
    {
        return *Network_;
    }

    DllSystem& EngineKernel::Dlls()
    {
        return *Dlls_;
    }

    const DllSystem& EngineKernel::Dlls() const
    {
        return *Dlls_;
    }

} // namespace dbc
