#include "App.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "Materials.h"
#include "Profile.h"
#include "Terrain.h"

namespace {

// Unendlich weite Perspektive mit Reversed-Z (nah = 1, unendlich = 0), Y nach unten für Vulkan
glm::dmat4 perspectiveInfinite(double fov, double aspect, double nearZ) {
    double f = 1.0 / std::tan(fov / 2);
    glm::dmat4 p(0.0);
    p[0][0] = f / aspect;
    p[1][1] = -f;
    p[2][3] = -1.0;
    p[3][2] = nearZ;
    return p;
}

glm::dmat4 orthoReversed(double hw, double hh, double nearZ, double farZ) {
    glm::dmat4 p = glm::orthoRH_ZO(-hw, hw, -hh, hh, farZ, nearZ);
    p[1][1] *= -1.0;
    return p;
}

struct Frustum {
    glm::dvec4 planes[6];
};

Frustum frustumOf(const glm::dmat4& m) {
    auto row = [&](int i) { return glm::dvec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    Frustum f;
    f.planes[0] = row(3) + row(0);
    f.planes[1] = row(3) - row(0);
    f.planes[2] = row(3) + row(1);
    f.planes[3] = row(3) - row(1);
    f.planes[4] = row(3) - row(2);  // nah (Reversed-Z: z <= w)
    f.planes[5] = row(2);           // fern (z >= 0)
    return f;
}

bool boxInFrustum(const Frustum& f, const glm::dvec3& mn, const glm::dvec3& mx) {
    for (const glm::dvec4& p : f.planes) {
        glm::dvec3 v(p.x > 0 ? mx.x : mn.x, p.y > 0 ? mx.y : mn.y, p.z > 0 ? mx.z : mn.z);
        if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0) return false;
    }
    return true;
}

void addLine(std::vector<LineVertex>& out, const glm::vec3& a, const glm::vec3& b, const glm::vec4& c) {
    out.push_back({a, c});
    out.push_back({b, c});
}

} // namespace

App::App() = default;
App::~App() = default;

// ============================================================================ Start / Ende

bool App::init(const AppOptions& opt) {
    opt_ = opt;
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW konnte nicht gestartet werden\n");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_MAXIMIZED, opt.screenshotPath.empty() ? GLFW_TRUE : GLFW_FALSE);
#ifdef NDEBUG
    const char* title = "Werkstatt – Handwerker-Simulator";
#else
    // Debug-Version: das Spiel selbst ist nicht optimiert (gut zum Debuggen; zum Spielen Release oder RelWithDebInfo)
    const char* title = "Werkstatt – Handwerker-Simulator (Debug-Version, langsamer)";
#endif
    window_ = glfwCreateWindow(1600, 900, title, nullptr, nullptr);
    if (!window_) return false;
    glfwSetWindowUserPointer(window_, this);
    glfwSetScrollCallback(window_, [](GLFWwindow* w, double, double y) { static_cast<App*>(glfwGetWindowUserPointer(w))->scrollAccum_ += y; });
    RenderQuality quality;  // Grafik aus den Einstellungen (Hauptmenü oder --world), sonst die Standardwerte
    if (opt.menu || !opt.worldDir.empty()) {
        const GameSettings s = loadSettings();
        quality.msaa = s.msaa;
        quality.sampleShading = s.supersampling;
        quality.shadowSize = (uint32_t)shadowMapSize(s.shadowQuality);
        quality.vsync = s.vsync;
    }
    if (!renderer_.init(window_, opt.validation, quality)) return false;
    if (!physics_.init()) return false;
    world_ = std::make_unique<World>(physics_, &renderer_);
    world_->setAsyncCollision(true);  // bearbeitete Teile: genaue Kollisionsform im Hintergrund, das Bild wartet nicht

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    const char* font = "C:/Windows/Fonts/segoeui.ttf";
    if (std::filesystem::exists(font)) {
        // Latein-1, Gedankenstriche, deutsche Anführungszeichen „“ ‚‘, Auslassungspunkte, Pfeile, ⌀
        static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2013, 0x2014, 0x2018, 0x201E, 0x2026, 0x2026, 0x2190, 0x2193, 0x2300, 0x2300, 0};
        io.Fonts->AddFontFromFileTTF(font, 18.0f, nullptr, ranges);
    }
    setupStyle();
    ImGui_ImplGlfw_InitForVulkan(window_, true);
    renderer_.initImGui();

    std::snprintf(netName_, sizeof(netName_), "%s", defaultPlayerName().c_str());  // Name im LAN: Windows-Benutzer
    if (opt.menu || !opt.worldDir.empty()) applySettings();  // Hauptmenü oder --world: Einstellungen gelten
    if (!opt.worldDir.empty()) {  // Welt aus der Weltliste
        std::string err;
        if (!openWorld(&err)) {
            exitMessage_ = "Die Welt konnte nicht geöffnet werden: " + err;
            return false;
        }
    } else if (!opt.emptyScene && opt.joinAddress.empty()) {
        world_->demoScene();
    }
    log("Welcome to the workshop! F1 = console, 'help' lists all commands.", 3);
#ifndef NDEBUG
    log("Debug build: the game code is not optimized and runs slower. For playing use Release or RelWithDebInfo.", 2);
