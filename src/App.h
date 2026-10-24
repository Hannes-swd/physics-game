// Das Spiel mit Fenster: Kameras, Eingabe, Werkzeuge, Spielfigur, Menüs.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Avatar.h"
#include "Commands.h"
#include "Drawing.h"
#include "Net.h"
#include "Physics.h"
#include "Remote.h"
#include "Renderer.h"
#include "Script.h"
#include "Settings.h"
#include "World.h"
#include "Worlds.h"

struct GLFWwindow;

enum class ViewMode { Perspective = 0, Top = 1, Front = 2, Side = 3, Quad = 4 };
enum class Tool { Select = 0, Saw = 1, Drill = 2, Round = 3, Mill = 4, Plane = 5, Heat = 6 };
enum class NetRole { Off = 0, Host = 1, Client = 2 };

// Was eine Spielfigur in der Hand hält (eigene Figur und die der Mitspieler)
struct Hand {
    uint32_t grabbed = 0;
    glm::dvec3 grabLocal{0};
    glm::dquat holdRel{1, 0, 0, 0};  // Lage des getragenen Teils relativ zur Blickrichtung (R dreht sie)
    double holdDist = 1.5;
    bool heldByMouse = false;
    bool tooHeavy = false;
};

// Maus und Tasten einer Spielfigur seit der letzten Auswertung (Tragen, Werfen, Drehen)
struct HandInput {
    bool lmbDown = false;
    int lmbPresses = 0, rmbPresses = 0, ePresses = 0;
    int turns = 0, tilts = 0;  // R / Shift+R
    double scroll = 0;
};

// Eine Spielfigur, wie alle sie sehen (der Gastgeber verteilt die Liste)
struct NetPlayer {
    uint32_t id = 0;
    std::string name;
    bool play = false;
    glm::dvec3 feet{0}, camEye{0};  // Füße (Spiel-Modus) bzw. Kamera (Bau-Modus)
    double yaw = 0, pitch = 0;
    uint32_t grabbed = 0;
    bool tooHeavy = false;
    bool onGround = true;  // sonst in der Luft (Sprung, Fall)
};

glm::vec3 playerColor(uint32_t id);  // Erkennungsfarbe eines Spielers im LAN

class App;

// Ein Mitspieler aus Sicht des Gastgebers
struct Peer {
    uint32_t id = 0;              // Spielernummer, zugleich Nummer seiner Spielfigur in der Physik
    std::string name;
    std::unique_ptr<net::Connection> conn;
    bool greeted = false;         // hat sich vorgestellt
    bool accepted = false;        // vom Gastgeber zugelassen
    double since = 0;             // verbunden seit (Spielzeit)
    double closeAt = -1;          // abgelehnt/entfernt: Verbindung ab dieser Zeit schließen
    // Spielfigur
    bool play = false;
    double yaw = 0, pitch = 0;
    glm::vec3 wish{0};            // Laufgeschwindigkeit (Welt)
    bool jump = false;
    glm::dvec3 camEye{0};
    Hand hand;
    // Befehle laufen beim Gastgeber, aber mit Auswahl und Kamera des Mitspielers
    std::vector<uint32_t> selection;
    bool selectionChanged = false;
    glm::dvec3 spawnRef{0};
    std::unique_ptr<CommandHost> commands;
    // Was der Mitspieler schon hat (nur Änderungen werden gesendet)
    struct Sent {
        uint32_t rev = 0;
        size_t meta = 0;
        glm::dvec3 pos{0};
        glm::dquat rot{1, 0, 0, 0};
    };
    std::unordered_map<uint32_t, Sent> sent;
    std::string sentJoints, sentLoads;
    std::unordered_map<uint32_t, uint64_t> sentGround;  // Grund je Meer: zuletzt geschickter Stand (GroundMap::rev)
    bool terrainSent = false;           // Gelände: zuletzt geschickter Generator und Stand der Änderungen
    const void* sentTerrainGen = nullptr;
    uint64_t sentTerrainRev = 0;
    const void* sentTreeLib = nullptr;  // Bäume: zuletzt geschickte Arten und Zahl der entfernten
    size_t sentTaken = 0;
};

