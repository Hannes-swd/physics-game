#include "Renderer.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Png.h"

namespace {

const uint32_t kMeshVert[] =
#include "mesh.vert.inc"
    ;
const uint32_t kMeshFrag[] =
#include "mesh.frag.inc"
    ;
const uint32_t kShadowVert[] =
#include "shadow.vert.inc"
    ;
const uint32_t kLineVert[] =
#include "line.vert.inc"
    ;
const uint32_t kLineFrag[] =
#include "line.frag.inc"
    ;
const uint32_t kSkyVert[] =
#include "sky.vert.inc"
    ;
const uint32_t kSkyFrag[] =
#include "sky.frag.inc"
    ;

struct Push {
    glm::mat4 viewProj;
    glm::vec4 camPos;
    glm::vec4 params;
};

struct FrameUbo {
    glm::mat4 lightViewProj;
    glm::vec4 sunDir;
    glm::vec4 sunColor;
    glm::vec4 skyColor;
    glm::vec4 groundColor;
    glm::vec4 misc;
    glm::vec4 originMod;
    glm::vec4 originF;
};

#define VK_CHECK(x)                                                                     \
    do {                                                                                \
        VkResult r_ = (x);                                                              \
        if (r_ != VK_SUCCESS) std::fprintf(stderr, "Vulkan-Fehler %d in %s\n", r_, #x); \
    } while (0)

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
    return VK_FALSE;
}

void imageBarrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &di);
}

} // namespace

// ============================================================================ Init

bool Renderer::init(GLFWwindow* window, bool validation) {
    window_ = window;
    if (!createInstance(validation)) return false;
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
        std::fprintf(stderr, "Fensteroberfläche konnte nicht erstellt werden\n");
        return false;
    }
    if (!pickDevice() || !createDevice()) return false;

    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = queueFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &cpi, nullptr, &cmdPool_));

    if (!createSwapchain()) return false;

    // Schattenkarte
    shadow_ = createImage(kShadowSize, kShadowSize, depthFormat_,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    si.compareEnable = VK_TRUE;
    si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    si.maxLod = 0.0f;
    VK_CHECK(vkCreateSampler(device_, &si, nullptr, &shadowSampler_));

    // Deskriptoren
    VkDescriptorSetLayoutBinding binds[3] = {};
    binds[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    binds[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    binds[2] = {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 3;
    dli.pBindings = binds;
    VK_CHECK(vkCreateDescriptorSetLayout(device_, &dli, nullptr, &setLayout_));

    VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFrames},
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFrames},
                                     {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFrames}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = kFrames;
    dpi.poolSizeCount = 3;
    dpi.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(device_, &dpi, nullptr, &descPool_));

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(device_, &pli, nullptr, &pipeLayout_));

    for (Frame& f : frames_) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = cmdPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &f.cmd));
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &f.imageAvailable));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(device_, &fci, nullptr, &f.fence));

        f.objects = createBuffer(sizeof(ObjectGpuData) * kMaxObjects, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        f.ubo = createBuffer(sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        f.vertices = createBuffer(256 * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);

        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descPool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &setLayout_;
        VK_CHECK(vkAllocateDescriptorSets(device_, &dai, &f.set));

        VkDescriptorBufferInfo objInfo{f.objects.buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo shInfo{shadowSampler_, shadow_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo uboInfo{f.ubo.buffer, 0, sizeof(FrameUbo)};
        VkWriteDescriptorSet w[3] = {};
        for (int i = 0; i < 3; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = f.set;
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[0].pBufferInfo = &objInfo;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].pImageInfo = &shInfo;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[2].pBufferInfo = &uboInfo;
        vkUpdateDescriptorSets(device_, 3, w, 0, nullptr);
    }

    return createPipelines();
}

bool Renderer::createInstance(bool validation) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Werkstatt";
    app.pEngineName = "Werkstatt-Engine";
    app.apiVersion = VK_API_VERSION_1_3;

    uint32_t extCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&extCount);
    std::vector<const char*> exts(glfwExt, glfwExt + extCount);
    std::vector<const char*> layers;
    if (validation) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> props(n);
        vkEnumerateInstanceLayerProperties(&n, props.data());
        bool found = false;
        for (auto& p : props)
            if (std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0) found = true;
        if (found) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else {
            std::fprintf(stderr, "Validierungsschicht nicht gefunden\n");
            validation = false;
        }
    }

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = (uint32_t)layers.size();
    ci.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan-Instanz konnte nicht erstellt werden\n");
        return false;
    }
    if (validation) {
        VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = debugCallback;
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(instance_, &mi, nullptr, &messenger_);
    }
    return true;
}

