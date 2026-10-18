#include "graphics/nvrhi/nvrhi_internal.h"

#include "core/assert.h"
#include "core/log.h"
#include "graphics/nvrhi/nvrhi_vulkan.h"
#include "window/window.h"

#include <nvrhi/validation.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace Spot::Detail {

namespace {

std::uint32_t GetLevelSize(std::uint32_t size, std::uint32_t mipLevel) {
    return std::max(size >> mipLevel, 1u);
}

} // namespace

nvrhi::Format ToNvrhi(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8:
        return nvrhi::Format::RGBA8_UNORM;
    case TextureFormat::Bgra8:
        return nvrhi::Format::BGRA8_UNORM;
    case TextureFormat::Rgba8Srgb:
        return nvrhi::Format::SRGBA8_UNORM;
    case TextureFormat::Bgra8Srgb:
        return nvrhi::Format::SBGRA8_UNORM;
    case TextureFormat::Depth32Float:
        return nvrhi::Format::D32;
    case TextureFormat::Rgba16Float:
        return nvrhi::Format::RGBA16_FLOAT;
    case TextureFormat::R11G11B10Float:
        return nvrhi::Format::R11G11B10_FLOAT;
    case TextureFormat::R8:
        return nvrhi::Format::R8_UNORM;
    case TextureFormat::Rg8:
        return nvrhi::Format::RG8_UNORM;
    case TextureFormat::R16Float:
        return nvrhi::Format::R16_FLOAT;
    case TextureFormat::Rg16Float:
        return nvrhi::Format::RG16_FLOAT;
    case TextureFormat::R32Float:
        return nvrhi::Format::R32_FLOAT;
    case TextureFormat::Rg32Float:
        return nvrhi::Format::RG32_FLOAT;
    case TextureFormat::Rgba32Float:
        return nvrhi::Format::RGBA32_FLOAT;
    case TextureFormat::R32Uint:
        return nvrhi::Format::R32_UINT;
    case TextureFormat::Rgb10A2:
        return nvrhi::Format::R10G10B10A2_UNORM;
    case TextureFormat::Bc1:
        return nvrhi::Format::BC1_UNORM;
    case TextureFormat::Bc1Srgb:
        return nvrhi::Format::BC1_UNORM_SRGB;
    case TextureFormat::Bc3:
        return nvrhi::Format::BC3_UNORM;
    case TextureFormat::Bc3Srgb:
        return nvrhi::Format::BC3_UNORM_SRGB;
    case TextureFormat::Bc4:
        return nvrhi::Format::BC4_UNORM;
    case TextureFormat::Bc5:
        return nvrhi::Format::BC5_UNORM;
    case TextureFormat::Bc6hFloat:
        return nvrhi::Format::BC6H_UFLOAT;
    case TextureFormat::Bc7:
        return nvrhi::Format::BC7_UNORM;
    case TextureFormat::Bc7Srgb:
        return nvrhi::Format::BC7_UNORM_SRGB;
    }
    return nvrhi::Format::RGBA8_UNORM;
}

bool IsNvrhiVulkanSupported() {
    return VulkanSurface::IsSupported();
}

std::unique_ptr<GraphicsDevice::Impl> CreateNvrhiVulkanDevice(Window* window, const GraphicsConfig& config) {
    SDL_Window* sdlWindow = window ? static_cast<SDL_Window*>(window->GetNativeHandle()) : nullptr;
    auto vulkan = VulkanSurface::Create(sdlWindow, config);
    if (!vulkan) {
        return nullptr;
    }
    nvrhi::vulkan::DeviceHandle vulkanDevice = vulkan->CreateNvrhiDevice();
    if (!vulkanDevice) {
        return nullptr;
    }
    nvrhi::DeviceHandle device = vulkanDevice.Get();
    // NVRHI's own checks of how it is used, on top of Vulkan's validation layer.
    if (config.debug) {
        device = nvrhi::validation::createValidationLayer(device);
    }
    if (!vulkan->PrepareSwapchain(*device)) {
        Log::GetEngineLogger().Warning("The window has no swapchain yet; it gets one when it can be drawn into");
    }
    Log::GetEngineLogger().Info("Graphics device created with the 'vulkan (nvrhi)' backend on {}",
                                vulkan->GetAdapterName());
    return std::make_unique<NvrhiDevice>(
        std::make_shared<NvrhiShared>(std::move(vulkan), std::move(vulkanDevice), std::move(device)));
}

NvrhiShared::NvrhiShared(std::unique_ptr<VulkanSurface> vulkan, nvrhi::vulkan::DeviceHandle vulkanDevice,
                         nvrhi::DeviceHandle device)
    : vulkan(std::move(vulkan)), vulkanDevice(std::move(vulkanDevice)), device(std::move(device)) {
}

