#include "graphics/nvrhi/nvrhi_vulkan.h"

#include "core/log.h"
#include "platform/platform.h"

#include <nvrhi/vulkan.h>

// NVRHI calls Vulkan through vulkan.hpp's default dispatcher; this file loads Vulkan's functions into it, so both see
// the same functions, and calls Vulkan's C API through it.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <string_view>

// Built as a static library, NVRHI leaves the dispatcher's definition, and loading it, to the program: this file
// does both, before NVRHI's device is made.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Spot::Detail {

namespace {

constexpr std::uint32_t RequiredVersion = VK_API_VERSION_1_3;
constexpr const char* ValidationLayer = "VK_LAYER_KHRONOS_validation";

auto& Vk() {
    return VULKAN_HPP_DEFAULT_DISPATCHER;
}

std::string_view ToString(VkResult result) {
    return nvrhi::vulkan::resultToString(result);
}

VKAPI_ATTR VkBool32 VKAPI_CALL ReportValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                VkDebugUtilsMessageTypeFlagsEXT,
                                                const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const LogLevel level =
        severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? LogLevel::Error : LogLevel::Warning;
    Log::GetEngineLogger().Write(level, "Vulkan: {}", data && data->pMessage ? data->pMessage : "(no message)");
    return VK_FALSE;
}

#if defined(_WIN32)
constexpr const char* VulkanLoader = "vulkan-1.dll";
#else
constexpr const char* VulkanLoader = "libvulkan.so.1";
#endif

// A window's device loads Vulkan through SDL's video driver, which makes its surfaces. A device without a window
// loads the loader itself, so it runs whatever the video driver is, even one with no Vulkan, as headless runs use.
bool LoadVulkan(bool window, SDL_SharedObject*& library, bool quiet) {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    if (window) {
        if (SDL_Vulkan_LoadLibrary(nullptr)) {
            getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
            if (!getInstanceProcAddr) {
                SDL_Vulkan_UnloadLibrary();
            }
        }
    } else if ((library = SDL_LoadObject(VulkanLoader))) {
        getInstanceProcAddr =
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_LoadFunction(library, "vkGetInstanceProcAddr"));
        if (!getInstanceProcAddr) {
            SDL_UnloadObject(library);
            library = nullptr;
        }
    }
    if (!getInstanceProcAddr) {
        if (!quiet) {
            Log::GetEngineLogger().Error("Vulkan is not available: {}", SDL_GetError());
        }
        return false;
    }
    Vk().init(getInstanceProcAddr);
    return true;
}

void UnloadVulkan(SDL_SharedObject* library) {
    if (library) {
        SDL_UnloadObject(library);
    } else {
        SDL_Vulkan_UnloadLibrary();
    }
}

bool HasInstanceLayer(const char* name) {
    std::uint32_t count = 0;
    Vk().vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    Vk().vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::ranges::any_of(layers, [name](const auto& layer) { return std::strcmp(layer.layerName, name) == 0; });
}

bool HasInstanceExtension(const char* name) {
    std::uint32_t count = 0;
    Vk().vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    Vk().vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::ranges::any_of(
        extensions, [name](const auto& extension) { return std::strcmp(extension.extensionName, name) == 0; });
}

bool HasDeviceExtension(VkPhysicalDevice device, const char* name) {
    std::uint32_t count = 0;
    Vk().vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    Vk().vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return std::ranges::any_of(
        extensions, [name](const auto& extension) { return std::strcmp(extension.extensionName, name) == 0; });
}

// The features the engine and NVRHI use, chained as Vulkan queries and enables them.
struct DeviceFeatures {
    VkPhysicalDeviceFeatures2 core{};
    VkPhysicalDeviceVulkan12Features vulkan12{};
    VkPhysicalDeviceVulkan13Features vulkan13{};

    DeviceFeatures() {
        core.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        core.pNext = &vulkan12;
        vulkan12.pNext = &vulkan13;
    }
    DeviceFeatures(const DeviceFeatures&) = delete;
    DeviceFeatures& operator=(const DeviceFeatures&) = delete;
};