bool Renderer::pickDevice() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance_, &n, devs.data());
    int bestScore = -1;
    for (VkPhysicalDevice d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;

        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &f13;
        vkGetPhysicalDeviceFeatures2(d, &f2);
        if (!f13.dynamicRendering || !f13.synchronization2) continue;

        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qs.data());
        int family = -1;
        for (uint32_t i = 0; i < qn; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
            if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                family = (int)i;
                break;
            }
        }
        if (family < 0) continue;
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
        if (score > bestScore) {
            bestScore = score;
            gpu_ = d;
            queueFamily_ = (uint32_t)family;
            gpuName_ = p.deviceName;
            depthClamp_ = f2.features.depthClamp == VK_TRUE;
            VkSampleCountFlags counts = p.limits.framebufferColorSampleCounts & p.limits.framebufferDepthSampleCounts;
            samples_ = (counts & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
        }
    }
    if (!gpu_) {
        std::fprintf(stderr, "Keine Grafikkarte mit Vulkan 1.3 gefunden\n");
        return false;
    }
    std::printf("Grafikkarte: %s\n", gpuName_.c_str());
    return true;
}

bool Renderer::createDevice() {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = queueFamily_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    f2.features.depthClamp = depthClamp_ ? VK_TRUE : VK_FALSE;

    const char* exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = &f2;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = exts;
    if (vkCreateDevice(gpu_, &ci, nullptr, &device_) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan-Gerät konnte nicht erstellt werden\n");
        return false;
    }
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return true;
}

bool Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps);

    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (auto& f : formats)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    swapFormat_ = chosen.format;

    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        swapExtent_ = caps.currentExtent;
    } else {
        swapExtent_.width = std::clamp((uint32_t)w, caps.minImageExtent.width, caps.maxImageExtent.width);
        swapExtent_.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (swapExtent_.width == 0 || swapExtent_.height == 0) return true;  // minimiert

    uint32_t count = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount > 0) count = std::min(count, caps.maxImageCount);
    canCopySwapchain_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = count;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = swapExtent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (canCopySwapchain_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    VkSwapchainKHR old = swapchain_;
    ci.oldSwapchain = old;
    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS) {
        std::fprintf(stderr, "Swapchain konnte nicht erstellt werden\n");
        return false;
    }
    if (old) vkDestroySwapchainKHR(device_, old, nullptr);

    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    swapImages_.resize(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapImages_.data());
    swapViews_.resize(n);
    renderFinished_.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = swapImages_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swapFormat_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &swapViews_[i]));
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &renderFinished_[i]));
    }

    msaaColor_ = createImage(swapExtent_.width, swapExtent_.height, swapFormat_,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, samples_,
                             VK_IMAGE_ASPECT_COLOR_BIT);
    depth_ = createImage(swapExtent_.width, swapExtent_.height, depthFormat_,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, samples_,
                         VK_IMAGE_ASPECT_DEPTH_BIT);
    return true;
}

void Renderer::destroySwapchain() {
    destroyImage(msaaColor_);
    destroyImage(depth_);
    for (auto v : swapViews_) vkDestroyImageView(device_, v, nullptr);
    for (auto s : renderFinished_) vkDestroySemaphore(device_, s, nullptr);
    swapViews_.clear();
    renderFinished_.clear();
    swapImages_.clear();
}

