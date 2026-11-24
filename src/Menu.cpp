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
#include <QWindow>

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "Keys.h"
#include "Net.h"
#include "Remote.h"
#include "Settings.h"
#include "WorldGen.h"
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

// Grafikstufe aus Name oder Zahl (low|niedrig|0 .. ultra|3); -1: unbekannt
int presetIndex(const std::string& v) {
    static const char* const de[kGraphicsPresets] = {"niedrig", "mittel", "hoch", "ultra"};
    for (int i = 0; i < kGraphicsPresets; ++i)
        if (v == kGraphicsPresetNames[i] || v == de[i] || v == std::to_string(i)) return i;
    return -1;
}

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

const char* kMenuHelp =
    "MAIN MENU (remote control or console; every command takes effect at once, without animations)\n"
    "  menu                                   where am I: page, open dialog, worlds (numbered), selection, settings\n"
    "  menu main|worlds|multiplayer|settings  switch to that page (also: menu page <name>, menu back);\n"
    "                                         settings tabs: menu settings|graphics|controls\n"
    "  menu select <n|name>                   select a world in the list (n = number from 'menu')\n"
    "  menu play [n|name]                     open the world (without: the selected one); the game window opens\n"
    "  menu lan [n|name]                      open the world and host a LAN game\n"
    "  menu new <name> [seed] [normal|flat] [all|<biome>]   create a world and select it (not opened). seed: number or\n"
    "                                         text, none = random; normal = terrain with biomes (all, or only one), flat = level floor\n"
    "  menu rename <n|name> <new name>        rename a world (names with spaces in \"quotes\")\n"
    "  menu delete <n|name>                   move the world to the recycle bin\n"
    "  menu games                             LAN games in the network (switches to page multiplayer, it searches there)\n"
    "  menu join <address|n|name>             join a LAN game (address like 192.168.1.20[:port], or number/name from 'menu games')\n"
    "  menu set <key> <value>                 name <text>, mouse <0.2-3>, fov <40-100>, shadows|edges|grid|borders on|off, autosave <minutes, 0 = never>\n"
    "                                         graphics low|medium|high|ultra (sets all of: msaa 1|2|4|8, supersampling on|off,\n"
    "                                         shadowquality low..ultra, view <0.5-2>, plants <0-1.5>), vsync on|off. Graphics apply\n"
    "                                         when a world opens\n"
    "  menu keys [reset]                      key bindings (action id, keys); reset: all back to the defaults\n"
    "  menu key <action> <key> [second key]   bind keys (A-Z, 0-9, F1-F12, Space, Shift, Ctrl, Alt, Tab, Up, Num0 ...);\n"
    "                                         'menu key <action> none' unbinds. Also works in the pause window\n"
    "  menu close                             close an open dialog\n"
    "  menu quit                              quit the program\n"
    "In the game: 'menu' tells where you are; 'menu <...>' saves the world, goes back to the main menu and runs the command there\n"
    "('menu worlds', 'menu play Island'); 'world menu' just goes back. Several commands: menu new Test ; menu play Test";

} // namespace

// Taste aus Qt (QML: event.key, event.modifiers) in den Namen von Keys.h
static std::string qtKeyName(int k, int mods) {
    const bool pad = (mods & Qt::KeypadModifier) != 0;
    if (k >= Qt::Key_A && k <= Qt::Key_Z) return std::string(1, (char)('A' + (k - Qt::Key_A)));
    if (k >= Qt::Key_0 && k <= Qt::Key_9) return (pad ? "Num" : "") + std::string(1, (char)('0' + (k - Qt::Key_0)));
    if (k >= Qt::Key_F1 && k <= Qt::Key_F12) return "F" + std::to_string(k - Qt::Key_F1 + 1);
    switch (k) {
    case Qt::Key_Space: return "Space";
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return "Tab";
    case Qt::Key_Return:
    case Qt::Key_Enter: return "Enter";
    case Qt::Key_Backspace: return "Backspace";
    case Qt::Key_Delete: return "Delete";
    case Qt::Key_Insert: return "Insert";
    case Qt::Key_Home: return "Home";
    case Qt::Key_End: return "End";
    case Qt::Key_PageUp: return "PageUp";
    case Qt::Key_PageDown: return "PageDown";
    case Qt::Key_Up: return "Up";
    case Qt::Key_Down: return "Down";
    case Qt::Key_Left: return "Left";
    case Qt::Key_Right: return "Right";
    case Qt::Key_Shift: return "Shift";
    case Qt::Key_Control: return "Ctrl";
    case Qt::Key_Alt: return "Alt";
    case Qt::Key_CapsLock: return "CapsLock";
    case Qt::Key_AsciiCircum:
    case Qt::Key_Dead_Circumflex: return "^";
    case Qt::Key_Comma: return "Comma";
    case Qt::Key_Period: return "Period";
    case Qt::Key_Minus: return "Minus";
    default: return "";
    }
}