struct AppOptions {
    bool validation = false;
    std::string script;           // Befehle beim Start ausführen
    std::string screenshotPath;   // nach N Bildern Foto speichern und beenden
    int screenshotFrames = 30;
    int port = 7777;
    std::string view;             // Startansicht
    bool play = false;
    bool emptyScene = false;
    // Aus dem Hauptmenü (Menu.cpp)
    bool menu = false;            // "Zurück zum Hauptmenü" möglich, Einstellungen gelten
    std::string worldDir;         // diese Welt öffnen (Ordner, UTF-8); leer: Beispiel-Werkstatt wie bisher
    bool lanHost = false;         // die Welt gleich im LAN öffnen
    std::string joinAddress;      // gleich einem LAN-Spiel beitreten (ohne eigene Welt)
    RemoteServer* remote = nullptr;  // Fernsteuerung des Hauptmenüs (bleibt zwischen Menü und Spiel offen); sonst eine eigene
    std::function<void()> onFirstFrame;  // nach dem ersten Bild des Spielfensters (Hauptmenü: sein Ladebild schließen)
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

    NetRole netRole() const { return netRole_; }
    bool backToMenu() const { return backToMenu_; }        // nach run(): ins Hauptmenü statt beenden
    const std::string& exitMessage() const { return exitMessage_; }  // für das Hauptmenü (z.B. Beitritt fehlgeschlagen)
    const std::string& menuCommand() const { return menuCommand_; }  // im Spiel getippt ("menu worlds"): führt das Hauptmenü aus
    static bool isMenuAction(const std::string& line);  // "menu <etwas>" (nicht nur "menu"/"menu status")

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
    std::string teleportCommand(const std::vector<std::string>& args);  // Befehl "tp": Biom, Fluss, Wasser, Mitspieler, Stelle
    // Spieler (Spiel-Modus) bzw. Baukamera an die Stelle feet (Füße), Blick auf look. "player" oder "camera"
    std::string teleportTo(glm::dvec3 feet, const glm::dvec3& look, double camDist);
    double standHeight(double x, double z, double fromY);  // oberste Fläche (Teile oder Boden) unter x, z, ab fromY abwärts
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
    // Tragen/Werfen/Drehen einer Spielfigur (eigene oder Mitspieler)
    void handActions(Hand& hand, const glm::dvec3& eye, const glm::dvec3& fwd, double yaw, const HandInput& in);
    void holdForces(Hand& hand, const glm::dvec3& eye, const glm::dvec3& fwd, double yaw);
    void updatePreview();
    void applyTool();
    void enterPlay();
    void leavePlay();
    void setCursorCaptured(bool on);
    void rotateSelected(int axis, double deg);
    void deleteSelected();
    void duplicateSelected();
    void dropSelected();
    void focusSelected();
    bool isSelected(uint32_t id) const;
    void toggleSelected(uint32_t id);
    glm::dvec3 selectionCenter() const;
    glm::dvec3 orbitEye() const;

    // AppFire.cpp (Wärme und Feuer: Werkzeug, Flammen, Rauch, Feuerschein, Temperatur für den Shader)
    void heatToolInput(float dt);
    void updateFx(float dt);
    void addFireFx(FrameDesc& fd);
    void addHeatData(FrameDesc& fd, DrawItem& it, const Object& o);

    // AppWater.cpp (Gewässer für die Shader, Spritzer und Dampf, Schwimmen)
    void addWaterData(FrameDesc& fd);
    // AppTrees.cpp: Bäume des Generators (Vorlagen), Krone eines Baum-Teils (false: hat keine)
    void addTrees(FrameDesc& fd, const std::function<bool(const glm::dvec3&, const glm::dvec3&)>& visible);
    bool addCrown(FrameDesc& fd, const Object& o, const glm::mat4& model, bool low, bool shadow);
    struct GroundWindow {  // Meer: Grund um die Kamera für die Shader, nur neu, wenn die Kamera weit wandert oder er sich ändert
        uint32_t water = 0;
        uint64_t rev = 0;
        glm::dvec2 origin{0};
        bool flat = true;
        std::vector<glm::vec4> cells;  // je vier Höhen (m, Welt)
    };
    std::vector<GroundWindow> groundWindows_;
    void updateWaterFx(float dt);
    float swimDepth(const glm::dvec3& feet) const;  // Anteil der Figur unter Wasser (0..1)

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
    void drawMaterialEditor();
    void refreshDrawing();
    void log(const std::string& text, int kind = 0);  // 0 normal, 1 Befehl, 2 Fehler, 3 Info
    void openImportDialog();
    void saveDialog(bool save);