#endif
    if (opt.remote) remote_ = opt.remote;  // Hauptmenü: dessen Server läuft schon (und bleibt nach dem Spiel offen)
    if (remote_->running() || remote_->start(opt.port))
        log("Remote control active: Werkstatt.exe --send \"command\" (port " + std::to_string(opt.port) + ")", 3);
    else
        log("Remote control not available (port " + std::to_string(opt.port) + " in use)", 2);

    if (!opt.view.empty()) setView(opt.view);
    if (!opt.script.empty()) {  // Zeilen laufen nacheinander im Spiel (warte funktioniert)
        std::ifstream in(std::filesystem::u8path(opt.script));
        if (!in) log("Script not found: " + opt.script, 2);
        std::string line;
        while (std::getline(in, line)) scriptQueue_.push_back(line);
    }
    if (opt.play) enterPlay();
    if (opt.lanHost) {  // Weltliste: "Im LAN öffnen"
        std::string err;
        if (netHost(&err)) showNet_ = true;
        else toast("LAN: " + err, false);
    }
    if (!opt.joinAddress.empty()) {  // Hauptmenü: Mehrspieler -> Beitreten
        std::string err;
        if (netJoin(opt.joinAddress, &err)) {
            joinPending_ = true;
            beginLoading(LoadKind::Join, "LAN-Spiel beitreten", opt.joinAddress);
            loading_.detail = "Verbinde mit " + opt.joinAddress + " ...";
            renderLoadingFrame(true);
        } else {
            exitMessage_ = "Beitreten fehlgeschlagen: " + err;
            backToMenu_ = quit_ = true;
        }
    }
    return true;
}

void App::shutdown() {
    renderer_.waitIdle();
    for (const Job& j : jobs_)  // Fernaufträge, die noch warten, bekommen eine Antwort (der Server bleibt beim Hauptmenü offen)
        if (j.source == 2 && !j.peer) remote_->respond(j.client, "Error: the world was closed before the command finished");
    jobs_.clear();
    if (remote_ == &ownRemote_) remote_->stop();
    netStop("");
    renderer_.destroyMesh(avatarMesh_);
    flora_.reset();
    world_.reset();
    physics_.shutdown();
    renderer_.shutdown();
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

int App::run(const AppOptions& opt) {
    if (!init(opt)) {
        shutdown();
        backToMenu_ = opt.menu;  // mit der Meldung zurück ins Hauptmenü
        return 1;
    }
    double last = glfwGetTime();
    while (!glfwWindowShouldClose(window_) && !quit_) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = (float)std::min(now - last, 0.1);
        last = now;
        time_ += dt;

        // Befehle kommen auch an, wenn das Fenster minimiert ist
        remote_->poll([&](uint64_t client, const std::string& l) {
            // Auf dem Weg ins Hauptmenü, oder ein Befehl für das Hauptmenü ("menu worlds", "menu play X"): das Hauptmenü
            // bearbeitet die Zeile und antwortet (die Verbindung bleibt dabei offen)
            if (!backToMenu_ && opt_.menu && isMenuAction(l)) leaveToMenu();
            if (backToMenu_) remote_->hold(client, l);
            else submit(l, 2, client);
        });
        netUpdate(dt);  // LAN: Mitspieler bzw. Gastgeber (auch minimiert)
        updateWorldSession(dt);  // automatisch speichern, Beitritt aus dem Hauptmenü
        updateTerrain();         // Gelände: Kacheln um die Kamera, Physik um Teile und Spielfiguren
        updateLoading(dt);       // Ladebildschirm: Fortschritt, am Ende ausblenden
        processScriptQueue();
        stepJobs();
        for (const std::string& e : world_->takeEvents()) {  // Brüche usw.
            log("!! " + e, 2);
            toast(e, false);
            netEvent(e, false);
        }
        if (quit_) break;

        int fw = 0, fh = 0;
        glfwGetFramebufferSize(window_, &fw, &fh);
        if (fw == 0 || fh == 0) {  // minimiert
            if (!play_ && peersPlaying()) updatePlay(dt);  // Gastgeber: Mitspieler laufen weiter
            glfwWaitEventsTimeout(peers_.empty() ? 0.05 : 0.01);
            continue;
        }
        if (dt > 0) fps_ = fps_ * 0.95f + (1.0f / dt) * 0.05f;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const auto frameStart = std::chrono::steady_clock::now();
        computeViews();
        {
            PROFILE("app.input");
            updatePause();  // Pausenfenster des Hauptmenüs (eigenes Qt-Fenster)
            updateInventory();  // Inventarfenster (eigenes Qt-Fenster)
            updateCatalog();    // Auswahl der Bauvorlagen und Rezepte (eigenes Qt-Fenster)
            handleInput(dt);
            heatToolInput(dt);
        }
        world_->pollCollisionJobs();  // im Hintergrund fertig gewordene Kollisionsformen übernehmen
        world_->advanceWaterClock(dt);  // Wellen laufen auch, wenn die Physik steht
        if (!netClient()) {
            PROFILE("world.heatOnly");
            world_->stepHeatOnly(dt);  // Bau-Modus: Wärme und Feuer laufen weiter, auch wenn die Physik steht
        }
        if (play_ || peersPlaying()) {
            PROFILE("world.step (play)");
            updatePlay(dt);
        }
        {
            PROFILE("app.fx");
            updateFx(dt);
        }
        computeViews();
        updatePreview();
        {
            PROFILE("app.ui");
            drawUI();
            ImGui::Render();
        }

        FrameDesc fd;
        {
            PROFILE("app.buildFrame");
            buildFrame(fd);
        }
        if (!pendingShot_.empty()) {
            fd.screenshotPath = pendingShot_;
            pendingShot_.clear();
        }
        if (!opt_.screenshotPath.empty() && frame_ + 1 == (uint64_t)opt_.screenshotFrames) fd.screenshotPath = opt_.screenshotPath;
        {
            PROFILE("render (cpu + gpu wait)");
            renderer_.render(fd);
        }
        firstFrameShown();
        prof::frame(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count());
        ++frame_;
        if (!opt_.screenshotPath.empty() && frame_ >= (uint64_t)opt_.screenshotFrames) quit_ = true;
    }
    if (opt_.pauseUi) opt_.pauseUi->close();
    if (opt_.inventoryUi) opt_.inventoryUi->close();
    if (opt_.catalogUi) opt_.catalogUi->close();
    endWorldSession();  // Welt speichern (auch beim Schließen des Fensters), Einstellungen zurückschreiben
    shutdown();
    return 0;
}

