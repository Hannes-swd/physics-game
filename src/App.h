// Das Spiel mit Fenster: Kameras, Eingabe, Werkzeuge, Spielfigur, Menüs.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "Commands.h"
#include "Drawing.h"
#include "Physics.h"
#include "Remote.h"
#include "Renderer.h"
#include "Script.h"
#include "World.h"

struct GLFWwindow;

enum class ViewMode { Perspective = 0, Top = 1, Front = 2, Side = 3, Quad = 4 };
enum class Tool { Select = 0, Saw = 1, Drill = 2, Round = 3, Mill = 4, Plane = 5 };

struct AppOptions {
    bool validation = false;
    std::string script;           // Befehle beim Start ausführen
    std::string screenshotPath;   // nach N Bildern Foto speichern und beenden
    int screenshotFrames = 30;
    int port = 7777;
    std::string view;             // Startansicht
    bool play = false;
    bool emptyScene = false;
};

class App : public CommandHost {
public:
    App();
    ~App() override;
    int run(const AppOptions& opt);

    // CommandHost
    World& world() override { return *world_; }
    glm::dvec3 spawnReference() override;
    uint32_t selectedId() const override { return selection_.empty() ? 0 : selection_.back(); }
    std::vector<uint32_t> selectedIds() const override { return selection_; }
    void select(uint32_t id) override;
    void selectMany(const std::vector<uint32_t>& ids) override { selection_ = ids; }
    void showDrawing(const std::vector<uint32_t>& ids) override;
    void showInspector(uint32_t id) override {
        showInspector_ = true;
        inspectTarget_ = id;
    }
    bool hasWindow() const override { return true; }
    std::string setMode(bool play) override;
    std::string setView(const std::string& v) override;
    std::string screenshot(const std::string& path) override;
    std::string cameraLook(const glm::dvec3& target, double dist) override;
    void requestQuit() override { quit_ = true; }
    bool appCommand(const std::string& cmd, const std::vector<std::string>& args, std::string& out) override;
    std::string statusText() override;
    bool queryValue(const std::string& name, double& num, std::string& str) override;
    std::string wait(double seconds) override;

private:
    struct View {
        ViewMode mode = ViewMode::Perspective;
        int x = 0, y = 0, w = 1, h = 1;
        bool ortho = false;
        glm::dvec3 eye{0}, target{0}, up{0, 1, 0};
        double halfHeight = 1;      // nur orthografisch
        glm::dmat4 viewProjRel{1};  // relativ zum Darstellungsursprung
    };
    struct Preview {
        bool valid = false;
        uint32_t objId = 0;
        solid::Plane plane;           // Säge (lokal)
        double distFromEnd = 0;
        glm::dvec3 from{0}, dir{0};   // Bohrer (lokal)
        double length = 0;
        int edge = -1;                // Abrunden: Kante unter der Maus
        int faceAxis = 1, faceSign = 1;  // Fräsen/Hobeln: Fläche
        double a = 0, b = 0;          // Fräsen: Lage auf der Fläche (m ab Mitte)
    };
    static const char* toolName(Tool t);

    // App.cpp
    bool init(const AppOptions& opt);
    void shutdown();
    void computeViews();
    View makeView(ViewMode m, int x, int y, int w, int h) const;
    bool mouseRay(const View& v, double mx, double my, glm::dvec3& o, glm::dvec3& d) const;
    int viewAt(double mx, double my) const;
    void buildFrame(FrameDesc& fd);
    void runLine(const std::string& line, const char* source);  // von Konsole/Oberfläche
    void submit(const std::string& line, int source, uint64_t client = 0);  // 0 Konsole, 1 Skript, 2 Fern
    void stepJobs();
    void toast(const std::string& text, bool ok = true);
    double snapStep() const;
    void processScriptQueue();

    // AppCommands.cpp (Live-Steuerung per Befehl)
    std::string playerCommand(const std::vector<std::string>& args);
    void lookAtPoint(const glm::dvec3& p);
    bool grabObject(uint32_t id, double dist, const glm::dvec3& localPoint);
    void throwHeld(double impulse);
    glm::dvec3 playerEye() const;
    glm::dvec3 playerForward() const;

    // AppInput.cpp
    void handleInput(float dt);
    void buildInput(float dt);
    void playInput(float dt);
    void updatePlay(float dt);
    void grabForces(float dt);
    void updatePreview();
    void applyTool();
    void enterPlay();
    void leavePlay();
    void setCursorCaptured(bool on);
    void rotateSelected(int axis, double deg);
    void deleteSelected();
    void duplicateSelected();
    void focusSelected();
    bool isSelected(uint32_t id) const;
    void toggleSelected(uint32_t id);
    glm::dvec3 selectionCenter() const;

    // AppUI.cpp
    void setupStyle();
    void drawUI();
    void drawMenuBar();
    void drawToolPanel();
    void drawProperties();
    void drawStatusBar();
    void drawConsole();
    void drawHelp();
    void drawViewLabels();
    void drawPlayHud();
    void drawToasts();
    void drawDrawingWindow();
    void drawInspector();
    void refreshDrawing();
    void log(const std::string& text, int kind = 0);  // 0 normal, 1 Befehl, 2 Fehler, 3 Info
    void openImportDialog();
    void saveDialog(bool save);

