// Hauptmenü mit Qt Quick (siehe Menu.h). Die Oberfläche steht in qml/ (eingebettet als qrc:/menu/), hier nur das
// C++-Objekt "menu", das sie benutzt: Welten (Worlds.h), Einstellungen (Settings.h), die Suche nach LAN-Spielen und die
// Befehle "menu ..." der Fernsteuerung. Alles, was man mit der Maus kann, geht auch per Befehl, und zwar sofort (ohne
// Übergänge), damit eine KI das Menü ohne Bildschirmfotos bedienen kann.
#include "Menu.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "Net.h"
#include "Remote.h"
#include "Settings.h"
#include "Worlds.h"

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }  // UTF-8
std::string ss(const QString& s) { return s.toStdString(); }

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

// Wörter einer Befehlszeile, "in Anführungszeichen" bleibt zusammen
std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, any = false;
    for (char c : line) {
        if (c == '"') {
            quoted = !quoted;
            any = true;
        } else if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
            if (any) out.push_back(cur);
            cur.clear();
            any = false;
        } else {
            cur += c;
            any = true;
        }
    }
    if (any) out.push_back(cur);
    return out;
}

// Befehlsfolge an ";" (außerhalb von Anführungszeichen) teilen
std::vector<std::string> splitCommands(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') quoted = !quoted;
        if (c == ';' && !quoted) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

bool parseBool(const std::string& v, bool& out) {
    const std::string s = lower(v);
    if (s == "on" || s == "true" || s == "1" || s == "yes" || s == "an" || s == "ja") return out = true, true;
    if (s == "off" || s == "false" || s == "0" || s == "no" || s == "aus" || s == "nein") return out = false, true;
    return false;
}

bool parseNum(const std::string& v, double& out) {
    char* end = nullptr;
    out = std::strtod(v.c_str(), &end);
    return end && end != v.c_str() && *end == 0;
}

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

const char* kMenuHelp =
    "MAIN MENU (remote control or console; every command takes effect at once, without animations)\n"
    "  menu                                   where am I: page, open dialog, worlds (numbered), selection, settings\n"
    "  menu main|worlds|multiplayer|settings  switch to that page (also: menu page <name>, menu back)\n"
    "  menu select <n|name>                   select a world in the list (n = number from 'menu')\n"
    "  menu play [n|name]                     open the world (without: the selected one); the game window opens\n"
    "  menu lan [n|name]                      open the world and host a LAN game\n"
    "  menu new <name> [seed]                 create a world and select it (not opened); seed: number or text, none = random\n"
    "  menu rename <n|name> <new name>        rename a world (names with spaces in \"quotes\")\n"
    "  menu delete <n|name>                   move the world to the recycle bin\n"
    "  menu games                             LAN games in the network (switches to page multiplayer, it searches there)\n"
    "  menu join <address|n|name>             join a LAN game (address like 192.168.1.20[:port], or number/name from 'menu games')\n"
    "  menu set <key> <value>                 name <text>, mouse <0.2-3>, fov <40-100>, shadows|edges|grid on|off, autosave <minutes, 0 = never>\n"
    "  menu close                             close an open dialog\n"
    "  menu quit                              quit the program\n"
    "In the game: 'menu' tells where you are; 'menu <...>' saves the world, goes back to the main menu and runs the command there\n"
    "('menu worlds', 'menu play Island'); 'world menu' just goes back. Several commands: menu new Test ; menu play Test";

} // namespace

// Das Objekt "menu" in QML
class MenuBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList worlds READ worlds NOTIFY worldsChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList lanGames READ lanGames NOTIFY lanGamesChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString worldsFolder READ worldsFolder CONSTANT)
    // Zustand der Oberfläche: QML meldet Seite und Dialog, die Auswahl teilen sich QML und die Befehle
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(QString dialog READ dialog WRITE setDialog NOTIFY dialogChanged)
    Q_PROPERTY(QString selectedDir READ selectedDir WRITE setSelectedDir NOTIFY selectedDirChanged)

