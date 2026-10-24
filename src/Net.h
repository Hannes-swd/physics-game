// LAN-Mehrspieler: Verbindungen zwischen Gastgeber und Mitspielern (TCP) und Spielesuche im lokalen Netz (UDP-Broadcast).
// Nachricht = 4 Byte Länge + 1 Byte Art + Inhalt. Alles nicht blockierend, wird im Hauptthread abgefragt.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace net {

constexpr int kGamePort = 7780;        // TCP: Spiel
constexpr int kDiscoveryPort = 7781;   // UDP: Spiele im LAN finden
constexpr uint32_t kProtocol = 4;      // beide Seiten müssen dieselbe Version sprechen (3: Gelände, 4: Bäume)

enum class Msg : uint8_t {
    // Mitspieler -> Gastgeber
    Hello = 1,        // Version, Name
    Command = 2,      // Befehlszeile (läuft beim Gastgeber)
    Transforms = 3,   // Teile verschoben/gedreht (Bau-Modus)
    Input = 4,        // Spielfigur: Laufrichtung, Blick, Tasten
    // Gastgeber -> Mitspieler
    Welcome = 32,     // zugelassen: eigene Spielernummer, Name des Gastgebers
    Waiting = 33,     // Gastgeber muss den Beitritt noch bestätigen
    Reject = 34,      // abgelehnt/entfernt: Grund
    Object = 35,      // Teil komplett (neu oder neue Geometrie)
    ObjectMeta = 36,  // Name, Material, Lagerung ... eines Teils
    Remove = 37,      // Teile entfernt
    Poses = 38,       // Lage bewegter Teile
    Joints = 39,      // alle Verbindungen
    JointLoads = 40,  // Auslastung der Verbindungen
    Players = 41,     // alle Spielfiguren
    Tick = 42,        // Weltzustand: Zeit, Physik an/aus, Luft, Bruch, Rückgängig-Schritte
    Result = 43,      // Antwort auf einen Befehl
    Event = 44,       // Meldung (Bruch, Spieler kommt/geht)
    Terrain = 45,     // Gelände: Seed, Biome (Text von biomes.json), alle Änderungen; der Mitspieler erzeugt es selbst
};

class Writer {
public:
    void u8(uint8_t v) { raw(&v, 1); }
    void u32(uint32_t v) { raw(&v, 4); }
    void f32(float v) { raw(&v, 4); }
    void f64(double v) { raw(&v, 8); }
    void str(const std::string& s);
    void vec3(const glm::vec3& v);
    void dvec3(const glm::dvec3& v);
    void dquat(const glm::dquat& q);
    const std::string& data() const { return buf_; }

private:
    void raw(const void* p, size_t n) { buf_.append(static_cast<const char*>(p), n); }
    std::string buf_;
};

// Liest eine Nachricht; bei zu kurzen Daten liefert alles 0 und ok() wird false
class Reader {
public:
    explicit Reader(const std::string& s) : p_(s.data()), end_(s.data() + s.size()) {}
    uint8_t u8();
    uint32_t u32();
    float f32();
    double f64();
    std::string str();
    glm::vec3 vec3();
    glm::dvec3 dvec3();
    glm::dquat dquat();
    bool ok() const { return ok_; }
    size_t left() const { return (size_t)(end_ - p_); }
    void fail() { ok_ = false; }

private:
    bool raw(void* out, size_t n);
    const char* p_;
    const char* end_;
    bool ok_ = true;
};

class Connection {
public:
    Connection() = default;
    explicit Connection(uint64_t acceptedSocket);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Verbindungsaufbau beginnen (kehrt sofort zurück); connected() wird true, sobald er steht
    bool connectTo(const std::string& host, int port, std::string* err);
    void send(Msg type, const Writer& w);
    void send(Msg type);
    // Senden/Empfangen voranbringen. false = Verbindung beendet oder gescheitert.
    bool update();
    // Nächste vollständige Nachricht
    bool next(Msg& type, std::string& payload);
    bool connected() const { return sock_ != ~0ull && !connecting_ && !closed_; }
    bool closed() const { return closed_; }
    size_t pending() const { return out_.size(); }  // noch nicht gesendete Bytes
    std::string address() const { return address_; }
    void close();

private:
    uint64_t sock_ = ~0ull;
    std::string in_, out_;
    std::string address_;
    bool connecting_ = false, closed_ = false;
};

class Listener {
public:
    ~Listener() { stop(); }
    bool start(int port);
    void stop();
    bool running() const { return sock_ != ~0ull; }
    std::unique_ptr<Connection> accept();

private:
    uint64_t sock_ = ~0ull;
};

// Gastgeber: beantwortet Suchanfragen aus dem LAN
class Beacon {
public:
    ~Beacon() { stop(); }
    bool start();
    void stop();
    void answer(const std::string& name, int port, int players);

private:
    uint64_t sock_ = ~0ull;
};

struct FoundGame {
    std::string address;
    int port = kGamePort;
    std::string name;
    int players = 0;
    double seen = 0;
};

// Mitspieler: fragt im LAN nach offenen Spielen
class Search {
public:
    ~Search() { stop(); }
    void update(double now);  // fragt etwa jede Sekunde, sammelt Antworten
    void stop();
    const std::vector<FoundGame>& found() const { return found_; }

private:
    uint64_t sock_ = ~0ull;
    double lastPing_ = -100;
    std::vector<FoundGame> found_;
};

// IPv4-Adressen dieses Rechners im LAN (für die Anzeige beim Gastgeber)
std::vector<std::string> localAddresses();

} // namespace net