    // AppWorld.cpp (Welt öffnen, speichern, automatisch speichern, Einstellungen, zurück ins Hauptmenü)
    bool openWorld(std::string* err);
    bool saveWorld(bool quiet, bool evenAsClient = false);  // evenAsClient: kurz vor dem Beitritt zu einem LAN-Spiel
    void updateWorldSession(float dt);
    void updateTerrain();  // Gelände je Bild: Kacheln um die Kamera, Physik um Teile und Spielfiguren
    bool terrainAppCommand(const std::vector<std::string>& args, std::string& out);  // terrain borders|goto|reload
    std::string reloadTrees();  // "trees reload": assets/trees.json in die offene Welt übernehmen
    void endWorldSession();
    void applySettings();
    void storeSettings();
    void leaveToMenu();
    std::string worldCommand(const std::vector<std::string>& args);  // Befehl "world"
    std::string menuCommandInGame(const std::vector<std::string>& args);  // Befehl "menu" im Spiel
    enum class LoadKind { None, World, Join, Save };
    void beginLoading(LoadKind kind, const std::string& title, const std::string& name);
    void updateLoading(float dt);         // je Bild: Fortschritt (Kollisionsformen, Beitritt), am Ende ausblenden
    void drawLoading();                   // über allem, deckt das Bild zu
    void renderLoadingFrame(bool force);  // ein Bild nur mit dem Ladebildschirm (während das Laden den Ablauf blockiert)
    void firstFrameShown();               // einmal nach dem ersten Bild: opt_.onFirstFrame

    // AppNet.cpp (LAN-Mehrspieler: Gastgeber rechnet alles, Mitspieler zeigen seine Welt und schicken Eingaben/Befehle)
    bool netClient() const { return netRole_ == NetRole::Client; }
    void netUpdate(float dt);
    void netStop(const std::string& why);
    bool netHost(std::string* err);
    bool netJoin(const std::string& address, std::string* err);
    void netEvent(const std::string& text, bool ok);  // Meldung an alle Mitspieler
    bool peersPlaying() const;
    Peer* findPeer(uint32_t id);
    void netHostUpdate(float dt);
    void netPeerMessage(Peer& p, net::Msg type, const std::string& payload);
    void netAccept(Peer& p);
    void netReject(Peer& p, const std::string& reason);
    void netRemovePeer(size_t index, const std::string& why);
    void netSync(Peer& p);
    void netPlayerList();
    void peerForces(float h);          // im Physik-Teilschritt: Mitspieler tragen Teile
    void updatePeerFigures(float dt);  // Mitspieler laufen
    glm::dvec3 peerEye(const Peer& p) const;
    void netClientUpdate(float dt);
    void netHostMessage(net::Msg type, const std::string& payload);
    void netSendCommand(const std::string& line, int source, uint64_t client, bool quiet);
    void netSendResult(Peer& p, uint32_t request, const ScriptJob& job);
    bool localCommand(const std::string& line) const;  // Mitspieler: läuft auf dem eigenen Rechner
    std::string lanCommand(const std::vector<std::string>& args);  // Befehl "lan"
    // Bau-Modus: Teile wurden verschoben. Mitspieler schickt die neue Lage zum Gastgeber; beim Gastgeber bleiben sie
    // ruhig liegen, auch wenn die Physik gerade läuft
    void pushUndo();
    void shareTransforms(const std::vector<uint32_t>& ids);
    size_t undoSteps() const { return netClient() ? netUndo_ : world_->undoCount(); }
    size_t redoSteps() const { return netClient() ? netRedo_ : world_->redoCount(); }
    void drawNetMenu();
    void drawNetWindow();
    void drawNetRequests();
    void drawPlayerTags();
    // Spielfiguren zeichnen: Mitspieler und in der Ansicht von hinten (F5) die eigene
    void addAvatars(FrameDesc& fd);
    void addFigure(FrameDesc& fd, uint32_t key, const glm::dvec3& feet, double yaw, double pitch, bool onGround, const glm::vec3* tint,
                   float dt);
    // Blickrichtung vom Kopf aus zum Greifen und Werfen; von hinten gesehen zielt sie auf das, was mitten im Bild liegt
    glm::dvec3 aimForward() const { return play_ && thirdPerson_ ? aim_ : playerForward(); }
    void updateThirdPersonEye();

    GLFWwindow* window_ = nullptr;
    Renderer renderer_;
    Physics physics_;
    std::unique_ptr<World> world_;
    RemoteServer ownRemote_;              // ohne Hauptmenü: eigene Fernsteuerung
    RemoteServer* remote_ = &ownRemote_;  // mit Hauptmenü: dessen Server (bleibt zwischen Menü und Spiel offen)
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
    double fov_ = glm::radians(55.0);
    double mouseSens_ = 1.0;      // Spiel-Modus: Faktor auf die Mausbewegung
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
    bool showBiomeBorders_ = true;  // Gelände: Linien an den Biomgrenzen
    size_t terrainDrawn_ = 0;       // Geländekacheln im letzten Bild
    size_t treesDrawn_ = 0;         // Bäume des Generators im letzten Bild (ohne die Teile)