NvrhiShared::~NvrhiShared() {
    device->waitForIdle();
    // The swapchain's images are NVRHI textures, which must go before the device, and the device before Vulkan's.
    vulkan->ReleaseSwapchain();
    device = nullptr;
    vulkanDevice = nullptr;
    vulkan.reset();
}

NvrhiDevice::NvrhiDevice(std::shared_ptr<NvrhiShared> shared)
    : m_shared(std::move(shared)), m_vulkan(m_shared->vulkan.get()), m_vulkanDevice(m_shared->vulkanDevice.Get()),
      m_device(m_shared->device.Get()) {
    m_context.device = m_device;
    // Not immediate, so uploads can record into command lists of their own while the frame's is open.
    m_context.commandList =
        m_device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
    m_uploadCommandList =
        m_device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
    for (auto& query : m_frameQueries) {
        query = m_device->createEventQuery();
    }
    m_capabilities.compressedTextures = SupportsSampling(TextureFormat::Bc7);
    m_capabilities.maxColorTargets = 8;
    m_capabilities.compute = true;
    m_capabilities.timestamps = true;
    m_capabilities.bindlessTextures = m_vulkan->HasTextureTables();
    m_capabilities.indirectDraws = true;
    m_capabilities.maxIndirectDrawCount = m_vulkan->HasMultiDrawIndirect() ? 1u << 16 : 1;
}

NvrhiDevice::~NvrhiDevice() {
    FlushUploads();
    m_device->waitForIdle();
    m_context.Release();
    m_uploadCommandList = nullptr;
    for (auto& query : m_frameQueries) {
        query = nullptr;
    }
}

void NvrhiDevice::RunNow(const std::function<void(nvrhi::ICommandList&)>& record) {
    if (!m_uploadsOpen) {
        m_uploadCommandList->open();
        m_uploadsOpen = true;
    }
    record(*m_uploadCommandList);
    // Bounded, so loading many assets does not hold all of their staging memory at once.
    if (++m_pendingUploads >= MaxPendingUploads) {
        FlushUploads();
    }
}

void NvrhiDevice::FlushUploads() {
    if (!m_uploadsOpen) {
        return;
    }
    m_uploadCommandList->close();
    m_device->executeCommandList(m_uploadCommandList);
    m_uploadsOpen = false;
    m_pendingUploads = 0;
}

std::unique_ptr<Texture> NvrhiDevice::CreateTexture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0) {
        Log::GetEngineLogger().Error("Cannot create a {}x{} texture", desc.width, desc.height);
        return nullptr;
    }
    const bool depth = IsDepthFormat(desc.format);
    if (depth && desc.mipmaps) {
        Log::GetEngineLogger().Error("Depth textures cannot have mipmaps");
        return nullptr;
    }
    if (desc.sampleCount != 1 && desc.sampleCount != 2 && desc.sampleCount != 4 && desc.sampleCount != 8) {
        Log::GetEngineLogger().Error("Textures cannot have {} samples, only 1, 2, 4 or 8", desc.sampleCount);
        return nullptr;
    }
    const bool multisampled = desc.sampleCount > 1;
    if (multisampled && (!desc.renderTarget || desc.mipmaps)) {
        Log::GetEngineLogger().Error("Multisampled textures must be render targets without mipmaps");
        return nullptr;
    }
    const bool cube = desc.type == TextureType::Cube;
    if (cube && (desc.width != desc.height || depth || multisampled)) {
        Log::GetEngineLogger().Error("Cube textures must be square, and neither depth textures nor multisampled");
        return nullptr;
    }

    std::uint32_t mipLevels = 1;
    if (desc.mipmaps) {
        for (std::uint32_t size = std::max(desc.width, desc.height); size > 1; size /= 2) {
            ++mipLevels;
        }
    }

    nvrhi::TextureDesc native;
    native.width = desc.width;
    native.height = desc.height;
    native.arraySize = cube ? 6 : 1;
    native.mipLevels = mipLevels;
    native.sampleCount = desc.sampleCount;
    native.format = ToNvrhi(desc.format);
    native.dimension = cube           ? nvrhi::TextureDimension::TextureCube
                       : multisampled ? nvrhi::TextureDimension::Texture2DMS
                                      : nvrhi::TextureDimension::Texture2D;
    // Mipmaps are drawn, each level from the one above.
    native.isRenderTarget = desc.renderTarget || (mipLevels > 1 && !IsCompressedFormat(desc.format));
    native.isUAV = desc.storage;
    // NVRHI tracks each texture's state and places the barriers between its uses.
    native.enableAutomaticStateTracking(depth && desc.renderTarget ? nvrhi::ResourceStates::DepthWrite
                                        : desc.renderTarget        ? nvrhi::ResourceStates::RenderTarget
                                                                   : nvrhi::ResourceStates::ShaderResource);
    native.debugName = depth ? "Depth texture" : desc.renderTarget ? "Render target" : "Texture";
    nvrhi::TextureHandle handle = m_device->createTexture(native);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create a {}x{} texture", desc.width, desc.height);
        return nullptr;
    }
    auto impl = std::make_unique<NvrhiTexture>();
    impl->shared = m_shared;
    impl->handle = std::move(handle);
    impl->desc = desc;
    impl->mipLevels = mipLevels;
    return GraphicsAccess::Make<Texture>(std::move(impl));
}