// Das Objekt "menu" in QML
class MenuBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList worlds READ worlds NOTIFY worldsChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    // Einstellungen -> Steuerung: zwei Spalten, je eine Liste von Zeilen (kind "group" / "key" / "fixed")
    Q_PROPERTY(QVariantList keyColumns READ keyColumns NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList lanGames READ lanGames NOTIFY lanGamesChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString worldsFolder READ worldsFolder CONSTANT)
    // Biome aus assets/biomes.json (Auswahl "nur ein Biom" beim Erstellen): je {id, label}
    Q_PROPERTY(QVariantList biomes READ biomes CONSTANT)
    // Im Pausenfenster des Spiels (dort gelten die Einstellungen sofort)
    Q_PROPERTY(bool inGame READ inGame CONSTANT)
    // Zustand der Oberfläche: QML meldet Seite und Dialog, die Auswahl teilen sich QML und die Befehle
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(QString dialog READ dialog WRITE setDialog NOTIFY dialogChanged)
    Q_PROPERTY(QString selectedDir READ selectedDir WRITE setSelectedDir NOTIFY selectedDirChanged)

public:
    MenuBackend(const std::string& message, RemoteServer* remote, bool inGame = false)
        : message_(qs(message)), remote_(remote), inGame_(inGame) {
        std::string report;
        gen_ = loadBiomesFile(&report);  // Biome für neue Welten (die Welt bekommt eine Kopie)
        if (!report.empty() && message_.isEmpty()) message_ = qs(report);
        for (const GenConfig::Biome& b : gen_->biomes) {
            QVariantMap m;
            m["id"] = qs(b.id);
            m["label"] = qs(b.label);
            biomes_.push_back(m);
        }
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
    QVariantList keyColumns() const { return keyColumns_; }
    QVariantList lanGames() const { return lanGames_; }
    QString message() const { return message_; }
    QString worldsFolder() const { return qs(worlds::worldsDir().u8string()); }
    bool inGame() const { return inGame_; }
    void reloadSettings() { loadSettingsMap(); }  // settings.json neu lesen (das Spiel hat vielleicht etwas umgestellt)
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
            m["biome"] = qs(w.biome);
            m["typeText"] = qs(typeText(w));
            worlds_.push_back(m);
        }
        Q_EMIT worldsChanged();
    }
    QVariantList biomes() const { return biomes_; }
    // Zufälliger Seed fürs Feld "Seed" (die Welt ist damit schon festgelegt, man kann ihn noch ändern)
    Q_INVOKABLE QString randomSeed() const { return qs(worlds::randomSeedText()); }
    // Ergebnis: {error: "" oder Meldung, dir: Ordner der neuen Welt}. type "normal" oder "flat", biome "all" oder eine id,
    // cheats: Bau-Modus und alle Befehle erlaubt (sonst nur Spielen)
    Q_INVOKABLE QVariantMap createWorld(const QString& name, const QString& seed, const QString& type = "normal", const QString& biome = "all",
                                        bool cheats = false) {
        QVariantMap r;
        WorldInfo w;
        std::string err;
        worlds::CreateOptions opt;
        opt.generator = ss(type);
        opt.biome = ss(biome).empty() ? "all" : ss(biome);
        opt.generatorText = gen_->text;
        opt.cheats = cheats;
        if (opt.biome != "all" && gen_->findBiome(opt.biome) < 0) {
            r["error"] = qs("Unbekanntes Biom '" + opt.biome + "'.");
            return r;
        }
        if (!worlds::create(ss(name), ss(seed), opt, w, &err)) {
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
        else if (k == "biomeBorders") s.biomeBorders = value.toBool();
        else if (k == "autosaveMinutes") s.autosaveMinutes = std::clamp(value.toInt(), 0, 120);
        // Grafik: eine Stufe (0 Niedrig .. 3 Ultra) setzt alle Werte, einzelne Werte machen daraus "Eigene"
        else if (k == "graphicsPreset") applyGraphicsPreset(s, value.toInt());
        else if (k == "msaa") s.msaa = value.toInt() >= 8 ? 8 : value.toInt() >= 4 ? 4 : value.toInt() >= 2 ? 2 : 1;
        else if (k == "supersampling") s.supersampling = value.toBool();
        else if (k == "shadowQuality") s.shadowQuality = std::clamp(value.toInt(), 0, 3);
        else if (k == "viewDistance") s.viewDistance = std::clamp(value.toDouble(), 0.5, 2.0);
        else if (k == "plants") s.plants = std::clamp(value.toDouble(), 0.0, 1.5);
        else if (k == "vsync") s.vsync = value.toBool();
        else return;
        saveSettings(s);
        loadSettingsMap();
    }

    // ---- Tasten (Einstellungen -> Steuerung)
    // Name einer Taste, wie ihn settings.json und das Spiel kennen ("W", "Space", "Ctrl"); leer: diese Taste geht nicht
    Q_INVOKABLE QString keyName(int key, int modifiers) const { return qs(qtKeyName(key, modifiers)); }
    // Taste slot (0 / 1) der Aktion id setzen; name leer: diese Taste entfernen
    Q_INVOKABLE QString setKey(const QString& id, int slot, const QString& name) {
        keys::Act a;
        if (!keys::byId(ss(id), a)) return qs("Error: unknown action '" + ss(id) + "'");
        if (!name.isEmpty() && !keys::validKey(ss(name))) return qs("Error: unknown key '" + ss(name) + "'");
        GameSettings s = loadSettings();
        keys::Binding b = keys::binding(s.keys, a);
        b[(size_t)std::clamp(slot, 0, 1)] = ss(name);
        keys::setBinding(s.keys, a, b);
        saveSettings(s);
        loadSettingsMap();
        return QString();
    }
    Q_INVOKABLE void resetKeys() {
        GameSettings s = loadSettings();
        s.keys.clear();
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
        settings_["biomeBorders"] = s.biomeBorders;
        settings_["autosaveMinutes"] = s.autosaveMinutes;
        settings_["graphicsPreset"] = graphicsPresetOf(s);  // -1: eigene Werte
        settings_["msaa"] = s.msaa;
        settings_["supersampling"] = s.supersampling;
        settings_["shadowQuality"] = s.shadowQuality;
        settings_["viewDistance"] = s.viewDistance;
        settings_["plants"] = s.plants;
        settings_["vsync"] = s.vsync;
        loadKeyColumns(s);
        Q_EMIT settingsChanged();
    }
    // Zeilen der Seite Steuerung: links Bewegen, Fenster, Handeln, Überall; rechts Werkzeuge, Bau-Modus
    void loadKeyColumns(const GameSettings& s) {
        keyColumns_.clear();
        const std::vector<std::vector<std::string>> cols = {{"Bewegen", "Fenster", "Handeln", "Überall"}, {"Werkzeuge", "Bau-Modus"}};
        for (const auto& groups : cols) {
            QVariantList rows;
            for (const std::string& g : groups) {
                QVariantMap head;
                head["kind"] = QStringLiteral("group");
                head["label"] = qs(g);
                rows.push_back(head);
                for (int i = 0; i < keys::kActs; ++i) {
                    const keys::Act a = (keys::Act)i;
                    if (keys::info(a).group != g) continue;
                    const keys::Binding b = keys::binding(s.keys, a);
                    QVariantMap r;
                    r["kind"] = QStringLiteral("key");
                    r["id"] = qs(keys::info(a).id);
                    r["label"] = qs(keys::info(a).label);
                    QVariantList names, texts, conflicts;
                    for (int k = 0; k < 2; ++k) {
                        names.push_back(qs(b[k]));
                        texts.push_back(qs(b[k].empty() ? "" : keys::keyText(b[k])));
                        conflicts.push_back(qs(keys::conflict(s.keys, a, k)));
                    }
                    r["names"] = names;
                    r["texts"] = texts;
                    r["conflicts"] = conflicts;
                    rows.push_back(r);
                }
                for (const keys::FixedRow& f : keys::fixedRows()) {
                    if (f.group != g) continue;
                    QVariantMap r;
                    r["kind"] = QStringLiteral("fixed");
                    r["label"] = qs(f.label);
                    r["text"] = qs(f.keys);
                    rows.push_back(r);
                }
            }
            keyColumns_.push_back(rows);
        }
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

    std::string biomeIds() const {
        std::string s;
        for (const GenConfig::Biome& b : gen_->biomes) s += (s.empty() ? "" : ", ") + b.id;
        return s;
    }

    // "flach", "normal, alle Biome", "normal, nur Ebene"
    std::string typeText(const WorldInfo& w) const {
        if (w.generator != "normal") return "flach";
        if (w.biome == "all" || w.biome.empty()) return "normal, alle Biome";
        const int b = gen_->findBiome(w.biome);
        return "normal, nur " + (b >= 0 ? gen_->biomes[(size_t)b].label : w.biome);
    }

    std::string worldLine(size_t i) const {
        const WorldInfo& w = infos_[i];
        char num[8];
        std::snprintf(num, sizeof(num), "%3zu", i + 1);
        std::string name = quoted(w.name);
        if (name.size() < 26) name.resize(26, ' ');
        return std::string(qs(w.dir.u8string()) == selectedDir_ ? " >" : "  ") + num + "  " + name + "  last opened " +
               worlds::formatTime(w.lastPlayed) + ", played " + worlds::formatDuration(w.playTime) + ", seed " + std::to_string(w.seed) +
               ", " + (w.generator == "normal" ? "normal" + (w.biome == "all" ? std::string() : " (" + w.biome + ")") : std::string("flat")) +
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
          << ", grid " << (s.grid ? "on" : "off") << ", borders " << (s.biomeBorders ? "on" : "off") << ", autosave " << (s.autosaveMinutes ? std::to_string(s.autosaveMinutes) + " min" : "never")
          << "\n";
        const int preset = graphicsPresetOf(s);
        o << "graphics      " << (preset >= 0 ? kGraphicsPresetNames[preset] : "custom") << ": msaa " << s.msaa << ", supersampling "
          << (s.supersampling ? "on" : "off") << ", shadowquality " << kGraphicsPresetNames[s.shadowQuality] << ", view " << s.viewDistance
          << ", plants " << s.plants << ", vsync " << (s.vsync ? "on" : "off") << " (applies when a world opens)\n";
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
        if (sub == "steuerung" || sub == "tasten") sub = "controls";
        if (sub == "grafik") sub = "graphics";
        if (sub == "main" || sub == "worlds" || sub == "multiplayer" || sub == "settings" || sub == "graphics" || sub == "controls") {
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
            // menu new <name> [seed] [normal|flat] [all|<biome>]: nach dem Namen in beliebiger Reihenfolge
            if (arg(2).empty()) return "Error: menu new <name> [seed] [normal|flat] [all|<biome>]   (names with spaces in \"quotes\")";
            std::string seed, type = "normal", biome = "all";
            bool cheats = false;  // wie im Dialog: ohne Cheats, sonst nur Spielen
            for (size_t i = 3; i < t.size(); ++i) {
                const std::string a = lower(t[i]);
                if (a == "normal" || a == "flat" || a == "flach") type = a == "flach" ? "flat" : a;
                else if (a == "cheats" || a == "mit-cheats") cheats = true;
                else if (a == "nocheats" || a == "ohne-cheats") cheats = false;
                else if (a == "all" || a == "alle") biome = "all";
                else if (gen_->findBiome(a) >= 0) biome = a;
                else if (seed.empty()) seed = t[i];
                else return "Error: menu new: unknown option '" + t[i] + "' (normal, flat, all, biomes: " + biomeIds() + ")";
            }
            if (type == "flat" && biome != "all") return "Error: a flat world has no biomes (use 'normal " + biome + "')";
            if (seed.empty()) seed = worlds::randomSeedText();  // wie im Dialog: der Seed steht schon vorher fest
            Q_EMIT closeDialogs();
            const QVariantMap r = createWorld(qs(arg(2)), qs(seed), qs(type), qs(biome), cheats);
            if (!r["error"].toString().isEmpty()) return "Error: " + ss(r["error"].toString());
            if (page_ != "worlds") requestPage("worlds");
            setSelectedDir(r["dir"].toString());
            const int i = indexOf(selectedDir_);
            const WorldInfo& w = infos_[(size_t)i - 1];
            return "created world " + std::to_string(i) + " " + quoted(w.name) + ", seed " + std::to_string(w.seed) + ", " + typeText(w) +
                   (w.cheats ? ", mit Cheats" : ", ohne Cheats") + ", selected. 'menu play' opens it.";
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
        // Tasten: "menu keys" (Liste), "menu keys reset", "menu key <aktion> <taste> [zweite]", "menu key <aktion> none"
        if (sub == "keys" || sub == "tasten") {
            if (lower(arg(2)) == "reset") resetKeys();
            else if (!arg(2).empty()) return "Error: menu keys [reset]";
            const GameSettings s = loadSettings();
            std::ostringstream o;
            for (int i = 0; i < keys::kActs; ++i) {
                const keys::Binding b = keys::binding(s.keys, (keys::Act)i);
                o << keys::info((keys::Act)i).id << " " << (b[0].empty() ? "none" : b[0]) << (b[1].empty() ? "" : " " + b[1]);
                const std::string c = keys::conflict(s.keys, (keys::Act)i, 0);
                if (!c.empty()) o << "   (also: " << c << ")";
                o << "\n";
            }
            return o.str();
        }
        if (sub == "key" || sub == "taste") {
            keys::Act a;
            if (!keys::byId(arg(2), a) || arg(3).empty()) return "Error: menu key <action> <key> [second key] | menu key <action> none   ('menu keys' lists them)";
            const std::string k1 = lower(arg(3)) == "none" ? "" : arg(3), k2 = arg(4);
            for (const std::string& k : {k1, k2})
                if (!k.empty() && !keys::validKey(k)) return "Error: unknown key '" + k + "' (A-Z, 0-9, F1-F12, Space, Shift, Ctrl, Alt, Tab, Up, ...)";
            GameSettings s = loadSettings();
            keys::setBinding(s.keys, a, {k1, k2});
            saveSettings(s);
            loadSettingsMap();
            const keys::Binding b = keys::binding(s.keys, a);
            return std::string(keys::info(a).id) + " = " + (b[0].empty() ? "none" : b[0]) + (b[1].empty() ? "" : " " + b[1]);
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
            else if ((key == "borders" || key == "biomeborders") && parseBool(v, b)) setSetting("biomeBorders", b);
            else if ((key == "autosave" || key == "autosaveminutes") && parseNum(v, d)) setSetting("autosaveMinutes", (int)std::clamp(d, 0.0, 60.0));
            else if ((key == "graphics" || key == "quality" || key == "shadowquality") && presetIndex(lower(v)) >= 0)
                setSetting(key == "shadowquality" ? "shadowQuality" : "graphicsPreset", presetIndex(lower(v)));
            else if (key == "msaa" && parseNum(v, d)) setSetting("msaa", (int)d);
            else if ((key == "supersampling" || key == "vsync") && parseBool(v, b)) setSetting(qs(key), b);
            else if ((key == "view" || key == "viewdistance") && parseNum(v, d)) setSetting("viewDistance", d);
            else if ((key == "plants" || key == "grass") && parseNum(v, d)) setSetting("plants", d);
            else return "Error: menu set " + key + " <value>: unknown key or bad value (name <text>, mouse <0.2-3>, fov <40-100>, shadows|edges|grid on|off, autosave <minutes>, "
                       "graphics low|medium|high|ultra, msaa 1|2|4|8, supersampling on|off, shadowquality low..ultra, view <0.5-2>, plants <0-1.5>, vsync on|off)";
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
    QVariantList keyColumns_;
    QString message_, page_ = "main", dialog_, selectedDir_;
    std::shared_ptr<const GenConfig> gen_;  // Biome für neue Welten (assets/biomes.json)
    QVariantList biomes_;
    net::Search search_;
    QTimer searchTimer_, remoteTimer_;
    QElapsedTimer clock_;
    RemoteServer* remote_ = nullptr;
    bool leaving_ = false;
    bool inGame_ = false;
};

// Das Objekt "pause" in qml/PauseWindow.qml: was gewählt wurde, holt das Spiel mit PauseUi::poll() ab
class PauseBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool fromMenu READ fromMenu NOTIFY changed)  // "Zurück zum Hauptmenü" anbieten
    Q_PROPERTY(QString note READ note NOTIFY changed)       // Hinweis auf der Seite "Beenden"
    Q_PROPERTY(bool cheats READ cheats NOTIFY changed)      // "Modus wechseln" (Bau-Modus) nur mit Cheats

public:
    bool fromMenu() const { return fromMenu_; }
    QString note() const { return note_; }
    bool cheats() const { return cheats_; }
    void set(bool fromMenu, const QString& note, bool cheats) {
        fromMenu_ = fromMenu;
        note_ = note;
        cheats_ = cheats;
        Q_EMIT changed();
    }
    Q_INVOKABLE void resume() { action = PauseUi::Action::Resume; }
    Q_INVOKABLE void switchMode() { action = PauseUi::Action::SwitchMode; }
    Q_INVOKABLE void toMenu() { action = PauseUi::Action::ToMenu; }
    Q_INVOKABLE void quitGame() { action = PauseUi::Action::QuitGame; }

    PauseUi::Action action = PauseUi::Action::None;

Q_SIGNALS:
    void changed();

private:
    bool fromMenu_ = false;
    bool cheats_ = false;
    QString note_;
};

// Pausenfenster (PauseUi.h): rahmenlos über dem Spielfenster mit qml/PauseWindow.qml, bleibt nach dem ersten Öffnen bis zum Ende der
// Sitzung geladen (nur versteckt). Die Einstellungsseite ist dieselbe wie im Hauptmenü (MenuBackend ohne Fernsteuerung:
// die bedient im Spiel das Spiel)
class QtPause : public PauseUi {
public:
    ~QtPause() override { reset(); }

    void open(void* gameWindow, bool fromMenu, const std::string& note, bool cheats) override {
        if (!engine_) {
            backend_ = std::make_unique<MenuBackend>("", nullptr, true);
            pause_ = std::make_unique<PauseBackend>();
            engine_ = std::make_unique<QQmlApplicationEngine>();
            engine_->rootContext()->setContextProperty("menu", backend_.get());
            engine_->rootContext()->setContextProperty("pause", pause_.get());
            QObject::connect(backend_.get(), &MenuBackend::settingsChanged, [this] { settingsChanged_ = true; });
            engine_->addImportPath(QStringLiteral("qrc:/menu"));  // der Stil "Wald"
            engine_->load(QUrl(QStringLiteral("qrc:/menu/PauseWindow.qml")));
            if (engine_->rootObjects().isEmpty()) {  // QML-Fehler (stehen in der Konsole)
                reset();
                return;
            }
            window_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().first());
        }
        if (!window_) return;
        backend_->reloadSettings();
        settingsChanged_ = false;
        pause_->set(fromMenu, qs(note), cheats);
        pause_->action = Action::None;
        Q_EMIT backend_->pageRequested(QStringLiteral("pause"));
        // vor dem Spielfenster (gehört zu ihm: kein eigener Knopf in der Taskleiste), rahmenlos genau darüber
        window_->setTransientParent(nullptr);
        foreign_.reset(gameWindow ? QWindow::fromWinId((WId)gameWindow) : nullptr);
        if (foreign_) {
            window_->setTransientParent(foreign_.get());
            const QRect g = foreign_->geometry();
            if (g.isValid()) window_->setGeometry(g);  // deckt das Spielfenster ganz ab, das Menü steht in der QML mittig
        }
        window_->show();
        window_->raise();
        window_->requestActivate();
        open_ = true;
        QCoreApplication::processEvents();
    }
    void close() override {
        if (window_) window_->hide();
        open_ = false;
        QCoreApplication::processEvents();  // sonst verschwindet es erst beim nächsten Öffnen
    }
    bool isOpen() const override { return open_; }
    Action poll() override {
        if (!open_ || !pause_) return Action::None;
        QCoreApplication::processEvents();
        const Action a = pause_->action;
        pause_->action = Action::None;
        return a;
    }
    bool takeSettingsChanged() override {
        const bool c = settingsChanged_;
        settingsChanged_ = false;
        return c;
    }
    void showPage(const std::string& page) override {
        if (!backend_) return;
        Q_EMIT backend_->pageRequested(qs(page));
        QCoreApplication::processEvents();
    }
    std::string page() const override { return backend_ ? ss(backend_->page()) : ""; }
    std::string command(const std::string& line) override {
        if (!backend_) return "Error: the pause window is not open";
        const std::vector<std::string> t = words(line);
        if (t.size() < 2 || t[0] != "menu" || (t[1] != "set" && t[1] != "key" && t[1] != "keys"))
            return "Error: only 'menu set <key> <value>', 'menu key ...' and 'menu keys' work in the pause window";
        const std::string r = backend_->command(line);
        QCoreApplication::processEvents();
        return r;
    }
    bool grab(const std::string& file) override {
        if (!window_ || !open_) return false;
        QCoreApplication::processEvents();
        return window_->grabWindow().save(qs(file));
    }

    // Ende der Sitzung (das Hauptmenü kommt wieder): alles weg
    void reset() {
        window_ = nullptr;
        engine_.reset();  // schließt das Fenster; vor den Objekten, die es benutzt
        foreign_.reset();
        backend_.reset();
        pause_.reset();
        open_ = settingsChanged_ = false;
    }

private:
    std::unique_ptr<MenuBackend> backend_;
    std::unique_ptr<PauseBackend> pause_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    std::unique_ptr<QWindow> foreign_;  // das Spielfenster aus Sicht von Qt
    QQuickWindow* window_ = nullptr;
    bool open_ = false, settingsChanged_ = false;
};

// Zeichnung für QML (Inventar, Auswahl): Liste von Maps mit pts (x0 y0 x1 y1 … in 0 … 1) und color
static QVariantList polyList(const std::vector<CatalogUi::Poly>& polys) {
    QVariantList out;
    for (const CatalogUi::Poly& p : polys) {
        QVariantList pts;
        for (float f : p.pts) pts.push_back((double)f);
        QVariantMap m;
        m["pts"] = pts;
        m["color"] = qs(p.color);
        out.push_back(m);
    }
    return out;
}

// Das Objekt "bag" in qml/BagWindow.qml: ein Fenster für Inventar (Bereich 0), Herstellen (1) und Bauen (2).
// Inventar: Raster, Teile (Liste von Maps: key, x, y, w, h, name, info, polys = Zeichnung), Gewichtszeile.
// Herstellen / Bauen: ein Kasten je Vorlage (label, info, polys), der zuletzt gewählte. Tasten (E, C, B nach der
// Belegung in settings.json): die des offenen Bereichs schließt, eine andere wechselt. Was man tut, holt das Spiel
// über InventoryUi::poll() bzw. CatalogUi::poll() ab
class BagBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int tab READ tab NOTIFY tabChanged)
    Q_PROPERTY(QStringList keyTexts READ keyTexts NOTIFY tabChanged)  // Taste je Bereich zur Anzeige ("E")
    Q_PROPERTY(int cols READ cols NOTIFY inventoryChanged)
    Q_PROPERTY(int rows READ rows NOTIFY inventoryChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY inventoryChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY inventoryChanged)
    Q_PROPERTY(QVariantList tiles READ tiles NOTIFY catalogChanged)
    Q_PROPERTY(int current READ current NOTIFY catalogChanged)
    // Bereich 3 "Alles" (nur mit Cheats): jedes Ding zum Geben (Liste von Maps: id, label, group, kg)
    Q_PROPERTY(bool cheats READ cheats NOTIFY giveChanged)
    Q_PROPERTY(QVariantList giveList READ giveList NOTIFY giveChanged)