    // Wärme-Werkzeug (Bau-Modus: Werkzeug H, Spiel-Modus: F halten) und Effekte
    int heatTool_ = 1;                     // Index in heatTools()
    float heatSet_ = 800.0f;               // Eigenschaften: Temperatur zum Setzen
    bool heatActive_ = false;              // gerade auf ein Teil gerichtet
    uint32_t heatTarget_ = 0;
    glm::dvec3 heatPoint_{0}, heatNormal_{0, 1, 0}, heatFrom_{0};
    struct FxParticle {
        glm::dvec3 pos{0};
        glm::vec3 vel{0};
        float age = 0, life = 1, size = 0.1f, grow = 1, strength = 1, seed = 0;
        int kind = 1;                      // 1 Rauch, 2 Funke, 3 Kältenebel, 5 Dampf, 6 Wassertropfen
    };
    std::vector<FxParticle> fx_;
    double fxSmokeAccum_ = 0;
    RayHit lookHit_;                       // Spiel-Modus: was vor der Figur liegt

    // Spielen
    bool play_ = false;
    bool cursorCaptured_ = false;
    double cursorX_ = 0, cursorY_ = 0;
    double scrollAccum_ = 0;
    bool prevLmb_ = false, prevRmb_ = false;
    glm::dvec2 autoMove_{0};      // per Befehl: x = vorwärts, y = rechts
    double autoMoveTime_ = 0;
    bool axe_ = false;            // Spiel-Modus: Axt in der Hand (X), Linksklick hackt
    double axeReady_ = 0;         // nächster Hieb frühestens dann (Spielzeit)
    bool autoRun_ = false, jumpRequest_ = false;
    double playYaw_ = 0, playPitch_ = 0;
    Hand hand_;
    uint32_t lookAt_ = 0;
    double lookDist_ = 0;
    bool thirdPerson_ = false;     // F5: Kamera hinter der Figur
    glm::dvec3 thirdEye_{0}, aim_{0, 0, -1};
    double eyeLag_ = 0, lastFeetY_ = 0;  // Kamera folgt einem Schritt auf eine Stufe weich nach oben

    // Spielfiguren (Modell und Bewegungszustand je Spieler)
    struct FigureAnim {
        glm::dvec3 feet{0};
        bool init = false;
        glm::vec2 vel{0.0f};     // waagrecht, geglättet
        glm::vec2 dir{0, 1};     // Laufrichtung relativ zum Blick (x rechts, y vorn), geglättet
        float vy = 0.0f;         // senkrecht, geglättet
        float phase = 0.0f;      // Schrittzyklus 0..1
        float idleTime = 0.0f;
        float airTime = 0.0f;    // so lange schon in der Luft
        float air = 0.0f;        // 0 = am Boden, 1 = Sprunghaltung
        float land = 0.0f;       // in die Knie federn nach der Landung
    };
    // Knochen der Figur, die prozedural bewegt werden (Sprung, Landung, Kopf)
    struct FigureRig {
        int head = -1, torso = -1, thigh[2] = {-1, -1}, shin[2] = {-1, -1}, foot[2] = {-1, -1}, arm[2] = {-1, -1};
        float armSide[2] = {-1, 1};  // Seite des Arms (x), damit "abspreizen" auf beiden Seiten nach außen geht
        float legLength = 0.9f;      // Hüfte bis Knöchel
    };
    AvatarModel avatar_;
    FigureRig rig_;
    bool avatarLoaded_ = false;
    int clipIdle_ = -1, clipWalk_ = -1, clipRun_ = -1, clipLeft_ = -1, clipRight_ = -1, clipBack_ = -1;
    std::map<uint32_t, FigureAnim> figureAnim_;
    double figureTime_ = -1;

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
        int source = 0;            // 0 Konsole, 1 Skript, 2 Fernsteuerung, 3 Mitspieler im LAN
        uint64_t client = 0;
        std::string line;
        uint32_t peer = 0, request = 0;  // Mitspieler: Spielernummer und Nummer seiner Anfrage
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
    // Material-Editor
    bool showMaterials_ = false, materialsDirty_ = false;
    int matEdit_ = 0;
    char matName_[40] = {}, matLabel_[80] = {}, matAliases_[160] = {}, matNew_[40] = {};
    int matBufFor_ = -1;
    std::string matStatus_;
    bool showDrawing_ = false;
    std::vector<uint32_t> drawingIds_;
    TechDrawing drawing_;
    Sheet sheet_;
    std::string drawingInfo_;
    int drawnObjects_ = 0, drawnLod_[3] = {0, 0, 0}, culledObjects_ = 0;

