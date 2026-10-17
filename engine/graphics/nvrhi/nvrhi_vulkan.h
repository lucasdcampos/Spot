#pragma once

// Backend-internal: the Vulkan objects an NVRHI device runs on, which NVRHI is given rather than makes, and the
// window's swapchain.

#include "graphics/graphics_device.h"

#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_SharedObject;

namespace Spot::Detail {

class VulkanSurface {
public:
    // Loads Vulkan and makes an instance and a device on the best GPU that runs Vulkan 1.3, with the window's surface
    // when there is one. Null, after logging why, when that cannot be done.
    static std::unique_ptr<VulkanSurface> Create(SDL_Window* window, const GraphicsConfig& config);
    // Whether a device can be made at all, without making one.
    static bool IsSupported();
    ~VulkanSurface();

    VulkanSurface(const VulkanSurface&) = delete;
    VulkanSurface& operator=(const VulkanSurface&) = delete;

    // Null, after logging why, when NVRHI rejects the device.
    nvrhi::vulkan::DeviceHandle CreateNvrhiDevice();
    std::string_view GetAdapterName() const { return m_adapterName; }
    bool HasWindow() const { return m_window != nullptr; }

    // The swapchain, made on first use and again when the window's size or the present mode changes.
    bool IsVSyncEnabled() const { return m_vsync; }
    void SetVSync(bool enabled);
    TextureFormat GetWindowFormat() const { return m_windowFormat; }
    // The image to draw the frame into, made to wait for the swapchain; null when there is none to draw into, such as
    // while the window is minimized.
    // The device is NVRHI's Vulkan one, below any validation layer, which can wait for and signal Vulkan's semaphores.
    nvrhi::ITexture* Acquire(nvrhi::vulkan::IDevice& device);
    // Makes the frame's submission, which comes next, signal what presenting waits for.
    void PrepareToPresent(nvrhi::vulkan::IDevice& device);
    void Present();
    std::uint32_t GetWidth() const { return m_extent.width; }
    std::uint32_t GetHeight() const { return m_extent.height; }
    // Makes the swapchain now, so the window's format is known before the first frame, as pipelines need it.
    bool PrepareSwapchain(nvrhi::IDevice& device) { return !m_window || !m_recreate || CreateSwapchain(device); }
    // Lets go of the swapchain's images, which the NVRHI device refers to, before the device goes.
    void ReleaseSwapchain();

private:
    VulkanSurface() = default;
    bool CreateSwapchain(nvrhi::IDevice& device);

    SDL_Window* m_window = nullptr;
    // The Vulkan loader when the device was made without a window, which SDL's video driver may not load.
    SDL_SharedObject* m_library = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    std::uint32_t m_queueFamily = 0;
    std::string m_adapterName;
    std::vector<const char*> m_instanceExtensions;
    std::vector<const char*> m_deviceExtensions;
    bool m_bufferDeviceAddress = false;
    bool m_linearWindow = true;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkExtent2D m_extent = {};
    TextureFormat m_windowFormat = TextureFormat::Bgra8Srgb;
    bool m_vsync = true;
    bool m_recreate = true;
    std::vector<nvrhi::TextureHandle> m_images;
    // An acquire semaphore per frame slot and a present semaphore per image, so neither is reused while in flight.
    std::vector<VkSemaphore> m_acquireSemaphores;
    std::vector<VkSemaphore> m_presentSemaphores;
    std::size_t m_acquireSlot = 0;
    std::uint32_t m_imageIndex = 0;
};

} // namespace Spot::Detail
