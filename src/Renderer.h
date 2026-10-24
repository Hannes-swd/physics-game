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
    glm::vec4 params{0.0f};  // x = Shading, y = Zufall, z = Hervorhebung, w = Anlauffarben (0 keine, 1 Stahl, 2 Kupfer/Messing)
    glm::vec4 heat0{0.0f, 0.0f, 0.0f, -1.0f};  // xyz = Ecke des Temperaturgitters (lokal), w = erste Zelle in heatCells (-1: keins)
    glm::vec4 heat1{1.0f};   // xyz = Zellgröße (m), w = Zellen je Achse gepackt: x + 64·y + 4096·z
};

constexpr int kMaxLights = 8;  // Feuerschein (Punktlichter)
constexpr int kMaxWater = 8;   // Gewässer je Bild (die nächsten)
constexpr int kWaterGroundCells = 128;  // Meer: Fenster der Grundkarte um die Kamera, Zellen je Achse (1 m), wie in common.glsl

// Ein Gewässer für die Shader. Muss zu frame.water in common.glsl passen (8 vec4 je Gewässer)
struct WaterGpu {
    glm::vec4 lo{0.0f};       // xyz Ecke unten (relativ zum Ursprung), w = Wellenlänge der größten Welle (m)
    glm::vec4 hi{0.0f};       // xyz Ecke oben (y = Wasserspiegel in Ruhe), w = Wellenhöhe (m)
    glm::vec4 phase{0.0f};    // Phase der vier Wellen am Ursprung (rad)
    glm::vec4 absorb{0.0f};   // rgb Lichtabsorption je m, w = 1 Meer (unbegrenzt)
    glm::vec4 scatter{0.0f};  // rgb Streufarbe, w = Rastergröße der Oberfläche (Vierecke je Seite)
    glm::vec4 grid0{0.0f, 0.0f, 1.0f, -1.0f};  // Strömungsgitter: xy = Ecke (x, z relativ zum Ursprung), z = Zellgröße, w = erste Zelle (-1: keins)
    glm::vec4 grid1{0.0f};    // xy = Zellen je Achse (x, z), zw = Strömung ohne Hindernisse (m/s)
    // Meer: Grund im Fenster (kWaterGroundCells² Zellen, je vec4 vier Höhen in Weltkoordinaten), sonst gilt lo.y.
    // xy = Ecke (x, z relativ zum Ursprung), z = zu den Höhen addieren (−Ursprung y), w = erster vec4 (-1: überall lo.y)
    glm::vec4 ground{0.0f, 0.0f, 0.0f, -1.0f};
};

struct DrawItem {
    const GpuMesh* mesh = nullptr;
    ObjectGpuData data;
    bool castShadow = true;
    // ohne mesh: Dreiecke aus FrameDesc::frameVertices (jedes Bild neu, z.B. animierte Figuren)
    uint32_t frameFirst = 0, frameCount = 0;
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
    std::vector<solid::Vertex> frameVertices;  // Dreiecke nur für dieses Bild (DrawItem::frameFirst/frameCount)
    // Wärme: je Zelle (Temperatur °C, Höchsttemperatur, Verkohlung 0..1, Schmelze 0..1), siehe ObjectGpuData::heat0
    std::vector<glm::vec4> heatCells;
    // Flammen, Rauch, Funken: Dreiecke, color = (u, v, Stärke, Art·10 + Zufall); Art 0 Flamme, 1 Rauch, 2 Funke,
    // 3 Kältenebel, 4 Brennerflamme
    std::vector<LineVertex> fire;
    // Feuerschein: Position (relativ zum Ursprung) und Farbe·Stärke
    std::vector<glm::vec3> lightPos, lightColor;
    // Gewässer (höchstens kMaxWater) und die Richtung der Wellen
    std::vector<WaterGpu> water;
    glm::vec2 waveDir{1.0f, 0.0f};
    // Strömungsgitter aller Gewässer: je Zelle (Strömung x, z in m/s, Schaum 0..1, Stau in m), siehe WaterGpu::grid0
    std::vector<glm::vec4> waterCells;
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
        Buffer objects, ubo, vertices, meshVertices, heat, water;
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
    VkPipeline linePipe_ = VK_NULL_HANDLE, transPipe_ = VK_NULL_HANDLE, firePipe_ = VK_NULL_HANDLE, waterPipe_ = VK_NULL_HANDLE;

    static constexpr int kFrames = 2;
    static constexpr uint32_t kMaxObjects = 8192;
    Frame frames_[kFrames];
    int frameIndex_ = 0;
    uint64_t frameCounter_ = 0;
    std::vector<Pending> pending_;
    bool imgui_ = false;
};