public:
    struct Ev {
        enum Kind { Close, Switch, TakeOut, Choose, Give } kind;
        int tab = 0;
        uint32_t key = 0;
        int index = -1;
        std::string id;  // Give
        double kg = 0;
        int count = 1;
        std::string shape;
    };
    std::vector<Ev> events;

    int tab() const { return tab_; }
    QStringList keyTexts() const { return keyTexts_; }
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    QVariantList items() const { return items_; }
    QString summary() const { return summary_; }
    QVariantList tiles() const { return tiles_; }
    int current() const { return current_; }
    bool cheats() const { return cheats_; }
    QVariantList giveList() const { return giveList_; }
    void setGiveList(bool cheats, const std::vector<InventoryUi::GiveEntry>& list) {
        cheats_ = cheats;
        giveList_.clear();
        for (const InventoryUi::GiveEntry& e : list) {
            QVariantMap m;
            m["id"] = qs(e.id);
            m["label"] = qs(e.label);
            m["group"] = qs(e.group);
            m["kg"] = e.kg;
            m["color"] = qs(e.color);
            m["color2"] = qs(e.color2);
            m["pattern"] = qs(e.pattern);
            m["roughness"] = e.roughness;
            m["metallic"] = e.metallic;
            m["polys"] = polyList(e.polys);
            giveList_.push_back(m);
        }
        Q_EMIT giveChanged();
    }
    // nur im Fenster umschalten (Inventar <-> Alles gehören beide zum Inventar)
    void showLocal(int t) {
        tab_ = t;
        Q_EMIT tabChanged();
    }

    void setTab(int t) {
        // Tasten der drei Bereiche neu lesen (man kann sie im Pausenfenster ändern)
        const GameSettings s = loadSettings();
        const keys::Act acts[3] = {keys::Act::Inventory, keys::Act::Craft, keys::Act::Build};
        keyTexts_.clear();
        for (int i = 0; i < 3; ++i) {
            tabKeys_[i] = keys::binding(s.keys, acts[i]);
            keyTexts_.push_back(qs(tabKeys_[i][0].empty() ? tabKeys_[i][1] : keys::keyText(tabKeys_[i][0])));
        }
        tab_ = t;
        Q_EMIT tabChanged();
    }
    void setInventory(int cols, int rows, const std::vector<InventoryUi::Item>& items, const std::string& summary) {
        cols_ = cols;
        rows_ = rows;
        items_.clear();
        for (const InventoryUi::Item& it : items) {
            QVariantMap m;
            m["key"] = (double)it.key;
            m["x"] = it.x;
            m["y"] = it.y;
            m["w"] = it.w;
            m["h"] = it.h;
            m["name"] = qs(it.name);
            m["info"] = qs(it.info);
            m["polys"] = polyList(it.polys);
            items_.push_back(m);
        }
        summary_ = qs(summary);
        Q_EMIT inventoryChanged();
    }
    void setCatalog(const std::vector<CatalogUi::Item>& items, int current) {
        current_ = current;
        tiles_.clear();
        for (const CatalogUi::Item& it : items) {
            QVariantMap m;
            m["label"] = qs(it.label);
            m["info"] = qs(it.info);
            m["polys"] = polyList(it.polys);
            tiles_.push_back(m);
        }
        Q_EMIT catalogChanged();
    }

    Q_INVOKABLE void close() { events.push_back({Ev::Close}); }
    Q_INVOKABLE void switchTo(int t) {
        if (t == tab_ || t < 0 || t > 3 || (t == 3 && !cheats_)) return;
        if ((tab_ == 0 || tab_ == 3) && (t == 0 || t == 3)) showLocal(t);  // Inventar <-> Alles: das Spiel merkt nichts
        else events.push_back({Ev::Switch, t});
    }
    Q_INVOKABLE void give(const QString& id, double kg, int count, const QString& shape) {
        Ev e{Ev::Give};
        e.id = ss(id);
        e.shape = ss(shape);
        e.kg = kg;
        e.count = std::clamp(count, 1, 50);
        events.push_back(e);
    }
    Q_INVOKABLE void takeOut(double key) { events.push_back({Ev::TakeOut, 0, (uint32_t)key}); }
    Q_INVOKABLE void choose(int index) { events.push_back({Ev::Choose, 0, 0, index}); }
    // Welcher Bereich zu dieser Taste gehört (QML: event.key, event.modifiers); -1: keiner
    Q_INVOKABLE int keyTab(int key, int modifiers) const {
        const std::string name = qtKeyName(key, modifiers);
        if (name.empty()) return -1;
        for (int i = 0; i < 3; ++i)
            if (tabKeys_[i][0] == name || tabKeys_[i][1] == name) return i;
        return -1;
    }

