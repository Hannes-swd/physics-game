// Werkstatt – Handwerker-Simulator
//
//   Werkstatt.exe                          Spiel mit Fenster
//   Werkstatt.exe --script befehle.txt     Befehle beim Start ausführen
//   Werkstatt.exe --send "info 3" ...      Befehle an ein laufendes Spiel schicken (Antwort als Text)
//   Werkstatt.exe --headless [--demo]      ohne Fenster: Befehle von --script oder Tastatur/Pipe
//   Werkstatt.exe --selftest               automatische Tests von Geometrie und Physik
//   Werkstatt.exe --shot bild.png [--frames N] [--view vier]   Bild speichern und beenden
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "App.h"
#include "Commands.h"
#include "Remote.h"
#include "Script.h"
#include "World.h"

int runSelfTest();
int runBench(const std::string& filter);

namespace {

class HeadlessHost : public CommandHost {
public:
    explicit HeadlessHost(World& w) : w_(w) {}
    World& world() override { return w_; }
    uint32_t selectedId() const override { return sel_.empty() ? 0 : sel_.back(); }
    std::vector<uint32_t> selectedIds() const override { return sel_; }
    void select(uint32_t id) override {
        sel_.clear();
        if (id) sel_.push_back(id);
    }
    void selectMany(const std::vector<uint32_t>& ids) override { sel_ = ids; }
    void requestQuit() override { quit = true; }
    bool quit = false;

private:
    World& w_;
    std::vector<uint32_t> sel_;
};

int runHeadless(const std::string& script, bool demo) {
    Physics physics;
    if (!physics.init()) return 1;
    int code = 0;
    {
        World world(physics, nullptr);
        world.setSimulating(true);  // ohne Fenster: Physik läuft, sobald Zeit vergeht (warte, sim)
        if (demo) world.demoScene();
        HeadlessHost host(world);
        auto exec = [&](std::string line) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);  // BOM (PowerShell, Editor)
            if (line.empty() || line[0] == '#') return;
            bool ok = true;
            std::string out = runScriptSync(host, line, &ok);
            std::printf("> %s\n%s", line.c_str(), out.c_str());
            std::fflush(stdout);
            if (!ok) code = 1;
        };
        std::string line;
        if (!script.empty()) {
            std::ifstream in(script);
            if (!in) {
                std::fprintf(stderr, "script not found: %s\n", script.c_str());
                return 2;
            }
            while (!host.quit && std::getline(in, line)) exec(line);
        } else {
            while (!host.quit && std::getline(std::cin, line)) exec(line);
        }
    }
    return code;
}

} // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    AppOptions opt;
    bool headless = false, demo = false, send = false;
    std::vector<std::string> sendCmds;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (send) {
            sendCmds.push_back(a);
        } else if (a == "--selftest") {
            return runSelfTest();
        } else if (a == "--bench") {
            return runBench(i + 1 < argc ? argv[i + 1] : "");
        } else if (a == "--headless") {
            headless = true;
        } else if (a == "--demo") {
            demo = true;
        } else if (a == "--send") {
            send = true;
        } else if (a == "--script") {
            opt.script = next();
        } else if (a == "--shot") {
            opt.screenshotPath = next();
        } else if (a == "--frames") {
            opt.screenshotFrames = std::max(2, std::atoi(next().c_str()));
        } else if (a == "--view") {
            opt.view = next();
        } else if (a == "--play") {
            opt.play = true;
        } else if (a == "--empty") {
            opt.emptyScene = true;
        } else if (a == "--validate") {
            opt.validation = true;
        } else if (a == "--port") {
            opt.port = std::atoi(next().c_str());
        } else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            return 2;
        }
    }
    if (send) return remoteClient(opt.port, sendCmds);
    if (headless) return runHeadless(opt.script, demo);

    // Per Doppelklick gestartet? Dann das leere Konsolenfenster ausblenden.
    DWORD procs[4];
    if (GetConsoleProcessList(procs, 4) == 1 && opt.screenshotPath.empty() && opt.script.empty()) FreeConsole();

    App app;
    return app.run(opt);
}