void App::runLine(const std::string& line, const char*) {
    log("> " + line, 1);
    submit(line, 0);
}

double App::snapStep() const {
    const double steps[] = {0.001, 0.005, 0.01, 0.05, 0.1};
    return steps[std::clamp(snapIndex_, 0, 4)];
}

// ============================================================================ CommandHost

glm::dvec3 App::spawnReference() {
    if (play_) {
        glm::dvec3 p = playerEye() + playerForward() * 1.5;
        return glm::dvec3(p.x, world_->groundHeight(p.x, p.z), p.z);  // auf dem Boden bzw. Gelände
    }
    return glm::dvec3(orbitTarget_.x, world_->groundHeight(orbitTarget_.x, orbitTarget_.z), orbitTarget_.z);
}

void App::select(uint32_t id) {
    selection_.clear();
    if (id) selection_.push_back(id);
}

void App::showDrawing(const std::vector<uint32_t>& ids) {
    drawingIds_ = ids;
    showDrawing_ = true;
    refreshDrawing();
}

std::string App::setMode(bool play) {
    if (play && !play_) enterPlay();
    else if (!play && play_) leavePlay();
    return play_ ? "play mode: physics running" : "build mode: physics paused";
}

std::string App::setView(const std::string& v) {
    if (v == "perspective" || v == "3d" || v == "1") viewMode_ = ViewMode::Perspective;
    else if (v == "top" || v == "2") viewMode_ = ViewMode::Top;
    else if (v == "front" || v == "3") viewMode_ = ViewMode::Front;
    else if (v == "side" || v == "4") viewMode_ = ViewMode::Side;
    else if (v == "quad" || v == "5") viewMode_ = ViewMode::Quad;
    else return "Error: unknown view (perspective, top, front, side, quad)";
    if (play_) leavePlay();
    return "view changed";
}

std::string App::screenshot(const std::string& path) {
    pendingShot_ = path;
    return "saving screenshot: " + path;
}

std::string App::cameraLook(const glm::dvec3& target, double dist) {
    if (play_) leavePlay();
    orbitTarget_ = target;
    orbitDist_ = std::clamp(dist, 0.03, 20000.0);
    return "camera looks at " + fmtNum(target.x * 100) + " " + fmtNum(target.y * 100) + " " + fmtNum(target.z * 100) + " cm";
}

// ============================================================================ Kameras

glm::dvec3 App::playerEye() const {
    // Mitspieler: die Figur läuft beim Gastgeber, hier steht ihre zuletzt gemeldete (geglättete) Lage
    return (netClient() ? netFeet_ : physics_.characterPosition()) + glm::dvec3(0, 1.65 + eyeLag_, 0);
}

glm::dvec3 App::playerForward() const {
    return glm::dvec3(-std::sin(playYaw_) * std::cos(playPitch_), std::sin(playPitch_), -std::cos(playYaw_) * std::cos(playPitch_));
}

void App::computeViews() {
    const int W = std::max(1, renderer_.width()), H = std::max(1, renderer_.height());
    renderOrigin_ = play_ ? playerEye() : orbitTarget_;
    if (play_ && thirdPerson_) updateThirdPersonEye();
    views_.clear();
    if (play_ || viewMode_ != ViewMode::Quad) {
        views_.push_back(makeView(play_ ? ViewMode::Perspective : viewMode_, 0, 0, W, H));
    } else {
        int hw = W / 2, hh = H / 2;
        views_.push_back(makeView(ViewMode::Top, 0, 0, hw - 1, hh - 1));
        views_.push_back(makeView(ViewMode::Perspective, hw + 1, 0, W - hw - 1, hh - 1));
        views_.push_back(makeView(ViewMode::Front, 0, hh + 1, hw - 1, H - hh - 1));
        views_.push_back(makeView(ViewMode::Side, hw + 1, hh + 1, W - hw - 1, H - hh - 1));
    }
}