Q_SIGNALS:
    void tabChanged();
    void inventoryChanged();
    void catalogChanged();
    void giveChanged();

private:
    int tab_ = 0;
    QStringList keyTexts_;
    keys::Binding tabKeys_[3];
    int cols_ = 1, rows_ = 1;
    QVariantList items_;
    QString summary_;
    QVariantList tiles_;
    int current_ = -1;
    bool cheats_ = false;
    QVariantList giveList_;
};

// Das gemeinsame Fenster: wie das Pausenfenster rahmenlos über dem Spielfenster, nach dem ersten Öffnen nur versteckt.
// Wechselt man den Bereich, bleibt es offen (kein Flackern, die Maus bleibt frei)
class QtBag {
public:
    ~QtBag() { reset(); }

    void show(void* gameWindow, int tab) {
        if (!engine_) {
            backend_ = std::make_unique<BagBackend>();
            engine_ = std::make_unique<QQmlApplicationEngine>();
            engine_->rootContext()->setContextProperty("bag", backend_.get());
            backend_->setInventory(cols_, rows_, items_, summary_);
            backend_->setGiveList(cheats_, give_);
            engine_->addImportPath(QStringLiteral("qrc:/menu"));  // der Stil "Wald"
            engine_->load(QUrl(QStringLiteral("qrc:/menu/BagWindow.qml")));
            if (engine_->rootObjects().isEmpty()) {  // QML-Fehler (stehen in der Konsole)
                reset();
                return;
            }
            window_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().first());
        }
        if (!window_) return;
        backend_->events.clear();
        backend_->setTab(tab);
        if (!open_) {
            window_->setTransientParent(nullptr);
            foreign_.reset(gameWindow ? QWindow::fromWinId((WId)gameWindow) : nullptr);
            if (foreign_) {
                window_->setTransientParent(foreign_.get());
                const QRect g = foreign_->geometry();
                if (g.isValid()) window_->setGeometry(g);  // deckt das Spielfenster ganz ab, der Inhalt steht in der QML mittig
            }
            window_->show();
            window_->raise();
            window_->requestActivate();
            open_ = true;
        }
        QCoreApplication::processEvents();
    }
    void hide() {
        if (window_) window_->hide();
        open_ = false;
        QCoreApplication::processEvents();
    }
    bool open() const { return open_; }
    int tab() const { return backend_ ? backend_->tab() : -1; }
    void setInventory(int cols, int rows, const std::vector<InventoryUi::Item>& items, const std::string& summary) {
        cols_ = cols;
        rows_ = rows;
        items_ = items;
        summary_ = summary;
        if (backend_) backend_->setInventory(cols, rows, items, summary);
    }
    void setCatalog(const std::vector<CatalogUi::Item>& items, int current) {
        if (backend_) backend_->setCatalog(items, current);
    }
    void setGiveList(bool cheats, const std::vector<InventoryUi::GiveEntry>& list) {
        cheats_ = cheats;
        give_ = list;
        if (backend_) backend_->setGiveList(cheats, list);
    }
    void showArea(int tab) {
        if (backend_ && open_) backend_->showLocal(tab);
    }
    bool next(BagBackend::Ev& e) {
        if (!open_ || !backend_) return false;
        QCoreApplication::processEvents();
        if (backend_->events.empty()) return false;
        e = backend_->events.front();
        backend_->events.erase(backend_->events.begin());
        return true;
    }
    bool grab(const std::string& file) {
        if (!window_ || !open_) return false;
        QCoreApplication::processEvents();
        return window_->grabWindow().save(qs(file));
    }
    void reset() {
        window_ = nullptr;
        engine_.reset();
        foreign_.reset();
        backend_.reset();
        open_ = false;
    }