void Renderer::recreateSwapchain() {
    vkDeviceWaitIdle(device_);
    destroySwapchain();
    createSwapchain();
    if (imgui_ && !swapImages_.empty()) ImGui_ImplVulkan_SetMinImageCount(2);
}

VkShaderModule Renderer::shaderModule(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device_, &ci, nullptr, &m));
    return m;
}

bool Renderer::createPipelines() {
    VkShaderModule meshV = shaderModule(kMeshVert, sizeof(kMeshVert));
    VkShaderModule meshF = shaderModule(kMeshFrag, sizeof(kMeshFrag));
    VkShaderModule shadowV = shaderModule(kShadowVert, sizeof(kShadowVert));
    VkShaderModule lineV = shaderModule(kLineVert, sizeof(kLineVert));
    VkShaderModule lineF = shaderModule(kLineFrag, sizeof(kLineFrag));
    VkShaderModule skyV = shaderModule(kSkyVert, sizeof(kSkyVert));
    VkShaderModule skyF = shaderModule(kSkyFrag, sizeof(kSkyFrag));

    struct Cfg {
        VkShaderModule vs, fs;
        bool meshInput, lineInput;
        VkPrimitiveTopology topo;
        VkCullModeFlags cull;
        bool depthTest, depthWrite, blend, shadowPass;
        VkPipeline* out;
    };
    const Cfg cfgs[] = {
        {meshV, meshF, true, false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_BACK_BIT, true, true, false, false, &meshPipe_},
        {shadowV, VK_NULL_HANDLE, true, false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE, true, true, false, true, &shadowPipe_},
        {skyV, skyF, false, false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE, false, false, false, false, &skyPipe_},
        {lineV, lineF, false, true, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_CULL_MODE_NONE, true, false, true, false, &linePipe_},
        {lineV, lineF, false, true, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE, true, false, true, false, &transPipe_},
    };

    for (const Cfg& c : cfgs) {
        VkPipelineShaderStageCreateInfo stages[2] = {};
        uint32_t stageCount = 1;
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = c.vs;
        stages[0].pName = "main";
        if (c.fs) {
            stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = c.fs;
            stages[1].pName = "main";
            stageCount = 2;
        }

        VkVertexInputBindingDescription bind{};
        VkVertexInputAttributeDescription attrs[2] = {};
        VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        if (c.meshInput) {
            bind = {0, sizeof(solid::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
            attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
            vin.vertexBindingDescriptionCount = 1;
            vin.pVertexBindingDescriptions = &bind;
            vin.vertexAttributeDescriptionCount = c.shadowPass ? 1 : 2;
            vin.pVertexAttributeDescriptions = attrs;
        } else if (c.lineInput) {
            bind = {0, sizeof(LineVertex), VK_VERTEX_INPUT_RATE_VERTEX};
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
            attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12};
            vin.vertexBindingDescriptionCount = 1;
            vin.pVertexBindingDescriptions = &bind;
            vin.vertexAttributeDescriptionCount = 2;
            vin.pVertexAttributeDescriptions = attrs;
        }

        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = c.topo;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = c.cull;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        if (c.shadowPass) {
            rs.depthBiasEnable = VK_TRUE;
            rs.depthBiasConstantFactor = 1.5f;
            rs.depthBiasSlopeFactor = 2.0f;
            rs.depthClampEnable = depthClamp_ ? VK_TRUE : VK_FALSE;
        }
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = c.shadowPass ? VK_SAMPLE_COUNT_1_BIT : samples_;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = c.depthTest;
        ds.depthWriteEnable = c.depthWrite;
        ds.depthCompareOp = c.shadowPass ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_GREATER_OR_EQUAL;
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        if (c.blend) {
            cba.blendEnable = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.colorBlendOp = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = c.shadowPass ? 0 : 1;
        cb.pAttachments = &cba;

        std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        if (c.lineInput) dyn.push_back(VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE);
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = (uint32_t)dyn.size();
        dy.pDynamicStates = dyn.data();

        VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        ri.colorAttachmentCount = c.shadowPass ? 0 : 1;
        ri.pColorAttachmentFormats = &swapFormat_;
        ri.depthAttachmentFormat = depthFormat_;

        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.pNext = &ri;
        pi.stageCount = stageCount;
        pi.pStages = stages;
        pi.pVertexInputState = &vin;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &ds;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &dy;
        pi.layout = pipeLayout_;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, c.out) != VK_SUCCESS) {
            std::fprintf(stderr, "Pipeline konnte nicht erstellt werden\n");
            return false;
        }
    }
    for (VkShaderModule m : {meshV, meshF, shadowV, lineV, lineF, skyV, skyF}) vkDestroyShaderModule(device_, m, nullptr);
    return true;
}