// Keeps what the GPU supports among what is wanted; false when something NVRHI needs is missing.
bool SelectFeatures(const DeviceFeatures& supported, DeviceFeatures& enabled) {
    if (!supported.vulkan12.timelineSemaphore || !supported.vulkan13.synchronization2) {
        return false;
    }
    const VkPhysicalDeviceFeatures& has = supported.core.features;
    VkPhysicalDeviceFeatures& use = enabled.core.features;
    use.samplerAnisotropy = has.samplerAnisotropy;
    use.depthClamp = has.depthClamp;
    use.imageCubeArray = has.imageCubeArray;
    use.independentBlend = has.independentBlend;
    use.fillModeNonSolid = has.fillModeNonSolid;
    use.textureCompressionBC = has.textureCompressionBC;
    use.shaderImageGatherExtended = has.shaderImageGatherExtended;
    use.dualSrcBlend = has.dualSrcBlend;
    use.multiDrawIndirect = has.multiDrawIndirect;
    use.drawIndirectFirstInstance = has.drawIndirectFirstInstance;
    use.shaderInt16 = has.shaderInt16;
    use.fragmentStoresAndAtomics = has.fragmentStoresAndAtomics;
    use.vertexPipelineStoresAndAtomics = has.vertexPipelineStoresAndAtomics;

    const auto& has12 = supported.vulkan12;
    auto& use12 = enabled.vulkan12;
    use12.timelineSemaphore = VK_TRUE;
    use12.descriptorIndexing = has12.descriptorIndexing;
    use12.runtimeDescriptorArray = has12.runtimeDescriptorArray;
    use12.descriptorBindingPartiallyBound = has12.descriptorBindingPartiallyBound;
    use12.descriptorBindingVariableDescriptorCount = has12.descriptorBindingVariableDescriptorCount;
    use12.shaderSampledImageArrayNonUniformIndexing = has12.shaderSampledImageArrayNonUniformIndexing;
    use12.descriptorBindingSampledImageUpdateAfterBind = has12.descriptorBindingSampledImageUpdateAfterBind;
    use12.bufferDeviceAddress = has12.bufferDeviceAddress;
    use12.hostQueryReset = has12.hostQueryReset;
    use12.scalarBlockLayout = has12.scalarBlockLayout;

    auto& use13 = enabled.vulkan13;
    use13.synchronization2 = VK_TRUE;
    use13.maintenance4 = supported.vulkan13.maintenance4;
    use13.dynamicRendering = supported.vulkan13.dynamicRendering;
    return true;
}

TextureFormat FromVulkan(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        return TextureFormat::Rgba8;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return TextureFormat::Rgba8Srgb;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return TextureFormat::Bgra8Srgb;
    default:
        return TextureFormat::Bgra8;
    }
}

nvrhi::Format ToNvrhiFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        return nvrhi::Format::RGBA8_UNORM;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return nvrhi::Format::SRGBA8_UNORM;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return nvrhi::Format::SBGRA8_UNORM;
    default:
        return nvrhi::Format::BGRA8_UNORM;
    }
}

class MessageLog final : public nvrhi::IMessageCallback {
public:
    void message(nvrhi::MessageSeverity severity, const char* text) override {
        const LogLevel level = severity == nvrhi::MessageSeverity::Info      ? LogLevel::Debug
                               : severity == nvrhi::MessageSeverity::Warning ? LogLevel::Warning
                                                                             : LogLevel::Error;
        Log::GetEngineLogger().Write(level, "NVRHI: {}", text);
    }
};

MessageLog s_messageLog;

} // namespace

bool VulkanSurface::IsSupported() {
    if (!Platform::Initialize()) {
        return false;
    }
    bool supported = false;
    SDL_SharedObject* library = nullptr;
    if (LoadVulkan(false, library, true)) {
        std::uint32_t version = VK_API_VERSION_1_0;
        if (Vk().vkEnumerateInstanceVersion) {
            Vk().vkEnumerateInstanceVersion(&version);
        }
        supported = version >= RequiredVersion;
        UnloadVulkan(library);
    }
    Platform::Shutdown();
    return supported;
}