private:
    std::unique_ptr<BagBackend> backend_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    std::unique_ptr<QWindow> foreign_;
    QQuickWindow* window_ = nullptr;
    bool open_ = false;
    int cols_ = 1, rows_ = 1;
    std::vector<InventoryUi::Item> items_;
    std::string summary_;
    bool cheats_ = false;
    std::vector<InventoryUi::GiveEntry> give_;
};

// Inventar (InventoryUi.h): Bereich 0 des gemeinsamen Fensters
class QtInventory : public InventoryUi {
public:
    explicit QtInventory(QtBag& bag) : bag_(bag) {}
    void open(void* gameWindow) override { bag_.show(gameWindow, 0); }
    void close() override {
        if (isOpen()) bag_.hide();
    }
    bool isOpen() const override { return bag_.open() && (bag_.tab() == 0 || bag_.tab() == 3); }
    void setContents(int cols, int rows, const std::vector<Item>& items, const std::string& summary) override {
        bag_.setInventory(cols, rows, items, summary);
    }
    Event poll() override {
        BagBackend::Ev e{};
        if (!isOpen() || !bag_.next(e)) return {};
        switch (e.kind) {
        case BagBackend::Ev::Close: return {Event::Close};
        case BagBackend::Ev::TakeOut: return {Event::TakeOut, e.key};
        case BagBackend::Ev::Switch: return {Event::Switch, 0, e.tab};
        case BagBackend::Ev::Give: {
            Event g{Event::Give};
            g.id = e.id;
            g.kg = e.kg;
            g.count = e.count;
            g.shape = e.shape;
            return g;
        }
        default: return {};
        }
    }
    bool grab(const std::string& file) override { return bag_.grab(file); }
    void setGiveList(bool cheats, const std::vector<GiveEntry>& list) override { bag_.setGiveList(cheats, list); }
    void showArea(int tab) override { bag_.showArea(tab); }

private:
    QtBag& bag_;
};