void Renderer::initImGui() {
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpi.maxSets = 16;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VK_CHECK(vkCreateDescriptorPool(device_, &dpi, nullptr, &imguiPool_));

    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_3;
    ii.Instance = instance_;
    ii.PhysicalDevice = gpu_;
    ii.Device = device_;
    ii.QueueFamily = queueFamily_;
    ii.Queue = queue_;
    ii.DescriptorPool = imguiPool_;
    ii.MinImageCount = 2;
    ii.ImageCount = std::max<uint32_t>(2, (uint32_t)swapImages_.size());
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.UseDynamicRendering = true;
    ii.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapFormat_;
    ImGui_ImplVulkan_Init(&ii);
    imgui_ = true;
}

// ============================================================================ Ressourcen

uint32_t Renderer::memoryType(uint32_t bits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return 0;
}

Renderer::Buffer Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device_, &ci, nullptr, &b.buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, b.buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, hostVisible
                                                            ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                            : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &b.memory));
    VK_CHECK(vkBindBufferMemory(device_, b.buffer, b.memory, 0));
    if (hostVisible) VK_CHECK(vkMapMemory(device_, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped));
    return b;
}

void Renderer::destroyBuffer(Buffer& b) {
    if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
    if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
    b = Buffer{};
}

Renderer::Image Renderer::createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                                      VkSampleCountFlagBits samples, VkImageAspectFlags aspect) {
    Image img;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = samples;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device_, &ci, nullptr, &img.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, img.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &img.memory));
    VK_CHECK(vkBindImageMemory(device_, img.image, img.memory, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {aspect, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &img.view));
    return img;
}

void Renderer::destroyImage(Image& i) {
    if (i.view) vkDestroyImageView(device_, i.view, nullptr);
    if (i.image) vkDestroyImage(device_, i.image, nullptr);
    if (i.memory) vkFreeMemory(device_, i.memory, nullptr);
    i = Image{};
}