App::View App::makeView(ViewMode m, int x, int y, int w, int h) const {
    View v;
    v.mode = m;
    v.x = x;
    v.y = y;
    v.w = std::max(1, w);
    v.h = std::max(1, h);
    const double aspect = double(v.w) / double(v.h);
    if (play_) {
        v.eye = thirdPerson_ ? thirdEye_ : playerEye();
        v.target = v.eye + playerForward();
    } else {
        v.target = orbitTarget_;
        switch (m) {
        case ViewMode::Top:
            v.eye = v.target + glm::dvec3(0, 500, 0);
            v.up = glm::dvec3(0, 0, -1);
            v.ortho = true;
            break;
        case ViewMode::Front:
            v.eye = v.target + glm::dvec3(0, 0, 500);
            v.ortho = true;
            break;
        case ViewMode::Side:
            v.eye = v.target + glm::dvec3(500, 0, 0);
            v.ortho = true;
            break;
        default:
            v.eye = v.target + orbitDist_ * glm::dvec3(std::cos(orbitPitch_) * std::sin(orbitYaw_), std::sin(orbitPitch_),
                                                       std::cos(orbitPitch_) * std::cos(orbitYaw_));
            break;
        }
    }
    glm::dmat4 view = glm::lookAt(v.eye - renderOrigin_, v.target - renderOrigin_, v.up);
    glm::dmat4 proj;
    if (v.ortho) {
        v.halfHeight = orbitDist_ * std::tan(fov_ / 2);
        proj = orthoReversed(v.halfHeight * aspect, v.halfHeight, 0.05, 1500.0);
    } else {
        proj = perspectiveInfinite(fov_, aspect, play_ ? 0.02 : std::clamp(orbitDist_ * 0.002, 0.002, 0.5));
    }
    v.viewProjRel = proj * view;
    return v;
}

bool App::mouseRay(const View& v, double mx, double my, glm::dvec3& o, glm::dvec3& d) const {
    glm::dvec2 ndc((mx - v.x) / v.w * 2.0 - 1.0, (my - v.y) / v.h * 2.0 - 1.0);
    glm::dmat4 inv = glm::inverse(v.viewProjRel);
    glm::dvec4 p0 = inv * glm::dvec4(ndc, 1.0, 1.0);
    glm::dvec4 p1 = inv * glm::dvec4(ndc, 0.5, 1.0);
    if (std::abs(p0.w) < 1e-12 || std::abs(p1.w) < 1e-12) return false;
    glm::dvec3 a = glm::dvec3(p0) / p0.w, b = glm::dvec3(p1) / p1.w;
    o = a + renderOrigin_;
    d = glm::normalize(b - a);
    return true;
}

int App::viewAt(double mx, double my) const {
    for (size_t i = 0; i < views_.size(); ++i) {
        const View& v = views_[i];
        if (mx >= v.x && mx < v.x + v.w && my >= v.y && my < v.y + v.h) return (int)i;
    }
    return 0;
}

// ============================================================================ Bild zusammenstellen