public:
    MenuBackend(const std::string& message, RemoteServer* remote) : message_(qs(message)), remote_(remote) {
        refresh();
        loadSettingsMap();
        connect(&searchTimer_, &QTimer::timeout, this, &MenuBackend::pollLan);
        clock_.start();
        if (remote_) {  // Fernsteuerung: oft nachsehen, die Antwort kommt sofort
            connect(&remoteTimer_, &QTimer::timeout, this, &MenuBackend::pollRemote);
            remoteTimer_.start(10);
        }
    }
    ~MenuBackend() override { search_.stop(); }

    MenuChoice choice;
    bool leaving() const { return leaving_; }

    QVariantList worlds() const { return worlds_; }
    QVariantMap settings() const { return settings_; }
    QVariantList lanGames() const { return lanGames_; }
    QString message() const { return message_; }
    QString worldsFolder() const { return qs(worlds::worldsDir().u8string()); }
    QString page() const { return page_; }
    QString dialog() const { return dialog_; }
    QString selectedDir() const { return selectedDir_; }
    void setPage(const QString& p) {
        if (p == page_) return;
        page_ = p;
        Q_EMIT pageChanged();
    }
    void setDialog(const QString& d) {
        if (d == dialog_) return;
        dialog_ = d;
        Q_EMIT dialogChanged();
    }
    void setSelectedDir(const QString& d) {
        if (d == selectedDir_) return;
        selectedDir_ = d;
        Q_EMIT selectedDirChanged();
    }
    void setMessage(const std::string& m) {
        message_ = qs(m);
        Q_EMIT messageChanged();
    }

    // ---- Welten
    Q_INVOKABLE void refresh() {
        infos_ = worlds::list();
        worlds_.clear();
        for (const WorldInfo& w : infos_) {
            QVariantMap m;
            m["dir"] = qs(w.dir.u8string());
            m["name"] = qs(w.name);
            m["broken"] = w.broken;
            m["lastPlayed"] = qs(worlds::formatTime(w.lastPlayed));
            m["created"] = qs(worlds::formatTime(w.created));
            m["playTime"] = qs(worlds::formatDuration(w.playTime));
            m["seed"] = qs(std::to_string(w.seed));
            m["seedText"] = qs(w.seedText);
            m["generator"] = qs(w.generator);
            worlds_.push_back(m);
        }
        Q_EMIT worldsChanged();
    }
    // Ergebnis: {error: "" oder Meldung, dir: Ordner der neuen Welt}
    Q_INVOKABLE QVariantMap createWorld(const QString& name, const QString& seed) {
        QVariantMap r;
        WorldInfo w;
        std::string err;
        if (!worlds::create(ss(name), ss(seed), w, &err)) {
            r["error"] = qs(err);
            return r;
        }
        refresh();
        r["error"] = QString();
        r["dir"] = qs(w.dir.u8string());
        return r;
    }
    Q_INVOKABLE QString renameWorld(const QString& dir, const QString& name) {
        std::string err;
        if (!worlds::rename(std::filesystem::u8path(ss(dir)), ss(name), &err)) return qs(err);
        refresh();
        return QString();
    }
    Q_INVOKABLE QString deleteWorld(const QString& dir) {
        std::string err;
        if (!worlds::remove(std::filesystem::u8path(ss(dir)), &err)) return qs(err);
        if (dir == selectedDir_) setSelectedDir(QString());
        refresh();
        return QString();
    }
    Q_INVOKABLE void openFolder(const QString& dir) { QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); }
    Q_INVOKABLE void play(const QString& dir, bool lan) {
        choice = MenuChoice();
        choice.kind = MenuChoice::Play;
        choice.worldDir = ss(dir);
        choice.lan = lan;
        leave();
    }

    // ---- Mehrspieler
    Q_INVOKABLE void join(const QString& address) {
        if (address.trimmed().isEmpty()) return;
        choice = MenuChoice();
        choice.kind = MenuChoice::Join;
        choice.address = ss(address.trimmed());
        leave();
    }
    // Solange die Seite "Mehrspieler" offen ist: offene Spiele im LAN suchen
    Q_INVOKABLE void setSearching(bool on) {
        if (on && !searchTimer_.isActive()) {
            pollLan();
            searchTimer_.start(300);
        } else if (!on) {
            searchTimer_.stop();
            search_.stop();
        }
    }

    // ---- Einstellungen: Schlüssel wie in settings (playerName, mouseSensitivity, fov, shadows, edges, grid, autosaveMinutes)
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value) {
        GameSettings s = loadSettings();
        const std::string k = ss(key);
        if (k == "playerName") s.playerName = worlds::cleanName(ss(value.toString()));
        else if (k == "mouseSensitivity") s.mouseSensitivity = std::clamp(value.toDouble(), 0.1, 5.0);
        else if (k == "fov") s.fovDeg = std::clamp(value.toDouble(), 30.0, 110.0);
        else if (k == "shadows") s.shadows = value.toBool();
        else if (k == "edges") s.edges = value.toBool();
        else if (k == "grid") s.grid = value.toBool();
        else if (k == "autosaveMinutes") s.autosaveMinutes = std::clamp(value.toInt(), 0, 120);
        else return;
        saveSettings(s);
        loadSettingsMap();
    }

    Q_INVOKABLE void clearMessage() { setMessage(""); }
    Q_INVOKABLE void quit() {
        choice = MenuChoice();
        leave();
    }

    // ---- Befehle (Fernsteuerung, im Spiel getippte "menu ..."). Antwort als Text, Fehler beginnen mit "Error"
    std::string command(const std::string& line) {
        std::string out;
        for (const std::string& part : splitCommands(line)) {
            const std::vector<std::string> t = words(part);
            if (t.empty() || t[0][0] == '#') continue;
            if (!out.empty() && out.back() != '\n') out += "\n";
            if (leaving_) {  // die Welt öffnet sich schon: der Rest gehört ins Spiel
                out += "Error: skipped '" + part + "' (the main menu is closing; send it as its own command)";
                continue;
            }
            out += run(t);
        }
        return out;
    }