GpuMesh Renderer::createMesh(const std::vector<solid::Vertex>& v) {
    GpuMesh m;
    if (v.empty()) return m;
    Buffer b = createBuffer(v.size() * sizeof(solid::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    std::memcpy(b.mapped, v.data(), v.size() * sizeof(solid::Vertex));
    vkUnmapMemory(device_, b.memory);
    m.buffer = b.buffer;
    m.memory = b.memory;
    m.vertexCount = (uint32_t)v.size();
    return m;
}

void Renderer::destroyMesh(GpuMesh& m) {
    if (m.buffer) pending_.push_back({m, frameCounter_});
    m = GpuMesh{};
}

void Renderer::waitIdle() {
    if (device_) vkDeviceWaitIdle(device_);
}

// ============================================================================ Zeichnen

void Renderer::render(const FrameDesc& fd) {
    Frame& fr = frames_[frameIndex_];
    vkWaitForFences(device_, 1, &fr.fence, VK_TRUE, UINT64_MAX);

    // Netze freigeben, die sicher nicht mehr in Benutzung sind
    for (size_t i = 0; i < pending_.size();) {
        if (pending_[i].frame + kFrames < frameCounter_) {
            vkDestroyBuffer(device_, pending_[i].mesh.buffer, nullptr);
            vkFreeMemory(device_, pending_[i].mesh.memory, nullptr);
            pending_[i] = pending_.back();
            pending_.pop_back();
        } else {
            ++i;
        }
    }

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window_, &fbw, &fbh);
    if (fbw == 0 || fbh == 0) return;
    if (swapImages_.empty() || (uint32_t)fbw != swapExtent_.width || (uint32_t)fbh != swapExtent_.height) {
        recreateSwapchain();
        if (swapImages_.empty()) return;
    }

    uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    vkResetFences(device_, 1, &fr.fence);

    // --- Daten hochladen
    const uint32_t objCount = (uint32_t)std::min<size_t>(fd.items.size(), kMaxObjects);
    auto* od = static_cast<ObjectGpuData*>(fr.objects.mapped);
    for (uint32_t i = 0; i < objCount; ++i) od[i] = fd.items[i].data;

    FrameUbo ubo;
    ubo.lightViewProj = fd.lightViewProj;
    ubo.sunDir = glm::vec4(glm::normalize(fd.sunDir), 0.0f);
    ubo.sunColor = glm::vec4(3.4f, 3.15f, 2.9f, 0.0f);
    ubo.skyColor = glm::vec4(0.36f, 0.45f, 0.62f, 0.0f);
    ubo.groundColor = glm::vec4(0.20f, 0.18f, 0.16f, 0.0f);
    ubo.misc = glm::vec4(fd.shadowTexelWorld, 1.0f, fd.fogDensity, fd.shadows ? 1.0f : 0.0f);
    ubo.originMod = glm::vec4(std::fmod(fd.origin.x, 200.0), std::fmod(fd.origin.y, 200.0), std::fmod(fd.origin.z, 200.0), 0.0f);
    ubo.originF = glm::vec4(glm::vec3(fd.origin), 0.0f);
    std::memcpy(fr.ubo.mapped, &ubo, sizeof(ubo));

    const size_t nLines = fd.lines.size(), nOverlay = fd.overlayLines.size(), nTrans = fd.translucent.size();
    const size_t vbBytes = (nLines + nOverlay + nTrans) * sizeof(LineVertex);
    if (vbBytes > fr.vertices.size) {
        destroyBuffer(fr.vertices);
        fr.vertices = createBuffer(std::max<VkDeviceSize>(vbBytes * 2, 256 * 1024), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    }
    auto* lv = static_cast<LineVertex*>(fr.vertices.mapped);
    if (nLines) std::memcpy(lv, fd.lines.data(), nLines * sizeof(LineVertex));
    if (nOverlay) std::memcpy(lv + nLines, fd.overlayLines.data(), nOverlay * sizeof(LineVertex));
    if (nTrans) std::memcpy(lv + nLines + nOverlay, fd.translucent.data(), nTrans * sizeof(LineVertex));

    VkCommandBuffer cmd = fr.cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    const VkShaderStageFlags pcStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &fr.set, 0, nullptr);

    // --- Schattenpass
    imageBarrier(cmd, shadow_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    {
        VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        da.imageView = shadow_.view;
        da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        da.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, {kShadowSize, kShadowSize}};
        ri.layerCount = 1;
        ri.pDepthAttachment = &da;
        vkCmdBeginRendering(cmd, &ri);
        if (fd.shadows) {
            VkViewport v{0, 0, (float)kShadowSize, (float)kShadowSize, 0, 1};
            VkRect2D sc{{0, 0}, {kShadowSize, kShadowSize}};
            vkCmdSetViewport(cmd, 0, 1, &v);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipe_);
            Push p{fd.lightViewProj, glm::vec4(0), glm::vec4(0)};
            vkCmdPushConstants(cmd, pipeLayout_, pcStages, 0, sizeof(Push), &p);
            for (uint32_t i = 0; i < objCount; ++i) {
                const DrawItem& it = fd.items[i];
                if (!it.castShadow || !it.mesh || !it.mesh->valid()) continue;
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &it.mesh->buffer, &off);
                vkCmdDraw(cmd, it.mesh->vertexCount, 1, 0, i);
            }
        }
        vkCmdEndRendering(cmd);
    }
    imageBarrier(cmd, shadow_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // --- Hauptpass (MSAA, wird ins Swapchain-Bild aufgelöst)
    VkImage swapImg = swapImages_[imageIndex];
    imageBarrier(cmd, swapImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    imageBarrier(cmd, msaaColor_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
    imageBarrier(cmd, depth_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    {
        VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        ca.imageView = samples_ == VK_SAMPLE_COUNT_1_BIT ? swapViews_[imageIndex] : msaaColor_.view;
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = samples_ == VK_SAMPLE_COUNT_1_BIT ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ca.clearValue.color = {{0.08f, 0.08f, 0.09f, 1.0f}};
        if (samples_ != VK_SAMPLE_COUNT_1_BIT) {
            ca.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            ca.resolveImageView = swapViews_[imageIndex];
            ca.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        da.imageView = depth_.view;
        da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        da.clearValue.depthStencil = {0.0f, 0};  // Reversed-Z
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, swapExtent_};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &ca;
        ri.pDepthAttachment = &da;
        vkCmdBeginRendering(cmd, &ri);

        for (const ViewDesc& v : fd.views) {
            if (v.w <= 0 || v.h <= 0) continue;
            int x0 = std::clamp(v.x, 0, (int)swapExtent_.width), y0 = std::clamp(v.y, 0, (int)swapExtent_.height);
            int x1 = std::clamp(v.x + v.w, 0, (int)swapExtent_.width), y1 = std::clamp(v.y + v.h, 0, (int)swapExtent_.height);
            if (x1 <= x0 || y1 <= y0) continue;
            VkViewport vp{(float)v.x, (float)v.y, (float)v.w, (float)v.h, 0.0f, 1.0f};
            VkRect2D sc{{x0, y0}, {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0)}};
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);

            Push p;
            p.viewProj = v.viewProj;
            p.camPos = glm::vec4(v.camPos, v.ortho ? 1.0f : 0.0f);
            p.params = glm::vec4(fd.grid ? 1.0f : 0.0f, fd.time, fd.buildMode ? 1.0f : 0.0f, 0.0f);
            vkCmdPushConstants(cmd, pipeLayout_, pcStages, 0, sizeof(Push), &p);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipe_);
            vkCmdDraw(cmd, 3, 1, 0, 0);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipe_);
            for (uint32_t i = 0; i < objCount; ++i) {
                const DrawItem& it = fd.items[i];
                if (!it.mesh || !it.mesh->valid()) continue;
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &it.mesh->buffer, &off);
                vkCmdDraw(cmd, it.mesh->vertexCount, 1, 0, i);
            }

            VkDeviceSize off = 0;
            if (nLines + nOverlay + nTrans > 0) vkCmdBindVertexBuffers(cmd, 0, 1, &fr.vertices.buffer, &off);
            if (nTrans) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transPipe_);
                vkCmdSetDepthTestEnable(cmd, VK_TRUE);
                vkCmdDraw(cmd, (uint32_t)nTrans, 1, (uint32_t)(nLines + nOverlay), 0);
            }
            if (nLines || nOverlay) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipe_);
                glm::vec4 params(p.params.x, p.params.y, p.params.z, 1.0f);
                vkCmdPushConstants(cmd, pipeLayout_, pcStages, offsetof(Push, params), sizeof(glm::vec4), &params);
                if (nLines) {
                    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
                    vkCmdDraw(cmd, (uint32_t)nLines, 1, 0, 0);
                }
                if (nOverlay) {
                    vkCmdSetDepthTestEnable(cmd, VK_FALSE);
                    vkCmdDraw(cmd, (uint32_t)nOverlay, 1, (uint32_t)nLines, 0);
                }
            }
        }
        vkCmdEndRendering(cmd);
    }

    // --- ImGui direkt ins Swapchain-Bild
    imageBarrier(cmd, swapImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
    if (imgui_) {
        VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        ca.imageView = swapViews_[imageIndex];
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, swapExtent_};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &ca;
        vkCmdBeginRendering(cmd, &ri);
        if (ImDrawData* dd = ImGui::GetDrawData()) ImGui_ImplVulkan_RenderDrawData(dd, cmd);
        vkCmdEndRendering(cmd);
    }

    // --- Bildschirmfoto
    Buffer shot;
    const bool wantShot = !fd.screenshotPath.empty() && canCopySwapchain_;
    if (wantShot) {
        shot = createBuffer((VkDeviceSize)swapExtent_.width * swapExtent_.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        imageBarrier(cmd, swapImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {swapExtent_.width, swapExtent_.height, 1};
        vkCmdCopyImageToBuffer(cmd, swapImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shot.buffer, 1, &region);
        imageBarrier(cmd, swapImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                     VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    } else {
        imageBarrier(cmd, swapImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    }
    vkEndCommandBuffer(cmd);

    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = fr.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = renderFinished_[imageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = 1;
    si.pWaitSemaphoreInfos = &waitInfo;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cmdInfo;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos = &signalInfo;
    VK_CHECK(vkQueueSubmit2(queue_, 1, &si, fr.fence));

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished_[imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &imageIndex;
    VkResult pr = vkQueuePresentKHR(queue_, &pi);

    if (wantShot) {
        vkDeviceWaitIdle(device_);
        const uint32_t w = swapExtent_.width, h = swapExtent_.height;
        std::vector<uint8_t> rgb((size_t)w * h * 3);
        const uint8_t* src = static_cast<const uint8_t*>(shot.mapped);
        const bool bgr = swapFormat_ == VK_FORMAT_B8G8R8A8_UNORM || swapFormat_ == VK_FORMAT_B8G8R8A8_SRGB;
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            rgb[i * 3 + 0] = src[i * 4 + (bgr ? 2 : 0)];
            rgb[i * 3 + 1] = src[i * 4 + 1];
            rgb[i * 3 + 2] = src[i * 4 + (bgr ? 0 : 2)];
        }
        writePng(fd.screenshotPath, w, h, rgb);
        destroyBuffer(shot);
    }

    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) recreateSwapchain();
    frameIndex_ = (frameIndex_ + 1) % kFrames;
    ++frameCounter_;
}

void Renderer::shutdown() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    if (imgui_) {
        ImGui_ImplVulkan_Shutdown();
        imgui_ = false;
    }
    for (auto& p : pending_) {
        vkDestroyBuffer(device_, p.mesh.buffer, nullptr);
        vkFreeMemory(device_, p.mesh.memory, nullptr);
    }
    pending_.clear();
    for (Frame& f : frames_) {
        destroyBuffer(f.objects);
        destroyBuffer(f.ubo);
        destroyBuffer(f.vertices);
        vkDestroySemaphore(device_, f.imageAvailable, nullptr);
        vkDestroyFence(device_, f.fence, nullptr);
    }
    for (VkPipeline p : {meshPipe_, shadowPipe_, skyPipe_, linePipe_, transPipe_})
        if (p) vkDestroyPipeline(device_, p, nullptr);
    vkDestroyPipelineLayout(device_, pipeLayout_, nullptr);
    vkDestroyDescriptorPool(device_, descPool_, nullptr);
    if (imguiPool_) vkDestroyDescriptorPool(device_, imguiPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    vkDestroySampler(device_, shadowSampler_, nullptr);
    destroyImage(shadow_);
    destroySwapchain();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    vkDestroyCommandPool(device_, cmdPool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (messenger_) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(instance_, messenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
}