    // LAN-Mehrspieler
    NetRole netRole_ = NetRole::Off;
    char netName_[40] = {};
    bool showNet_ = false;
    bool netConfirm_ = true;              // Gastgeber: Mitspieler erst nach Bestätigung zulassen
    net::Listener netListener_;
    net::Beacon netBeacon_;
    net::Search netSearch_;
    std::vector<std::string> netAddresses_;  // eigene LAN-Adressen (Anzeige beim Gastgeber)
    std::vector<std::unique_ptr<Peer>> peers_;
    uint32_t netNextPeer_ = 2;            // 1 = Gastgeber
    double netSyncAccum_ = 0, netLoadsAccum_ = 0;
    std::vector<NetPlayer> netPlayers_;   // alle Spielfiguren, auch die eigene
    uint32_t netSelfId_ = 0;
    std::unique_ptr<net::Connection> netConn_;  // Mitspieler: Verbindung zum Gastgeber
    std::string netHostName_, netStatus_;
    bool netWelcomed_ = false;
    bool netWorldReady_ = false;          // Mitspieler: die erste Lage nach dem Beitritt ist da (die Welt ist komplett)
    double netConnectStart_ = 0;
    char netAddress_[64] = {};
    struct NetRequest {
        int source = 0;
        uint64_t client = 0;
        bool quiet = false;
    };
    std::map<uint32_t, NetRequest> netRequests_;
    uint32_t netNextRequest_ = 1;
    HandInput netHand_;                   // Mitspieler: seit dem letzten Senden
    glm::vec3 netWish_{0};
    bool netJump_ = false, netSpawn_ = false;
    glm::dvec3 netSpawnFeet_{0};
    double netSpawnUntil_ = 0;  // tp: bis dahin gilt eine Lage vom Gastgeber weit weg von netSpawnFeet_ als veraltet
    glm::dvec3 netFeet_{0}, netFeetTarget_{0};  // eigene Figur laut Gastgeber (geglättet)
    bool netUndoPending_ = false;
    // Selbst verschobene Teile: Lagen vom Gastgeber kurz zurückhalten (sie sind älter als die eigene Bewegung)
    struct OwnMove {
        double time = 0;
        bool held = false;
        glm::dvec3 pos{0};
        glm::dquat rot{1, 0, 0, 0};
    };
    std::unordered_map<uint32_t, OwnMove> netOwnMoves_;
    size_t netUndo_ = 0, netRedo_ = 0;
    bool netHostSim_ = false;             // Physik läuft beim Gastgeber
    double netInputAccum_ = 0, netInspectAt_ = 0;
    std::string netInspectText_;
    GpuMesh avatarMesh_;

    // Welt (AppWorld.cpp)
    WorldInfo worldInfo_;
    bool haveWorld_ = false;          // eine Welt aus der Weltliste ist offen (wird gespeichert)
    bool worldReadOnly_ = false;      // Laden fehlgeschlagen: nicht überschreiben
    double autosaveAccum_ = 0, playTimeMark_ = 0;
    GameSettings settings_;
    bool backToMenu_ = false, joinPending_ = false;
    std::string exitMessage_, menuCommand_;

    // Ladebildschirm (AppWorld.cpp): Welt öffnen, LAN-Spiel beitreten, beim Verlassen speichern
    struct Loading {
        LoadKind kind = LoadKind::None;
        std::string title, name, detail;  // z.B. "Welt wird geladen", Name der Welt, "Teile 120 / 800"
        float progress = -1;              // 0..1, kleiner 0: unbestimmt (Balken läuft hin und her)
        double since = 0;                 // Spielzeit beim Start
        float fade = 0;                   // größer 0: blendet aus (Sekunden übrig), Eingaben gehen schon durch
        size_t jobsAtStart = 0;           // Kollisionsformen im Hintergrund, als das Laden fertig war
        size_t tilesAtStart = 0;          // fehlende Geländekacheln (höchster Stand, für den Balken)
        double lastFrame = -1;            // glfwGetTime des letzten Ladebilds (während synchroner Arbeit)
    } loading_;
    bool loadingBlocks() const { return loading_.kind != LoadKind::None && loading_.fade <= 0; }
    bool firstFrameShown_ = false;
};