Q_SIGNALS:
    void worldsChanged();
    void settingsChanged();
    void lanGamesChanged();
    void messageChanged();
    void pageChanged();
    void dialogChanged();
    void selectedDirChanged();
    void pageRequested(const QString& name);  // QML wechselt sofort (ohne Übergang)
    void closeDialogs();

private:
    void leave() {
        leaving_ = true;
        remoteTimer_.stop();
        QCoreApplication::exit(0);  // läuft die Schleife noch nicht (Befehl beim Start), prüft show() leaving()
    }

    void requestPage(const std::string& name) {
        Q_EMIT closeDialogs();
        Q_EMIT pageRequested(qs(name));
        setPage(qs(name));
    }

    void loadSettingsMap() {
        const GameSettings s = loadSettings();
        settings_.clear();
        settings_["playerName"] = qs(s.playerName);
        settings_["defaultName"] = qs(defaultPlayerName());
        settings_["mouseSensitivity"] = s.mouseSensitivity;
        settings_["fov"] = s.fovDeg;
        settings_["shadows"] = s.shadows;
        settings_["edges"] = s.edges;
        settings_["grid"] = s.grid;
        settings_["autosaveMinutes"] = s.autosaveMinutes;
        Q_EMIT settingsChanged();
    }
    void pollLan() {
        search_.update(clock_.elapsed() / 1000.0);
        QVariantList list;
        for (const net::FoundGame& g : search_.found()) {
            QVariantMap m;
            m["name"] = qs(g.name);
            m["address"] = qs(g.port == net::kGamePort ? g.address : g.address + ":" + std::to_string(g.port));
            m["players"] = g.players;
            list.push_back(m);
        }
        if (list != lanGames_) {  // nur bei Änderungen (sonst springt die Liste in QML jedes Mal neu)
            lanGames_ = list;
            Q_EMIT lanGamesChanged();
        }
    }
    void pollRemote() {
        remote_->poll([&](uint64_t client, const std::string& line) {
            if (leaving_) {  // schon auf dem Weg ins Spiel: dort beantworten
                remote_->hold(client, line);
                return;
            }
            remote_->respond(client, command(line));
        });
    }

    // Welt aus Nummer (1 = oben in der Liste), Name oder Ordnername (Groß/Klein egal); leer: die ausgewählte
    const WorldInfo* findWorld(const std::string& ref, std::string& err) const {
        if (ref.empty()) {
            for (const WorldInfo& w : infos_)
                if (qs(w.dir.u8string()) == selectedDir_) return &w;
            err = "Error: no world selected ('menu select <n|name>' or give one: 'menu play <n|name>')";
            return nullptr;
        }
        double n = 0;
        if (parseNum(ref, n) && n == (int)n) {
            if (n >= 1 && n <= (double)infos_.size()) return &infos_[(size_t)n - 1];
            err = "Error: there is no world number " + ref + " (" + std::to_string(infos_.size()) + " worlds, see 'menu')";
            return nullptr;
        }
        for (const WorldInfo& w : infos_)
            if (lower(w.name) == lower(ref) || lower(w.dir.filename().u8string()) == lower(ref)) return &w;
        err = "Error: no world called \"" + ref + "\" (see 'menu')";
        return nullptr;
    }
    int indexOf(const QString& dir) const {
        for (size_t i = 0; i < infos_.size(); ++i)
            if (qs(infos_[i].dir.u8string()) == dir) return (int)i + 1;
        return 0;
    }

    std::string worldLine(size_t i) const {
        const WorldInfo& w = infos_[i];
        char num[8];
        std::snprintf(num, sizeof(num), "%3zu", i + 1);
        std::string name = quoted(w.name);
        if (name.size() < 26) name.resize(26, ' ');
        return std::string(qs(w.dir.u8string()) == selectedDir_ ? " >" : "  ") + num + "  " + name + "  last opened " +
               worlds::formatTime(w.lastPlayed) + ", played " + worlds::formatDuration(w.playTime) + ", seed " + std::to_string(w.seed) +
               (w.broken ? "  BROKEN (can only be deleted)" : "");
    }

    std::string status() const {
        std::ostringstream o;
        o << "screen        main menu, page " << ss(page_) << "\n";
        o << "dialog        " << (dialog_.isEmpty() ? "none" : ss(dialog_)) << "\n";
        if (!message_.isEmpty()) o << "message       " << ss(message_) << "\n";
        o << "worlds        " << infos_.size() << (infos_.empty() ? " (none yet: 'menu new <name> [seed]')" : "") << "\n";
        for (size_t i = 0; i < infos_.size(); ++i) o << worldLine(i) << "\n";
        const int sel = indexOf(selectedDir_);
        o << "selected      " << (sel ? std::to_string(sel) + " " + quoted(infos_[(size_t)sel - 1].name) : std::string("none")) << "\n";
        if (searchTimer_.isActive()) {
            o << "lan games     " << lanGames_.size() << " found (searching while page multiplayer is open)\n";
            for (int i = 0; i < lanGames_.size(); ++i) {
                const QVariantMap g = lanGames_[i].toMap();
                o << "   " << i + 1 << "  " << quoted(ss(g["name"].toString())) << " at " << ss(g["address"].toString()) << ", "
                  << g["players"].toInt() << " player(s)\n";
            }
        }
        const GameSettings s = loadSettings();
        o << "settings      name " << quoted(s.playerName.empty() ? defaultPlayerName() + " (default)" : s.playerName) << ", mouse "
          << s.mouseSensitivity << ", fov " << s.fovDeg << ", shadows " << (s.shadows ? "on" : "off") << ", edges " << (s.edges ? "on" : "off")
          << ", grid " << (s.grid ? "on" : "off") << ", autosave " << (s.autosaveMinutes ? std::to_string(s.autosaveMinutes) + " min" : "never")
          << "\n";
        o << "commands      'menu help'";
        return o.str();
    }

    std::string run(const std::vector<std::string>& t) {
        const std::string first = lower(t[0]);
        if (first == "help" || first == "?") return kMenuHelp;
        if (first == "status") return status();
        if (first == "quit" || first == "exit") return run({"menu", "quit"});
        if (first != "menu")
            return "Error: this is the main menu, only 'menu ...' commands work here ('menu help'). Open a world with 'menu play <n|name>', "
                   "then all game commands work.";
        std::string sub = t.size() > 1 ? lower(t[1]) : "";
        auto arg = [&](size_t i) { return i < t.size() ? t[i] : std::string(); };
        std::string err;

        // Seiten (auch deutsch)
        if (sub == "page" || sub == "seite") sub = lower(arg(2));
        if (sub == "start" || sub == "hauptmenü" || sub == "back" || sub == "zurück") sub = "main";
        if (sub == "spielen" || sub == "welten" || sub == "list") sub = "worlds";
        if (sub == "mehrspieler" || sub == "lan-games") sub = "multiplayer";
        if (sub == "einstellungen" || sub == "options") sub = "settings";
        if (sub == "main" || sub == "worlds" || sub == "multiplayer" || sub == "settings") {
            requestPage(sub);
            return status();
        }
        if (sub.empty() || sub == "status") return status();
        if (sub == "help") return kMenuHelp;

        if (sub == "select") {
            const WorldInfo* w = findWorld(arg(2), err);
            if (!w) return arg(2).empty() ? "Error: menu select <n|name>" : err;
            if (page_ != "worlds") requestPage("worlds");
            setSelectedDir(qs(w->dir.u8string()));
            return "selected " + std::to_string(indexOf(selectedDir_)) + " " + quoted(w->name);
        }
        if (sub == "play" || sub == "open" || sub == "lan" || sub == "host") {
            const WorldInfo* w = findWorld(arg(2), err);
            if (!w) return err;
            if (w->broken) return "Error: " + quoted(w->name) + " is broken (world.json unreadable); it can only be deleted";
            setSelectedDir(qs(w->dir.u8string()));
            const bool lan = sub == "lan" || sub == "host";
            const std::string name = w->name;
            play(selectedDir_, lan);
            return "opening world " + quoted(name) + (lan ? " as LAN host" : "") +
                   ". The game window opens; then all game commands work ('world', 'status', 'help'). 'world menu' comes back here.";
        }
        if (sub == "new" || sub == "create") {
            if (arg(2).empty()) return "Error: menu new <name> [seed]   (names with spaces in \"quotes\")";
            Q_EMIT closeDialogs();
            const QVariantMap r = createWorld(qs(arg(2)), qs(arg(3)));
            if (!r["error"].toString().isEmpty()) return "Error: " + ss(r["error"].toString());
            if (page_ != "worlds") requestPage("worlds");
            setSelectedDir(r["dir"].toString());
            const int i = indexOf(selectedDir_);
            const WorldInfo& w = infos_[(size_t)i - 1];
            return "created world " + std::to_string(i) + " " + quoted(w.name) + ", seed " + std::to_string(w.seed) +
                   (w.seedText.empty() ? " (random)" : "") + ", selected. 'menu play' opens it.";
        }
        if (sub == "rename") {
            const WorldInfo* w = findWorld(arg(2), err);
            if (!w) return arg(2).empty() ? "Error: menu rename <n|name> <new name>" : err;
            if (arg(3).empty()) return "Error: menu rename <n|name> <new name>   (names with spaces in \"quotes\")";
            Q_EMIT closeDialogs();
            const QString dir = qs(w->dir.u8string());
            const QString e = renameWorld(dir, qs(arg(3)));
            if (!e.isEmpty()) return "Error: " + ss(e);
            return "renamed to " + quoted(worlds::cleanName(arg(3)));
        }
        if (sub == "delete" || sub == "remove") {
            const WorldInfo* w = findWorld(arg(2), err);
            if (!w) return arg(2).empty() ? "Error: menu delete <n|name>" : err;
            Q_EMIT closeDialogs();
            const std::string name = w->name;
            const QString e = deleteWorld(qs(w->dir.u8string()));
            if (!e.isEmpty()) return "Error: " + ss(e);
            return "moved " + quoted(name) + " to the recycle bin";
        }
        if (sub == "games") {
            if (page_ != "multiplayer") requestPage("multiplayer");  // die Seite sucht
            pollLan();
            std::ostringstream o;
            o << lanGames_.size() << " LAN game(s) found" << (lanGames_.isEmpty() ? " so far (it keeps searching while this page is open, ask again in a second)" : "")
              << "\n";
            for (int i = 0; i < lanGames_.size(); ++i) {
                const QVariantMap g = lanGames_[i].toMap();
                o << "   " << i + 1 << "  " << quoted(ss(g["name"].toString())) << " at " << ss(g["address"].toString()) << ", "
                  << g["players"].toInt() << " player(s)\n";
            }
            return o.str();
        }
        if (sub == "join") {
            std::string a = arg(2);
            if (a.empty()) return "Error: menu join <address|n|name>   (e.g. 192.168.1.20 or a number from 'menu games')";
            double n = 0;
            if (parseNum(a, n) && n == (int)n && n >= 1 && n <= lanGames_.size()) {
                a = ss(lanGames_[(int)n - 1].toMap()["address"].toString());
            } else {
                for (const QVariant& v : lanGames_)
                    if (lower(ss(v.toMap()["name"].toString())) == lower(a)) a = ss(v.toMap()["address"].toString());
            }
            join(qs(a));
            return "joining the LAN game at " + a + ". The game window opens; if it fails you come back here ('menu' shows the message).";
        }
        if (sub == "set") {
            const std::string key = lower(arg(2)), v = arg(3);
            if (key.empty() || (v.empty() && key != "name"))
                return "Error: menu set <key> <value>   (name, mouse, fov, shadows, edges, grid, autosave)";
            double d = 0;
            bool b = false;
            if (key == "name" || key == "playername") setSetting("playerName", qs(v));
            else if ((key == "mouse" || key == "mousesensitivity") && parseNum(v, d)) setSetting("mouseSensitivity", std::clamp(d, 0.2, 3.0));
            else if (key == "fov" && parseNum(v, d)) setSetting("fov", std::clamp(d, 40.0, 100.0));
            else if ((key == "shadows" || key == "edges" || key == "grid") && parseBool(v, b)) setSetting(qs(key), b);
            else if ((key == "autosave" || key == "autosaveminutes") && parseNum(v, d)) setSetting("autosaveMinutes", (int)std::clamp(d, 0.0, 60.0));
            else return "Error: menu set " + key + " <value>: unknown key or bad value (name <text>, mouse <0.2-3>, fov <40-100>, shadows|edges|grid on|off, autosave <minutes>)";
            const std::string s = status();
            return s.substr(s.find("settings      "), s.find("\ncommands") - s.find("settings      "));
        }
        if (sub == "close") {
            const std::string was = ss(dialog_);
            Q_EMIT closeDialogs();
            setDialog(QString());
            return was.empty() ? "no dialog was open" : "closed dialog \"" + was + "\"";
        }
        if (sub == "quit" || sub == "exit") {
            quit();
            return "quitting";
        }
        return "Error: unknown menu command '" + t[1] + "' ('menu help')";
    }

    QVariantList worlds_, lanGames_;
    std::vector<WorldInfo> infos_;
    QVariantMap settings_;
    QString message_, page_ = "main", dialog_, selectedDir_;
    net::Search search_;
    QTimer searchTimer_, remoteTimer_;
    QElapsedTimer clock_;
    RemoteServer* remote_ = nullptr;
    bool leaving_ = false;
};