// Herstellen und Bauen (CatalogUi.h): Bereiche 1 und 2 des gemeinsamen Fensters
class QtCatalog : public CatalogUi {
public:
    explicit QtCatalog(QtBag& bag) : bag_(bag) {}
    void open(void* gameWindow, bool craft, const std::vector<Item>& items, int current) override {
        bag_.setCatalog(items, current);
        bag_.show(gameWindow, craft ? 1 : 2);
    }
    void close() override {
        if (isOpen()) bag_.hide();
    }
    bool isOpen() const override { return bag_.open() && (bag_.tab() == 1 || bag_.tab() == 2); }
    Event poll() override {
        BagBackend::Ev e{};
        if (!isOpen() || !bag_.next(e)) return {};
        switch (e.kind) {
        case BagBackend::Ev::Close: return {Event::Close};
        case BagBackend::Ev::Choose: return {Event::Choose, e.index};
        case BagBackend::Ev::Switch: return {Event::Switch, -1, e.tab};
        default: return {};
        }
    }
    bool grab(const std::string& file) override { return bag_.grab(file); }

private:
    QtBag& bag_;
};

struct MainMenu::Impl {
    QGuiApplication app;
    // Das Fenster bleibt nach show() noch offen (Ladeseite), bis hide() kommt: dann ist das Spielfenster schon da
    std::unique_ptr<MenuBackend> backend;
    std::unique_ptr<QQmlApplicationEngine> engine;
    std::unique_ptr<QtPause> pause;  // Pausenfenster im Spiel (vor app abgebaut)
    // Ein Fenster im Spiel für Inventar, Herstellen und Bauen (vor den beiden Anschlüssen angelegt, nach ihnen abgebaut)
    std::unique_ptr<QtBag> bag;
    std::unique_ptr<QtInventory> inventory;  // Bereich Inventar
    std::unique_ptr<QtCatalog> catalog;      // Bereiche Herstellen und Bauen
    QtBag& theBag() {
        if (!bag) bag = std::make_unique<QtBag>();
        return *bag;
    }
    Impl(int& argc, char** argv) : app(argc, argv) {}
};