std::unique_ptr<VulkanSurface> VulkanSurface::Create(SDL_Window* window, const GraphicsConfig& config) {
    if (!Platform::Initialize()) {
        return nullptr;
    }
    SDL_SharedObject* library = nullptr;
    if (!LoadVulkan(window != nullptr, library, false)) {
        Platform::Shutdown();
        return nullptr;
    }
    // From here on its destructor unloads Vulkan and shuts the platform down, whatever fails.
    std::unique_ptr<VulkanSurface> vulkan(new VulkanSurface());
    vulkan->m_library = library;
    vulkan->m_window = window;
    vulkan->m_vsync = config.vsync;
    vulkan->m_linearWindow = config.linearWindow;

    std::uint32_t version = VK_API_VERSION_1_0;
    if (Vk().vkEnumerateInstanceVersion) {
        Vk().vkEnumerateInstanceVersion(&version);
    }
    if (version < RequiredVersion) {
        Log::GetEngineLogger().Error("Vulkan {}.{} is installed; the Vulkan backend needs 1.3",
                                     VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
        return nullptr;
    }

    if (window) {
        std::uint32_t count = 0;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        vulkan->m_instanceExtensions.assign(extensions, extensions + count);
    }
    const bool debugUtils = HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils) {
        vulkan->m_instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    std::vector<const char*> layers;
    if (config.debug) {
        if (HasInstanceLayer(ValidationLayer)) {
            layers.push_back(ValidationLayer);
        } else {
            Log::GetEngineLogger().Warning("Vulkan's validation layer is not installed; running without it");
        }
    }

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Spot";
    application.pEngineName = "Spot";
    application.apiVersion = RequiredVersion;
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &application;
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(vulkan->m_instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = vulkan->m_instanceExtensions.data();
    instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    if (const VkResult result = Vk().vkCreateInstance(&instanceInfo, nullptr, &vulkan->m_instance);
        result != VK_SUCCESS) {
        Log::GetEngineLogger().Error("Failed to create a Vulkan instance: {}", ToString(result));
        return nullptr;
    }
    Vk().init(vk::Instance(vulkan->m_instance));

    if (debugUtils && !layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
        messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = &ReportValidation;
        Vk().vkCreateDebugUtilsMessengerEXT(vulkan->m_instance, &messengerInfo, nullptr, &vulkan->m_messenger);
    }

    if (window && !SDL_Vulkan_CreateSurface(window, vulkan->m_instance, nullptr, &vulkan->m_surface)) {
        Log::GetEngineLogger().Error("Failed to create the window's Vulkan surface: {}", SDL_GetError());
        return nullptr;
    }

    // The best GPU that runs Vulkan 1.3 with what NVRHI needs: a discrete one over an integrated one.
    std::uint32_t deviceCount = 0;
    Vk().vkEnumeratePhysicalDevices(vulkan->m_instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    Vk().vkEnumeratePhysicalDevices(vulkan->m_instance, &deviceCount, devices.data());
    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        Vk().vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < RequiredVersion ||
            (window && !HasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME))) {
            continue;
        }
        DeviceFeatures supported;
        Vk().vkGetPhysicalDeviceFeatures2(candidate, &supported.core);
        DeviceFeatures unused;
        if (!SelectFeatures(supported, unused)) {
            continue;
        }
        std::uint32_t familyCount = 0;
        Vk().vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        Vk().vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (std::uint32_t family = 0; family < familyCount; ++family) {
            VkBool32 presents = VK_TRUE;
            if (window) {
                Vk().vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, vulkan->m_surface, &presents);
            }
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presents) {
                const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
                if (score > bestScore) {
                    bestScore = score;
                    vulkan->m_physicalDevice = candidate;
                    vulkan->m_queueFamily = family;
                    vulkan->m_adapterName = properties.deviceName;
                }
                break;
            }
        }
    }
    if (!vulkan->m_physicalDevice) {
        Log::GetEngineLogger().Error("No GPU runs Vulkan 1.3 with timeline semaphores and synchronization2");
        return nullptr;
    }

    DeviceFeatures supported;
    Vk().vkGetPhysicalDeviceFeatures2(vulkan->m_physicalDevice, &supported.core);
    DeviceFeatures enabled;
    SelectFeatures(supported, enabled);
    vulkan->m_bufferDeviceAddress = enabled.vulkan12.bufferDeviceAddress == VK_TRUE;
    vulkan->m_multiDrawIndirect = enabled.core.features.multiDrawIndirect == VK_TRUE;
    const VkPhysicalDeviceVulkan12Features& indexing = enabled.vulkan12;
    vulkan->m_textureTables = indexing.runtimeDescriptorArray && indexing.descriptorBindingPartiallyBound &&
                              indexing.descriptorBindingVariableDescriptorCount &&
                              indexing.descriptorBindingSampledImageUpdateAfterBind;
    if (window) {
        vulkan->m_deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    // NVRHI turns depth clipping off, as pipelines that clamp depth ask, such as shadow casters behind a shadow
    // view's near plane, only through this extension.
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depthClip{};
    depthClip.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT;
    if (HasDeviceExtension(vulkan->m_physicalDevice, VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 query{};
        query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        query.pNext = &depthClip;
        Vk().vkGetPhysicalDeviceFeatures2(vulkan->m_physicalDevice, &query);
        if (depthClip.depthClipEnable) {
            vulkan->m_deviceExtensions.push_back(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
            depthClip.pNext = nullptr;
            enabled.vulkan13.pNext = &depthClip;
        }
    }
    if (!enabled.vulkan13.pNext) {
        Log::GetEngineLogger().Warning("This GPU cannot turn depth clipping off, so shadows may miss far casters");
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = vulkan->m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &enabled.core;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(vulkan->m_deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = vulkan->m_deviceExtensions.data();
    if (const VkResult result = Vk().vkCreateDevice(vulkan->m_physicalDevice, &deviceInfo, nullptr, &vulkan->m_device);
        result != VK_SUCCESS) {
        Log::GetEngineLogger().Error("Failed to create a Vulkan device on {}: {}", vulkan->m_adapterName,
                                     ToString(result));
        return nullptr;
    }
    Vk().init(vk::Device(vulkan->m_device));
    Vk().vkGetDeviceQueue(vulkan->m_device, vulkan->m_queueFamily, 0, &vulkan->m_queue);
    return vulkan;
}

VulkanSurface::~VulkanSurface() {
    if (m_device) {
        Vk().vkDeviceWaitIdle(m_device);
    }
    ReleaseSwapchain();
    for (VkSemaphore semaphore : m_acquireSemaphores) {
        Vk().vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : m_presentSemaphores) {
        Vk().vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    if (m_swapchain) {
        Vk().vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    }
    if (m_device) {
        Vk().vkDestroyDevice(m_device, nullptr);
    }
    if (m_surface) {
        Vk().vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_messenger) {
        Vk().vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
    }
    if (m_instance) {
        Vk().vkDestroyInstance(m_instance, nullptr);
    }
    UnloadVulkan(m_library);
    Platform::Shutdown();
}

nvrhi::vulkan::DeviceHandle VulkanSurface::CreateNvrhiDevice() {
    nvrhi::vulkan::DeviceDesc desc;
    desc.errorCB = &s_messageLog;
    desc.instance = m_instance;
    desc.physicalDevice = m_physicalDevice;
    desc.device = m_device;
    desc.graphicsQueue = m_queue;
    desc.graphicsQueueIndex = static_cast<int>(m_queueFamily);
    desc.instanceExtensions = m_instanceExtensions.data();
    desc.numInstanceExtensions = m_instanceExtensions.size();
    desc.deviceExtensions = m_deviceExtensions.data();
    desc.numDeviceExtensions = m_deviceExtensions.size();
    desc.bufferDeviceAddressSupported = m_bufferDeviceAddress;
    nvrhi::vulkan::DeviceHandle device = nvrhi::vulkan::createDevice(desc);
    if (!device) {
        Log::GetEngineLogger().Error("NVRHI could not run on the Vulkan device of {}", m_adapterName);
    }
    return device;
}

void VulkanSurface::SetVSync(bool enabled) {
    if (enabled != m_vsync) {
        m_vsync = enabled;
        m_recreate = true;
    }
}

void VulkanSurface::ReleaseSwapchain() {
    m_images.clear();
}

bool VulkanSurface::CreateSwapchain(nvrhi::IDevice& device) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    if (width <= 0 || height <= 0) {
        return false;
    }
    device.waitForIdle();
    m_images.clear();

    VkSurfaceCapabilitiesKHR capabilities{};
    Vk().vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);
    std::uint32_t formatCount = 0;
    Vk().vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    Vk().vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
    std::uint32_t modeCount = 0;
    Vk().vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    Vk().vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, modes.data());

    // sRGB frames encode the linear colors shaders write for display, unless the window was asked to show them as
    // they are.
    const std::initializer_list<VkFormat> wanted =
        m_linearWindow ? std::initializer_list<VkFormat>{VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB}
                       : std::initializer_list<VkFormat>{VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    VkSurfaceFormatKHR chosen = formats.front();
    for (const VkFormat format : wanted) {
        const auto found = std::ranges::find_if(formats, [format](const VkSurfaceFormatKHR& candidate) {
            return candidate.format == format && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (found != formats.end()) {
            chosen = *found;
            break;
        }
    }
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_vsync) {
        for (const VkPresentModeKHR mode : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::ranges::find(modes, mode) != modes.end()) {
                presentMode = mode;
                break;
            }
        }
    }

    m_extent = capabilities.currentExtent.width != UINT32_MAX
                   ? capabilities.currentExtent
                   : VkExtent2D{std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width,
                                           capabilities.maxImageExtent.width),
                                std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height,
                                           capabilities.maxImageExtent.height)};
    if (m_extent.width == 0 || m_extent.height == 0) {
        return false;
    }
    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = m_surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = m_extent;
    info.imageArrayLayers = 1;
    info.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = m_swapchain;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    if (const VkResult result = Vk().vkCreateSwapchainKHR(m_device, &info, nullptr, &swapchain); result != VK_SUCCESS) {
        Log::GetEngineLogger().Error("Failed to create the swapchain: {}", ToString(result));
        return false;
    }
    if (m_swapchain) {
        Vk().vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    }
    m_swapchain = swapchain;
    m_windowFormat = FromVulkan(chosen.format);

    std::uint32_t count = 0;
    Vk().vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr);
    std::vector<VkImage> images(count);
    Vk().vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, images.data());
    for (VkImage image : images) {
        nvrhi::TextureDesc desc;
        desc.width = m_extent.width;
        desc.height = m_extent.height;
        desc.format = ToNvrhiFormat(chosen.format);
        desc.isRenderTarget = true;
        desc.initialState = nvrhi::ResourceStates::Present;
        desc.keepInitialState = true;
        desc.debugName = "Swapchain image";
        m_images.push_back(
            device.createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), desc));
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    while (m_acquireSemaphores.size() < count + 1) {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        Vk().vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore);
        m_acquireSemaphores.push_back(semaphore);
    }
    while (m_presentSemaphores.size() < count) {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        Vk().vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore);
        m_presentSemaphores.push_back(semaphore);
    }
    m_recreate = false;
    return true;
}

