// Vulkan-Renderer: Schattenkarte, MSAA, mehrere Ansichten pro Bild, Linien, ImGui.
#pragma once
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "Solid.h"

struct GLFWwindow;

struct GpuMesh {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;
    bool valid() const { return buffer != VK_NULL_HANDLE && vertexCount > 0; }
};

// Muss zum std430-Layout in common.glsl passen
struct ObjectGpuData {
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};   // rgb, a = Rauheit
    glm::vec4 color2{1.0f};  // rgb, a = metallisch
    glm::vec4 params{0.0f};  // x = Shading, y = Zufall, z = Hervorhebung
};

struct DrawItem {
    const GpuMesh* mesh = nullptr;
    ObjectGpuData data;
    bool castShadow = true;
};

struct LineVertex {
    glm::vec3 pos;
    glm::vec4 color;
};

struct ViewDesc {
    glm::mat4 viewProj{1.0f};
    glm::vec3 camPos{0.0f};
    bool ortho = false;
    int x = 0, y = 0, w = 1, h = 1;  // Pixelrechteck im Fenster
};

struct FrameDesc {
    std::vector<DrawItem> items;
    std::vector<ViewDesc> views;
    std::vector<LineVertex> lines;         // mit Tiefentest (Kanten, Raster)
    std::vector<LineVertex> overlayLines;  // immer sichtbar (Auswahl, Hilfslinien)
    std::vector<LineVertex> translucent;   // durchsichtige Dreiecke (Vorschau Säge/Bohrer)
    glm::dvec3 origin{0.0};  // Darstellungsursprung: alle Positionen im Bild sind relativ dazu
    float fogDensity = 0.00035f;
    glm::mat4 lightViewProj{1.0f};
    float shadowTexelWorld = 0.01f;
    glm::vec3 sunDir{0.4f, 1.0f, 0.3f};
    bool shadows = true;
    bool grid = true;
    bool buildMode = true;
    float time = 0.0f;
    std::string screenshotPath;  // nicht leer: dieses Bild als PNG speichern
};

class Renderer {
public:
    bool init(GLFWwindow* window, bool validation);
    void shutdown();

    GpuMesh createMesh(const std::vector<solid::Vertex>& vertices);
    void destroyMesh(GpuMesh& mesh);  // verzögert, bis die GPU fertig ist

    void initImGui();
    void render(const FrameDesc& f);  // ImGui::Render() muss vorher aufgerufen sein
    void waitIdle();

    int width() const { return (int)swapExtent_.width; }
    int height() const { return (int)swapExtent_.height; }
    const char* gpuName() const { return gpuName_.c_str(); }

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
    };
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct Frame {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        Buffer objects, ubo, vertices;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    struct Pending {
        GpuMesh mesh;
        uint64_t frame;
    };

    bool createInstance(bool validation);
    bool pickDevice();
    bool createDevice();
    bool createSwapchain();
    void destroySwapchain();
    void recreateSwapchain();
    bool createPipelines();
    VkShaderModule shaderModule(const uint32_t* code, size_t bytes);
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void destroyBuffer(Buffer& b);
    Image createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, VkSampleCountFlagBits samples,
                      VkImageAspectFlags aspect);
    void destroyImage(Image& i);
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags props);

    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    std::string gpuName_;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    bool depthClamp_ = false;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapExtent_{};
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;
    std::vector<VkSemaphore> renderFinished_;
    bool canCopySwapchain_ = false;
    Image msaaColor_, depth_;
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_4_BIT;
    const VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;

    Image shadow_;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    static constexpr uint32_t kShadowSize = 4096;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorPool imguiPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE;
    VkPipeline meshPipe_ = VK_NULL_HANDLE, shadowPipe_ = VK_NULL_HANDLE, skyPipe_ = VK_NULL_HANDLE;
    VkPipeline linePipe_ = VK_NULL_HANDLE, transPipe_ = VK_NULL_HANDLE;

    static constexpr int kFrames = 2;
    static constexpr uint32_t kMaxObjects = 8192;
    Frame frames_[kFrames];
    int frameIndex_ = 0;
    uint64_t frameCounter_ = 0;
    std::vector<Pending> pending_;
    bool imgui_ = false;
};
