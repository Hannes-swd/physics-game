// Werkstatt – Handwerker-Simulator
//
//   Werkstatt.exe                          Hauptmenü (Weltliste, Mehrspieler, Einstellungen), dann das Spiel
//   Werkstatt.exe --world <Name|Ordner>    diese Welt direkt öffnen
//   Werkstatt.exe --no-menu                ohne Hauptmenü in die Beispiel-Werkstatt (wie früher)
//   Werkstatt.exe --menu-shot bild.png [main|worlds|multiplayer|settings]   Bild des Hauptmenüs speichern und beenden
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
#include "Worlds.h"
#ifdef WERKSTATT_MENU
#include "Menu.h"
#endif

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

// --world: Ordner oder Name einer Welt aus der Weltliste
bool findWorld(const std::string& what, std::string& dir) {
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::u8path(what);
    if (std::filesystem::is_regular_file(p / "world.json", ec)) {
        dir = std::filesystem::absolute(p, ec).u8string();
        return true;
    }
    for (const WorldInfo& w : worlds::list())
        if (w.name == what || w.dir.filename().u8string() == what) {
            dir = w.dir.u8string();
            return true;
        }
    return false;
}

#ifdef WERKSTATT_MENU
// Hauptmenü -> Spiel -> (Zurück zum Hauptmenü) -> Hauptmenü ... Jede Sitzung hat ihr eigenes Spielfenster.
// Die Fernsteuerung läuft die ganze Zeit (Menü und Spiel teilen sie): eine KI bleibt verbunden und kann mit "menu ..."
// das Menü bedienen und mit allen anderen Befehlen das Spiel
int runWithMenu(int& argc, char** argv, const AppOptions& base) {
    MainMenu menu(argc, argv);
    RemoteServer remote;
    if (!remote.start(base.port)) std::fprintf(stderr, "remote control not available (port %d in use)\n", base.port);
    std::string message, command;
    for (;;) {
        const MenuChoice c = menu.show(message, remote.running() ? &remote : nullptr, command);
        if (c.kind == MenuChoice::Quit) return 0;
        AppOptions opt = base;
        opt.menu = true;
        opt.remote = &remote;
        opt.worldDir = c.kind == MenuChoice::Play ? c.worldDir : "";
        opt.lanHost = c.kind == MenuChoice::Play && c.lan;
        opt.joinAddress = c.kind == MenuChoice::Join ? c.address : "";
        auto app = std::make_unique<App>();
        const int code = app->run(opt);
        message = app->exitMessage();
        command = app->menuCommand();  // im Spiel getippt: "menu worlds" usw.
        if (!app->backToMenu()) return code;
    }
}
#endif

} // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    AppOptions opt;
    bool headless = false, demo = false, send = false, noMenu = false;
    std::string menuShot, menuShotPage;  // --menu-shot bild.png [seite]
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
        } else if (a == "--menu-shot") {
            menuShot = next();
            if (i + 1 < argc && argv[i + 1][0] != '-') menuShotPage = argv[++i];
        } else if (a == "--no-menu") {
            noMenu = true;
        } else if (a == "--world") {
            const std::string w = next();
            if (!findWorld(w, opt.worldDir)) {
                std::fprintf(stderr, "world not found: %s (folder or name in %s)\n", w.c_str(), worlds::worldsDir().u8string().c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            return 2;
        }
    }
    if (send) return remoteClient(opt.port, sendCmds);
    if (headless) return runHeadless(opt.script, demo);

    // Per Doppelklick gestartet? Dann das leere Konsolenfenster ausblenden.
    DWORD procs[4];
    if (GetConsoleProcessList(procs, 4) == 1 && opt.screenshotPath.empty() && opt.script.empty() && menuShot.empty()) FreeConsole();

#ifdef WERKSTATT_MENU
    // Ohne Angaben: Hauptmenü. Mit --script, --shot, --play, --empty, --view, --world (Tests, Fernsteuerung) direkt ins Spiel
    const bool direct = noMenu || !opt.script.empty() || !opt.screenshotPath.empty() || opt.play || opt.emptyScene || !opt.view.empty() ||
                        !opt.worldDir.empty();
    if (!menuShot.empty()) {
        MainMenu menu(argc, argv);
        menu.setScreenshot(menuShot, menuShotPage);
        menu.show("");
        return 0;
    }
    if (!direct) return runWithMenu(argc, argv, opt);
#else
    (void)noMenu;
    if (!menuShot.empty()) {
        std::fprintf(stderr, "built without the main menu (Qt)\n");
        return 2;
    }
#endif
    App app;
    return app.run(opt);
}