nvrhi::ITexture* VulkanSurface::Acquire(nvrhi::vulkan::IDevice& device) {
    if (!m_window) {
        return nullptr;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    if (static_cast<std::uint32_t>(width) != m_extent.width || static_cast<std::uint32_t>(height) != m_extent.height) {
        m_recreate = true;
    }
    // Twice at most: a swapchain the window outgrew is made again, and acquiring tried once more.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (m_recreate && !CreateSwapchain(device)) {
            return nullptr;
        }
        const VkSemaphore semaphore = m_acquireSemaphores[m_acquireSlot];
        const VkResult result =
            Vk().vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, semaphore, VK_NULL_HANDLE, &m_imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            m_recreate = true;
            continue;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            Log::GetEngineLogger().Error("Failed to acquire a swapchain image: {}", ToString(result));
            return nullptr;
        }
        m_acquireSlot = (m_acquireSlot + 1) % m_acquireSemaphores.size();
        device.queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
        return m_images[m_imageIndex];
    }
    return nullptr;
}

void VulkanSurface::PrepareToPresent(nvrhi::vulkan::IDevice& device) {
    device.queueSignalSemaphore(nvrhi::CommandQueue::Graphics, m_presentSemaphores[m_imageIndex], 0);
}

void VulkanSurface::Present() {
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &m_presentSemaphores[m_imageIndex];
    info.swapchainCount = 1;
    info.pSwapchains = &m_swapchain;
    info.pImageIndices = &m_imageIndex;
    const VkResult result = Vk().vkQueuePresentKHR(m_queue, &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_recreate = true;
    } else if (result != VK_SUCCESS) {
        Log::GetEngineLogger().Error("Failed to present the frame: {}", ToString(result));
    }
}

} // namespace Spot::Detail