CatalogUi* MainMenu::catalogUi() {
    if (!impl_->catalog) impl_->catalog = std::make_unique<QtCatalog>(impl_->theBag());
    return impl_->catalog.get();
}

PauseUi* MainMenu::pauseUi() {
    if (!impl_->pause) impl_->pause = std::make_unique<QtPause>();
    return impl_->pause.get();
}

InventoryUi* MainMenu::inventoryUi() {
    if (!impl_->inventory) impl_->inventory = std::make_unique<QtInventory>(impl_->theBag());
    return impl_->inventory.get();
}

MainMenu::MainMenu(int& argc, char** argv) {
    // "Wald": eigener Stil (qml/Wald/, eingebettet als qrc:/menu/Wald), Baumstamm-Buttons, Holzbretter, Moos. Was er
    // nicht selbst gestaltet, kommt aus "Basic"
    QQuickStyle::setStyle("Wald");
    // Pause und Inventar liegen durchsichtig über dem Spiel (grau abgedunkelt): dafür brauchen die Fenster Alpha
    QQuickWindow::setDefaultAlphaBuffer(true);
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
    if (impl_->pause) impl_->pause->reset();  // das Pausenfenster der letzten Sitzung
    if (impl_->bag) impl_->bag->reset();  // Inventar / Herstellen / Bauen der letzten Sitzung
    impl_->backend = std::make_unique<MenuBackend>(message, remote);
    impl_->engine = std::make_unique<QQmlApplicationEngine>();
    MenuBackend& backend = *impl_->backend;
    QQmlApplicationEngine& engine = *impl_->engine;
    MenuChoice c;
    engine.rootContext()->setContextProperty("menu", &backend);
    engine.addImportPath(QStringLiteral("qrc:/menu"));  // der Stil "Wald"
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