void App::buildFrame(FrameDesc& fd) {
    const glm::dvec3 O = renderOrigin_;
    fd.origin = O;
    fd.buildMode = !play_;
    fd.grid = showGrid_ && !play_;
    fd.shadows = showShadows_;
    fd.time = (float)time_;
    fd.sunDir = glm::normalize(glm::vec3(0.45f, 0.85f, 0.3f));
    fd.fogDensity = (float)(0.00035 / settings_.viewDistance);  // Sichtweite (Einstellungen -> Grafik)

    for (const View& v : views_) {
        ViewDesc vd;
        vd.viewProj = glm::mat4(v.viewProjRel);
        vd.camPos = glm::vec3(v.eye - O);
        vd.ortho = v.ortho;
        vd.x = v.x;
        vd.y = v.y;
        vd.w = v.w;
        vd.h = v.h;
        fd.views.push_back(vd);
    }

    // Schattenbereich um den Blickpunkt (im Spiel-Modus so weit, wie die Schattenstufe in den Einstellungen sagt)
    const double playShadow = shadowRange(settings_.shadowQuality);
    glm::dvec3 shadowCenter = play_ ? playerEye() + playerForward() * std::max(4.0, playShadow * 0.3) : orbitTarget_;
    double R = play_ ? playShadow : std::clamp(orbitDist_ * 1.1, 2.0, 60.0);
    if (!play_ && viewMode_ != ViewMode::Perspective) R = std::clamp(orbitDist_ * 1.4, 2.0, 80.0);
    {
        glm::vec3 c = glm::vec3(shadowCenter - O);
        glm::vec3 L = fd.sunDir;
        glm::mat4 lv = glm::lookAt(c + L * 150.0f, c, glm::vec3(0, 1, 0));
        float texel = float(2.0 * R / renderer_.shadowSize());
        glm::vec4 lc = lv * glm::vec4(c, 1.0f);
        glm::vec2 snapped = glm::floor(glm::vec2(lc) / texel) * texel;
        lv = glm::translate(glm::mat4(1.0f), glm::vec3(snapped - glm::vec2(lc), 0.0f)) * lv;
        glm::mat4 lp = glm::orthoRH_ZO(-(float)R, (float)R, -(float)R, (float)R, 1.0f, 400.0f);
        lp[1][1] *= -1.0f;
        fd.lightViewProj = lp * lv;
        fd.shadowTexelWorld = texel;
    }

    std::vector<Frustum> frusta;
    for (const View& v : views_) frusta.push_back(frustumOf(v.viewProjRel));
    auto visibleAny = [&](const glm::dvec3& mn, const glm::dvec3& mx) {
        for (const Frustum& f : frusta)
            if (boxInFrustum(f, mn - O, mx - O)) return true;
        return false;
    };

    if (const Terrain* tr = world_->terrain()) {
        // Gelände: die gewählten Kacheln (Terrain::updateView), Farbe je Eckpunkt, dazu die Biomgrenzen als Linien
        terrainDrawn_ = 0;
        for (const Terrain::Drawn& d : tr->drawList()) {
            if (!d.mesh || !d.mesh->valid() || !visibleAny(d.lo, d.hi)) continue;
            DrawItem it;
            it.mesh = d.mesh;
            it.castShadow = d.level <= 2;
            it.data.model = glm::translate(glm::mat4(1.0f), glm::vec3(d.origin - O));
            it.data.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.92f);
            it.data.color2 = glm::vec4(0.0f);
            it.data.params = glm::vec4(11, 0, 0, 0);
            fd.items.push_back(it);
            ++terrainDrawn_;
            if (showBiomeBorders_ && d.border && !d.border->empty() && d.level <= 5) {
                const glm::vec3 off(d.origin - O);
                for (const LineVertex& lv : *d.border) fd.lines.push_back({lv.pos + off, lv.color});
            }
        }
    } else {  // flache Welt: Boden (liegt immer unter der Kamera)
        DrawItem g;
        g.mesh = &world_->groundMesh();
        g.castShadow = false;
        g.data.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, (float)(-O.y), 0.0f));
        g.data.color = glm::vec4(0.3f, 0.3f, 0.3f, 0.85f);
        g.data.color2 = glm::vec4(0.2f, 0.2f, 0.2f, 0.0f);
        g.data.params = glm::vec4(10, 0, 0, 0);
        fd.items.push_back(g);
    }
    auto pixelSize = [&](const View& v, const Object& o) {
        double r = 0.5 * glm::length(o.wMax - o.wMin);
        if (v.ortho) return r / v.halfHeight * v.h * 0.5;
        double d = std::max(glm::length((o.wMin + o.wMax) * 0.5 - v.eye), 1e-3);
        return r / d * (v.h * 0.5 / std::tan(fov_ / 2));
    };

    drawnObjects_ = culledObjects_ = 0;
    drawnLod_[0] = drawnLod_[1] = drawnLod_[2] = 0;
    const bool edges = !play_ && showEdges_;
    const glm::vec4 edgeColor(0.03f, 0.03f, 0.04f, 0.55f);
    const glm::vec4 selColor(1.0f, 0.6f, 0.12f, 1.0f);

    world_->grid().forEachNear(O, 60000.0, [&](Chunk& ch) {
        glm::dvec3 cmn, cmx;
        world_->grid().contentBounds(ch.key, cmn, cmx);
        if (!visibleAny(cmn, cmx)) {
            culledObjects_ += (int)ch.objects.size();
            return;
        }
        for (uint32_t id : ch.objects) {
            Object* o = world_->find(id);
            if (!o) continue;
            double px = 0;
            bool vis = false;
            for (size_t i = 0; i < views_.size(); ++i)
                if (boxInFrustum(frusta[i], o->wMin - O, o->wMax - O)) {
                    vis = true;
                    px = std::max(px, pixelSize(views_[i], *o));
                }
            const bool sel = isSelected(o->id);
            if (!vis || (px < 0.4 && !sel)) {
                ++culledObjects_;
                continue;
            }
            // Detailstufe: 0 = voll + Kanten, 1 = voll, 2 = grobe Ersatzform
            int lod = px > 28 ? 0 : (px > 3 ? 1 : 2);
            if (sel || o->id == hovered_ || o->id == hand_.grabbed) lod = 0;
            if (fd.items.size() >= 8000) break;
            ++drawnObjects_;
            ++drawnLod_[lod];

            const Material& m = materials()[o->material];
            DrawItem it;
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(o->pos - O)) * glm::mat4_cast(glm::quat(o->rot));
            if (lod == 2) {
                it.mesh = &world_->boxMesh();
                glm::vec3 c = glm::vec3((o->bbMin + o->bbMax) * 0.5), s = glm::vec3(o->bbMax - o->bbMin);
                model = model * glm::translate(glm::mat4(1.0f), c) * glm::scale(glm::mat4(1.0f), glm::max(s, glm::vec3(1e-4f)));
            } else {
                it.mesh = &o->mesh;
            }
            it.data.model = model;
            it.data.color = glm::vec4(m.color, m.roughness);
            it.data.color2 = glm::vec4(m.color2, m.metallic);
            float hl = 0;
            if (preview_.valid && preview_.objId == o->id) hl = 3;
            else if (sel || o->id == hand_.grabbed) hl = 2;
            else if (o->id == hovered_ || (play_ && o->id == lookAt_)) hl = 1;
            // Anlauffarben laut Materialdatei: Stahl bunt, Kupfer/Messing dunkel, Aluminium bleibt blank
            const float temper = (float)m.temper;
            it.data.params = glm::vec4((float)m.shading, o->seed * 10.0f, hl, temper);
            if (lod < 2) addHeatData(fd, it, *o);
            glm::dvec3 ctr = (o->wMin + o->wMax) * 0.5;
            it.castShadow = lod < 2 && glm::length(ctr - shadowCenter) < R * 1.6;
            fd.items.push_back(it);
            // Baum: die Krone hängt am Teil (Lage des Teils, nicht die Ersatzform)
            if (o->crown)
                addCrown(fd, *o, glm::translate(glm::mat4(1.0f), glm::vec3(o->pos - O)) * glm::mat4_cast(glm::quat(o->rot)), lod == 2, it.castShadow);

            if (edges && lod == 0 && !o->edges.empty() && fd.lines.size() < 3000000) {
                glm::mat3 rot = glm::mat3_cast(glm::quat(o->rot));
                glm::vec3 t = glm::vec3(o->pos - O);
                const glm::vec4& c = sel ? selColor : edgeColor;
                for (size_t i = 0; i + 1 < o->edges.size(); i += 2)
                    addLine(fd.lines, t + rot * o->edges[i], t + rot * o->edges[i + 1], c);
            }
        }
    });

    addTrees(fd, visibleAny);  // Bäume des Generators (die nahen sind Teile, oben schon dabei)
    addFlora(fd, visibleAny);  // Gras, Blumen, Büsche
    addScatter(fd, visibleAny); // Steine, Stöcke, Stämme, Stümpfe (die großen in der Nähe sind Teile, oben schon dabei)
    addPlans(fd);      // Bauvorlagen: Hologramme
    addAvatars(fd);    // andere Spieler im LAN
    addFireFx(fd);     // Flammen, Rauch, Feuerschein
    addWaterData(fd);  // Gewässer: Wellen, Farbe, Lichtflecken

    // Auswahl: Hüllquader + lokale Achsen
    if (!play_) {
        for (uint32_t id : selection_) {
            Object* o = world_->find(id);
            if (!o) continue;
            glm::mat3 rot = glm::mat3_cast(glm::quat(o->rot));
            glm::vec3 t = glm::vec3(o->pos - O);
            glm::vec3 a = glm::vec3(o->bbMin), b = glm::vec3(o->bbMax);
            glm::vec3 c[8];
            for (int i = 0; i < 8; ++i) c[i] = t + rot * glm::vec3((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z);
            const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            glm::vec4 bc(1.0f, 0.65f, 0.2f, 0.35f);
            for (auto& k : e) addLine(fd.overlayLines, c[k[0]], c[k[1]], bc);
            float len = 0.6f * std::max({b.x - a.x, b.y - a.y, b.z - a.z});
            if (id == selectedId()) {
                addLine(fd.overlayLines, t, t + rot * glm::vec3(len, 0, 0), {0.95f, 0.25f, 0.2f, 1});
                addLine(fd.overlayLines, t, t + rot * glm::vec3(0, len, 0), {0.3f, 0.9f, 0.3f, 1});
                addLine(fd.overlayLines, t, t + rot * glm::vec3(0, 0, len), {0.3f, 0.5f, 1.0f, 1});
            }
            if (o->hingeAxis >= 0 && !o->fixed) {
                glm::vec3 ax(0);
                ax[o->hingeAxis] = 1;
                ax = rot * ax * (len * 1.5f);
                addLine(fd.overlayLines, t - ax, t + ax, {1.0f, 0.9f, 0.1f, 1});
            }
        }
    }

    // Verbindungen: Seile und Federn immer, Markierungen für Kleber/Nägel/Scharniere im Bau-Modus
    for (const auto& jp : world_->joints()) {
        const Joint& j = *jp;
        glm::dvec3 pa, pb;
        world_->jointPoints(j, pa, pb);
        const glm::vec3 a = glm::vec3(pa - O), b = glm::vec3(pb - O);
        const float u = (float)std::clamp(j.load, 0.0, 1.0);
        const glm::vec4 loadCol = u < 0.5f ? glm::vec4(0.2f + 1.6f * u, 0.9f, 0.25f, 1) : glm::vec4(1.0f, 0.9f - 1.6f * (u - 0.5f), 0.2f, 1);
        if (j.type == JointType::Rope) {
            addLine(fd.lines, a, b, {0.75f, 0.6f, 0.35f, 1});
        } else if (j.type == JointType::Spring) {  // Zickzack
            glm::vec3 d = b - a;
            float len = glm::length(d);
            if (len > 1e-5f) {
                glm::vec3 dir = d / len;
                glm::vec3 side = glm::normalize(glm::cross(dir, std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
                const int N = 16;
                float r = std::min(0.03f, len * 0.1f);
                glm::vec3 prev = a;
                for (int i = 1; i <= N; ++i) {
                    glm::vec3 p = a + d * (float(i) / N) + (i < N ? side * (i % 2 ? r : -r) : glm::vec3(0));
                    addLine(fd.lines, prev, p, {0.7f, 0.72f, 0.75f, 1});
                    prev = p;
                }
            }
        } else if (!play_) {
            float s = 0.012f * (float)std::max(0.3, std::min(orbitDist_, 4.0));
            if (j.type == JointType::Hinge) {
                if (const Object* B = world_->find(j.b)) {
                    glm::vec3 ax = glm::vec3(B->rot * j.axisB) * (float)(0.6 * glm::length(B->bbMax - B->bbMin));
                    addLine(fd.overlayLines, b - ax, b + ax, loadCol);
                }
            } else {
                addLine(fd.overlayLines, a - glm::vec3(s, 0, 0), a + glm::vec3(s, 0, 0), loadCol);
                addLine(fd.overlayLines, a - glm::vec3(0, s, 0), a + glm::vec3(0, s, 0), loadCol);
                addLine(fd.overlayLines, a - glm::vec3(0, 0, s), a + glm::vec3(0, 0, s), loadCol);
            }
        }
    }

    // Physik-Inspektor: Schwerpunkt, Kontaktpunkte und Kraftpfeile
    if (showInspector_ && world_->inspectId()) {
        if (Object* o = world_->find(world_->inspectId())) {
            BodyState bs;
            glm::dvec3 com = o->pos;
            bool hasState = physics_.bodyState(o->body, bs);
            if (hasState) com = bs.com;
            const InspectData& d = world_->inspect();
            const glm::vec3 c = glm::vec3(com - O);
            const double size = std::max(0.05, glm::length(o->bbMax - o->bbMin));
            const double weight = std::max(1e-6, o->mass * 9.81);
            auto arrow = [&](const glm::dvec3& f, const glm::vec4& col, double scale) {
                glm::dvec3 v = f * scale;
                double len = glm::length(v);
                if (len < 1e-4) return;
                if (len > 3 * size) {
                    v *= 3 * size / len;
                    len = 3 * size;
                }
                glm::vec3 b = c + glm::vec3(v);
                glm::vec3 dir = glm::normalize(glm::vec3(v));
                glm::vec3 side = glm::normalize(glm::cross(dir, std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
                float hl = (float)std::min(len * 0.25, size * 0.15);
                addLine(fd.overlayLines, c, b, col);
                addLine(fd.overlayLines, b, b - dir * hl + side * hl * 0.5f, col);
                addLine(fd.overlayLines, b, b - dir * hl - side * hl * 0.5f, col);
            };
            const double fs = 0.6 * size / weight;  // Meter pro Newton (Gewicht = 0,6 x Teilgröße)
            arrow(glm::dvec3(0, -weight, 0), {1.0f, 0.25f, 0.2f, 1}, fs);
            if (d.valid) {
                arrow(d.fContact, {0.3f, 1.0f, 0.35f, 1}, fs);
                arrow(d.fExternal, {0.8f, 0.4f, 1.0f, 1}, fs);
                arrow(d.fDrag + d.fBuoyancy, {0.3f, 0.95f, 1.0f, 1}, fs);
                arrow(d.fNet, {1, 1, 1, 1}, fs);
            }
            if (hasState) arrow(bs.vel, {0.35f, 0.55f, 1.0f, 1}, 0.25);
            float r = (float)size * 0.08f;
            glm::vec4 white(1, 1, 1, 1);
            addLine(fd.overlayLines, c - glm::vec3(r, 0, 0), c + glm::vec3(r, 0, 0), white);
            addLine(fd.overlayLines, c - glm::vec3(0, r, 0), c + glm::vec3(0, r, 0), white);
            addLine(fd.overlayLines, c - glm::vec3(0, 0, r), c + glm::vec3(0, 0, r), white);
            for (const ContactInfo& ct : d.contacts) {
                glm::vec3 p = glm::vec3(ct.point - O);
                float q = (float)size * 0.03f;
                glm::vec4 y(1.0f, 0.9f, 0.1f, 1);
                addLine(fd.overlayLines, p - glm::vec3(q, 0, 0), p + glm::vec3(q, 0, 0), y);
                addLine(fd.overlayLines, p - glm::vec3(0, 0, q), p + glm::vec3(0, 0, q), y);
                addLine(fd.overlayLines, p, p + glm::vec3(ct.normal) * q * 2.0f, y);
            }
        }
    }

    // Werkzeugvorschau
    if (preview_.valid) {
        Object* o = world_->find(preview_.objId);
        if (o) {
            glm::dmat3 rot = glm::mat3_cast(o->rot);
            auto W = [&](const glm::dvec3& p) { return glm::vec3(o->pos - O + rot * p); };
            if (tool_ == Tool::Saw) {
                // Schnittfläche = Ebene geschnitten mit dem Hüllquader
                solid::Piece box = solid::makeBox((o->bbMax - o->bbMin) * 0.5 + 0.004);
                for (auto& f : box.faces) {
                    glm::dvec3 shift = (o->bbMin + o->bbMax) * 0.5;
                    for (auto& v : f.v) v += shift;
                    f.d += glm::dot(f.n, shift);
                }
                solid::Piece cutp;
                if (solid::clip(box, preview_.plane, cutp)) {
                    for (auto& f : cutp.faces) {
                        if (glm::dot(f.n, preview_.plane.n) < 0.9999 || std::abs(f.d - preview_.plane.d) > 1e-6) continue;
                        glm::vec4 fill(1.0f, 0.2f, 0.1f, 0.28f), line(1.0f, 0.35f, 0.2f, 1.0f);
                        for (size_t i = 1; i + 1 < f.v.size(); ++i) {
                            fd.translucent.push_back({W(f.v[0]), fill});
                            fd.translucent.push_back({W(f.v[i]), fill});
                            fd.translucent.push_back({W(f.v[i + 1]), fill});
                        }
                        for (size_t i = 0; i < f.v.size(); ++i) addLine(fd.overlayLines, W(f.v[i]), W(f.v[(i + 1) % f.v.size()]), line);
                    }
                }
            } else if (tool_ == Tool::Drill) {
                glm::dvec3 d = glm::normalize(preview_.dir);
                glm::dvec3 u = glm::normalize(glm::cross(d, std::abs(d.y) < 0.9 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0)));
                glm::dvec3 w = glm::cross(d, u);
                double r = drillDiaMm_ / 2000.0;
                const int N = 32;
                glm::vec4 col(1.0f, 0.75f, 0.1f, 1.0f), fill(1.0f, 0.75f, 0.1f, 0.25f);
                for (int i = 0; i < N; ++i) {
                    double a0 = 2 * glm::pi<double>() * i / N, a1 = 2 * glm::pi<double>() * (i + 1) / N;
                    glm::dvec3 p0 = preview_.from + (u * std::cos(a0) + w * std::sin(a0)) * r;
                    glm::dvec3 p1 = preview_.from + (u * std::cos(a1) + w * std::sin(a1)) * r;
                    addLine(fd.overlayLines, W(p0), W(p1), col);
                    addLine(fd.overlayLines, W(p0 + d * preview_.length), W(p1 + d * preview_.length), col * glm::vec4(1, 1, 1, 0.5f));
                    fd.translucent.push_back({W(preview_.from), fill});
                    fd.translucent.push_back({W(p0), fill});
                    fd.translucent.push_back({W(p1), fill});
                    if (i % 8 == 0) addLine(fd.overlayLines, W(p0), W(p0 + d * preview_.length), col * glm::vec4(1, 1, 1, 0.5f));
                }
            } else if (tool_ == Tool::Round && preview_.edge >= 0) {
                const auto& edges = world_->edgesOf(*o);
                if (preview_.edge < (int)edges.size()) {
                    const solid::Edge& e = edges[preview_.edge];
                    glm::vec4 col = e.convex ? glm::vec4(1.0f, 0.75f, 0.1f, 1.0f) : glm::vec4(0.3f, 0.85f, 1.0f, 1.0f);
                    // Kante dick zeichnen (mehrere leicht versetzte Linien) und die Rundung andeuten
                    const glm::dvec3 off = (e.n1 + e.n2) * 0.0004;
                    for (int k = -1; k <= 1; ++k) addLine(fd.overlayLines, W(e.a + off * double(k)), W(e.b + off * double(k)), col);
                    // wo die Rundung auf den beiden Flächen beginnt
                    const double r = roundMm_ / 1000.0, dist = roundChamfer_ ? r : r * std::tan(e.angle / 2);
                    const glm::dvec3 ed = glm::normalize(e.b - e.a);
                    for (int side = 0; side < 2; ++side) {
                        const glm::dvec3 n = side ? e.n2 : e.n1, other = side ? e.n1 : e.n2;
                        glm::dvec3 in = glm::normalize(glm::cross(n, ed));
                        if (e.convex != (glm::dot(in, other) < 0)) in = -in;
                        addLine(fd.overlayLines, W(e.a + in * dist), W(e.b + in * dist), col * glm::vec4(1, 1, 1, 0.6f));
                    }
                }
            } else if (tool_ == Tool::Mill || tool_ == Tool::Plane) {
                const int k = preview_.faceAxis, s = preview_.faceSign;
                const int u = k == 0 ? 1 : 0, v = k == 2 ? 1 : 2;
                glm::dvec3 c = (o->bbMin + o->bbMax) * 0.5;
                c[k] = (s > 0 ? o->bbMax[k] : o->bbMin[k]) + s * 0.0005;
                double hu, hv;
                if (tool_ == Tool::Mill) {
                    c[u] += preview_.a;
                    c[v] += preview_.b;
                    hu = millDims_[0] / 2000.0;
                    hv = millDims_[1] / 2000.0;
                } else {
                    hu = (o->bbMax[u] - o->bbMin[u]) * 0.5;
                    hv = (o->bbMax[v] - o->bbMin[v]) * 0.5;
                }
                glm::dvec3 du(0), dv(0);
                du[u] = hu;
                dv[v] = hv;
                glm::dvec3 q[4] = {c - du - dv, c + du - dv, c + du + dv, c - du + dv};
                glm::vec4 col(0.35f, 0.9f, 0.45f, 1.0f), fill(0.35f, 0.9f, 0.45f, 0.25f);
                for (int i = 0; i < 4; ++i) addLine(fd.overlayLines, W(q[i]), W(q[(i + 1) % 4]), col);
                for (int i : {0, 1, 2, 0, 2, 3}) fd.translucent.push_back({W(q[i]), fill});
            }
        }
    }
}