void NvrhiDevice::UploadToTexture(Texture& texture, std::uint32_t mipLevel, std::span<const std::byte> pixels,
                                  bool generateMipmaps) {
    const TextureFormat format = texture.GetFormat();
    const std::uint32_t width = GetLevelSize(texture.GetWidth(), mipLevel);
    const std::uint32_t height = GetLevelSize(texture.GetHeight(), mipLevel);
    // A row of pixels, or of 4x4 blocks for compressed formats.
    const std::uint32_t rowPitch = GetTextureDataSize(format, width, 1);
    const std::size_t layerSize = GetTextureDataSize(format, width, height);
    nvrhi::ITexture* native = GetNvrhiTexture(texture);
    RunNow([&](nvrhi::ICommandList& commandList) {
        for (std::uint32_t layer = 0; layer < texture.GetLayerCount(); ++layer) {
            commandList.writeTexture(native, layer, mipLevel, pixels.data() + layer * layerSize, rowPitch);
        }
        if (generateMipmaps) {
            m_context.RecordMipmaps(commandList, texture);
        }
    });
}

std::vector<std::uint8_t> NvrhiDevice::DownloadTexture(const Texture& texture, std::uint32_t mipLevel) {
    const std::uint32_t width = GetLevelSize(texture.GetWidth(), mipLevel);
    const std::uint32_t height = GetLevelSize(texture.GetHeight(), mipLevel);
    const std::uint32_t layers = texture.GetLayerCount();

    nvrhi::TextureDesc stagingDesc;
    stagingDesc.width = width;
    stagingDesc.height = height;
    stagingDesc.arraySize = layers;
    stagingDesc.format = ToNvrhi(texture.GetFormat());
    stagingDesc.dimension = layers > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D;
    stagingDesc.debugName = "Texture download";
    const nvrhi::StagingTextureHandle staging = m_device->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);
    if (!staging) {
        Log::GetEngineLogger().Error("Failed to create a texture to download into");
        return {};
    }
    nvrhi::ITexture* native = GetNvrhiTexture(texture);
    RunNow([&](nvrhi::ICommandList& commandList) {
        for (std::uint32_t layer = 0; layer < layers; ++layer) {
            commandList.copyTexture(staging, nvrhi::TextureSlice().setArraySlice(layer), native,
                                    nvrhi::TextureSlice().setMipLevel(mipLevel).setArraySlice(layer));
        }
    });
    FlushUploads();

    const std::size_t rowSize = GetTextureDataSize(texture.GetFormat(), width, 1);
    const std::uint32_t rows = IsCompressedFormat(texture.GetFormat()) ? (height + 3) / 4 : height;
    std::vector<std::uint8_t> pixels(rowSize * rows * layers);
    for (std::uint32_t layer = 0; layer < layers; ++layer) {
        std::size_t rowPitch = 0;
        // Mapping waits for the copy to finish.
        const auto* mapped = static_cast<const std::uint8_t*>(m_device->mapStagingTexture(
            staging, nvrhi::TextureSlice().setArraySlice(layer), nvrhi::CpuAccessMode::Read, &rowPitch));
        if (!mapped) {
            Log::GetEngineLogger().Error("Failed to read the downloaded texture back");
            return {};
        }
        std::uint8_t* destination = pixels.data() + static_cast<std::size_t>(layer) * rowSize * rows;
        for (std::uint32_t row = 0; row < rows; ++row) {
            std::memcpy(destination + row * rowSize, mapped + row * rowPitch, rowSize);
        }
        m_device->unmapStagingTexture(staging);
    }
    return pixels;
}

bool NvrhiDevice::SupportsSampleCount(TextureFormat format, std::uint32_t sampleCount) const {
    if (sampleCount == 1) {
        return true;
    }
    if (sampleCount != 2 && sampleCount != 4 && sampleCount != 8) {
        return false;
    }
    const nvrhi::FormatSupport support = m_device->queryFormatSupport(ToNvrhi(format));
    const nvrhi::FormatSupport needed =
        IsDepthFormat(format) ? nvrhi::FormatSupport::DepthStencil : nvrhi::FormatSupport::RenderTarget;
    return (support & needed) == needed;
}