struct MainMenu::Impl {
    QGuiApplication app;
    // Das Fenster bleibt nach show() noch offen (Ladeseite), bis hide() kommt: dann ist das Spielfenster schon da
    std::unique_ptr<MenuBackend> backend;
    std::unique_ptr<QQmlApplicationEngine> engine;
    Impl(int& argc, char** argv) : app(argc, argv) {}
};

MainMenu::MainMenu(int& argc, char** argv) {
    // "Basic": schlichte Grundform der Qt Quick Controls, lässt sich später vollständig selbst gestalten
    QQuickStyle::setStyle("Basic");
    impl_ = std::make_unique<Impl>(argc, argv);
    QGuiApplication::setApplicationName("Werkstatt");
    // Das Menüfenster wird zwischendurch geschlossen (das Spiel läuft): das darf Qt nicht als "Programm beenden" werten.
    // Schließt man es selbst (X), ruft Main.qml menu.quit() auf
    QGuiApplication::setQuitOnLastWindowClosed(false);
}

MainMenu::~MainMenu() { hide(); }

void MainMenu::hide() {
    impl_->engine.reset();  // schließt das Fenster
    impl_->backend.reset();
}

MenuChoice MainMenu::show(const std::string& message, RemoteServer* remote, const std::string& command) {
    hide();
    impl_->backend = std::make_unique<MenuBackend>(message, remote);
    impl_->engine = std::make_unique<QQmlApplicationEngine>();
    MenuBackend& backend = *impl_->backend;
    QQmlApplicationEngine& engine = *impl_->engine;
    MenuChoice c;
    engine.rootContext()->setContextProperty("menu", &backend);
    engine.load(QUrl(QStringLiteral("qrc:/menu/Main.qml")));
    if (engine.rootObjects().isEmpty()) return c;  // QML-Fehler (stehen in der Konsole): beenden
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    backend.setSelectedDir(qs(lastWorld_));
    if (!command.empty()) {  // im Spiel getippt ("menu worlds"): Fehler erscheinen als Meldung auf der Startseite
        const std::string r = backend.command(command);
        if (r.rfind("Error", 0) == 0) backend.setMessage(r);
    }
    if (!shotFile_.empty()) {  // --menu-shot: Seite öffnen, kurz warten (Layout, erstes Bild), Bild speichern, beenden
        QMetaObject::invokeMethod(window, "openPage", Q_ARG(QVariant, QVariant(qs(shotPage_))), Q_ARG(QVariant, QVariant(true)));
        if (shotPage_ == "loading")
            QMetaObject::invokeMethod(window, "showLoading", Q_ARG(QVariant, QVariant(QStringLiteral("Welt wird geladen"))),
                                      Q_ARG(QVariant, QVariant(QStringLiteral("Beispielwelt"))));
        QTimer::singleShot(1200, [&] {
            const bool ok = window && window->grabWindow().save(qs(shotFile_));
            std::fprintf(ok ? stdout : stderr, ok ? "menu screenshot: %s\n" : "menu screenshot failed: %s\n", shotFile_.c_str());
            backend.quit();
        });
    }
    if (!backend.leaving()) impl_->app.exec();  // bis play/join/quit oder das Fenster geschlossen wird
    c = backend.choice;
    if (c.kind == MenuChoice::Play) lastWorld_ = c.worldDir;
    backend.setSearching(false);
    if (c.kind == MenuChoice::Quit) {
        hide();
        return c;
    }
    // Ladeseite zeigen und warten, bis sie wirklich auf dem Bildschirm ist. Das Fenster bleibt so stehen, bis das
    // Spielfenster sein erstes Bild hat (main.cpp ruft dann hide()): kein leerer Moment beim Wechsel
    std::string name;
    if (c.kind == MenuChoice::Play)
        for (const QVariant& v : backend.worlds())
            if (v.toMap()["dir"].toString() == qs(c.worldDir)) name = ss(v.toMap()["name"].toString());
    const QString heading = c.kind == MenuChoice::Join ? QStringLiteral("LAN-Spiel beitreten") : QStringLiteral("Welt wird geladen");
    QMetaObject::invokeMethod(window, "showLoading", Q_ARG(QVariant, QVariant(heading)),
                              Q_ARG(QVariant, QVariant(qs(c.kind == MenuChoice::Join ? c.address : name))));
    QEventLoop wait;
    QObject::connect(window, &QQuickWindow::frameSwapped, &wait, &QEventLoop::quit, Qt::QueuedConnection);
    QTimer::singleShot(500, &wait, &QEventLoop::quit);
    window->requestUpdate();
    wait.exec();
    return c;
}

#include "Menu.moc"