    GLFWwindow* window_ = nullptr;
    Renderer renderer_;
    Physics physics_;
    std::unique_ptr<World> world_;
    RemoteServer remote_;
    AppOptions opt_;
    bool quit_ = false;
    double time_ = 0;
    uint64_t frame_ = 0;
    float fps_ = 0;

    // Kamera
    ViewMode viewMode_ = ViewMode::Perspective;
    std::vector<View> views_;
    glm::dvec3 orbitTarget_{0, 0.6, -0.5};
    double orbitYaw_ = 0.55, orbitPitch_ = 0.42, orbitDist_ = 3.4;
    const double fov_ = glm::radians(55.0);
    glm::dvec3 renderOrigin_{0};
    int activeView_ = 0;          // Ansicht unter der Maus
    int dragView_ = -1;           // Ansicht, in der gerade gezogen wird
    double lastMx_ = 0, lastMy_ = 0;
    bool rmbDown_ = false, mmbDown_ = false;

    // Bauen
    Tool tool_ = Tool::Select;
    std::vector<uint32_t> selection_;   // letzter Eintrag = Hauptauswahl
    uint32_t hovered_ = 0;
    RayHit hoverHit_;
    bool hoverValid_ = false;
    bool dragCandidate_ = false, dragging_ = false;
    double dragStartMx_ = 0, dragStartMy_ = 0;
    glm::dvec3 dragPlaneP_{0}, dragPlaneN_{0}, dragStartHit_{0}, dragStartPos_{0};
    bool dragVertical_ = false;
    std::vector<std::pair<uint32_t, glm::dvec3>> dragStarts_;
    bool snap_ = true;
    int snapIndex_ = 2;           // 1 mm, 5 mm, 1 cm, 5 cm, 10 cm
    int sawAxis_ = 0;
    float sawAngle_ = 0.0f, sawKerfMm_ = 0.0f;
    float drillDiaMm_ = 8.0f, drillDepthMm_ = 0.0f;
    float roundMm_ = 3.0f;                 // Radius bzw. Fase
    bool roundChamfer_ = false;
    float millDims_[3] = {40, 20, 5};      // Tasche: Länge, Breite, Tiefe (mm)
    float millCornerMm_ = 0.0f;
    float planeMm_ = 1.0f;                 // Hobeln: Abnahme je Klick
    Preview preview_;
    bool showEdges_ = true, showShadows_ = true, showGrid_ = true;

    // Spielen
    bool play_ = false;
    bool cursorCaptured_ = false;
    double cursorX_ = 0, cursorY_ = 0;
    double scrollAccum_ = 0;
    bool heldByMouse_ = false, prevLmb_ = false, prevRmb_ = false;
    glm::dvec2 autoMove_{0};      // per Befehl: x = vorwärts, y = rechts
    double autoMoveTime_ = 0;
    bool autoRun_ = false, jumpRequest_ = false;
    double playYaw_ = 0, playPitch_ = 0;
    uint32_t grabbed_ = 0;
    glm::dvec3 grabLocal_{0};
    glm::dquat holdRel_{1, 0, 0, 0};  // Lage des getragenen Teils relativ zur Blickrichtung (R dreht sie)
    double holdDist_ = 1.5;
    bool tooHeavy_ = false;
    uint32_t lookAt_ = 0;
    double lookDist_ = 0;

    // Oberfläche
    bool showConsole_ = false, showHelp_ = false, focusConsole_ = false;
    struct LogLine {
        std::string text;
        int kind;
    };
    std::vector<LogLine> log_;
    char consoleInput_[512] = {};
    std::vector<std::string> history_;
    int historyPos_ = -1;
    bool scrollLog_ = false;
    std::string status_;
    double statusTime_ = 0;
    std::string pendingShot_;
    std::deque<std::string> scriptQueue_;
    bool scriptBusy_ = false;
    struct Job {
        std::unique_ptr<ScriptJob> job;
        int source = 0;
        uint64_t client = 0;
        std::string line;
    };
    std::vector<Job> jobs_;
    struct Toast {
        std::string text;
        double time;
        bool ok;
    };
    std::deque<Toast> toasts_;
    int newShape_ = 0, newMaterial_ = 0;
    float newDims_[4] = {80, 10, 2, 0};
    int joinType_ = 0, gluePreset_ = 0, joinAxis_ = 1;
    float joinV1_ = 0, joinV2_ = 0;
    int gearTeeth_ = 20;
    float gearModule_ = 2.0f, gearWidth_ = 1.5f, gearBore_ = 8.0f;
    int propBlades_ = 2;
    float propDiameter_ = 30.0f, propPitch_ = 20.0f;
    // Bereich "Bearbeiten" im Eigenschaften-Fenster
    float editRadius_ = 3.0f;
    int turnAxis_ = 1, bendAxis_ = 0;
    float turnFrom_ = 0, turnTo_ = 5, turnD_ = 10, turnD2_ = 0;
    float bendAt_ = 10, bendAngle_ = 90, bendRadius_ = 0;
    float scaleF_ = 1.0f;
    bool mirrorCopy_ = false;
    char nameBuf_[128] = {};
    uint32_t nameBufFor_ = 0;
    bool showInspector_ = false;
    uint32_t inspectTarget_ = 0;
    bool showDrawing_ = false;
    std::vector<uint32_t> drawingIds_;
    TechDrawing drawing_;
    Sheet sheet_;
    std::string drawingInfo_;
    int drawnObjects_ = 0, drawnLod_[3] = {0, 0, 0}, culledObjects_ = 0;
};