bool NvrhiDevice::SupportsRenderTarget(TextureFormat format) const {
    const nvrhi::FormatSupport support = m_device->queryFormatSupport(ToNvrhi(format));
    const nvrhi::FormatSupport needed =
        (IsDepthFormat(format) ? nvrhi::FormatSupport::DepthStencil : nvrhi::FormatSupport::RenderTarget) |
        nvrhi::FormatSupport::ShaderSample;
    return (support & needed) == needed;
}

bool NvrhiDevice::SupportsSampling(TextureFormat format) const {
    const nvrhi::FormatSupport support = m_device->queryFormatSupport(ToNvrhi(format));
    const nvrhi::FormatSupport needed = nvrhi::FormatSupport::Texture | nvrhi::FormatSupport::ShaderLoad;
    return (support & needed) == needed;
}

TextureFormat NvrhiDevice::GetWindowFormat() const {
    return m_vulkan->HasWindow() ? m_vulkan->GetWindowFormat() : TextureFormat::Rgba8Srgb;
}

bool NvrhiDevice::IsVSyncEnabled() const {
    return m_vulkan->HasWindow() && m_vulkan->IsVSyncEnabled();
}

void NvrhiDevice::SetVSync(bool enabled) {
    if (m_vulkan->HasWindow()) {
        m_vulkan->SetVSync(enabled);
    }
}

bool NvrhiDevice::BeginFrame() {
    SPOT_ASSERT(!m_context.frameOpen, "BeginFrame() called while a frame is open");
    if (!m_vulkan->HasWindow()) {
        return false;
    }
    // The slot's last frame finished on the GPU before this one records over what it used.
    if (m_frameQueried[m_frameSlot]) {
        m_device->waitEventQuery(m_frameQueries[m_frameSlot]);
        m_context.CollectTimers(m_frameSlot, m_timings);
    }
    m_context.frameSlot = m_frameSlot;
    nvrhi::ITexture* image = m_vulkan->Acquire(*m_vulkanDevice);
    if (!image) {
        return false;
    }
    m_context.commandList->open();
    m_context.target = image;
    m_context.acquiredTarget = image;
    m_context.width = m_vulkan->GetWidth();
    m_context.height = m_vulkan->GetHeight();
    m_context.format = m_vulkan->GetWindowFormat();
    m_context.frameOpen = true;
    m_presenting = true;
    return true;
}

bool NvrhiDevice::BeginOffscreenFrame(Texture& target) {
    SPOT_ASSERT(!m_context.frameOpen, "A frame is already open");
    if (!target.IsRenderTarget() || target.GetSampleCount() != 1 || target.GetLayerCount() != 1 ||
        IsDepthFormat(target.GetFormat())) {
        Log::GetEngineLogger().Error("Offscreen frames need a single-sample 2D color render target");
        return false;
    }
    if (m_frameQueried[m_frameSlot]) {
        m_device->waitEventQuery(m_frameQueries[m_frameSlot]);
        m_context.CollectTimers(m_frameSlot, m_timings);
    }
    m_context.frameSlot = m_frameSlot;
    m_context.commandList->open();
    m_context.target = GetNvrhiTexture(target);
    m_context.acquiredTarget = m_context.target;
    m_context.width = target.GetWidth();
    m_context.height = target.GetHeight();
    m_context.format = target.GetFormat();
    m_context.frameOpen = true;
    return true;
}

void NvrhiDevice::EndFrame() {
    SPOT_ASSERT(m_context.frameOpen, "EndFrame() called without an open frame");
    SPOT_ASSERT(!m_context.passOpen, "A render pass is still open at the end of the frame");
    m_context.CheckTimersEnded();
    m_context.commandList->close();
    const bool presents = std::exchange(m_presenting, false);
    if (presents) {
        m_vulkan->PrepareToPresent(*m_vulkanDevice);
    }
    // The frame's uploads go first, as uploads made during a frame reach the GPU before its draws.
    FlushUploads();
    m_device->executeCommandList(m_context.commandList);
    m_device->resetEventQuery(m_frameQueries[m_frameSlot]);
    m_device->setEventQuery(m_frameQueries[m_frameSlot], nvrhi::CommandQueue::Graphics);
    m_frameQueried[m_frameSlot] = true;
    if (presents) {
        m_vulkan->Present();
    }
    m_context.FinishFrame();
    // Frees what was released once the GPU is done with it.
    m_device->runGarbageCollection();
    m_frameSlot = (m_frameSlot + 1) % FramesInFlight;
    ++m_frameNumber;
    m_context.target = nullptr;
    m_context.acquiredTarget = nullptr;
    m_context.width = 0;
    m_context.height = 0;
    m_context.frameOpen = false;
}

} // namespace Spot::Detail
