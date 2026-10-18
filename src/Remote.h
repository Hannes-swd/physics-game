// Fernsteuerung des laufenden Spiels über eine lokale TCP-Verbindung (nur 127.0.0.1).
// Protokoll: eine Zeile pro Befehl(sfolge); Antwort = Textzeilen, danach eine Zeile "<<end>>".
// Die Antwort kann später kommen (z.B. nach "warte 2"), das Spiel läuft solange weiter.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

constexpr const char* kRemoteEnd = "<<end>>";

class RemoteServer {
public:
    ~RemoteServer();
    bool start(int port);
    void stop();
    // Neue Zeilen abholen (im Hauptthread). onLine wird pro Client erst wieder gerufen,
    // wenn die vorige Zeile mit respond() beantwortet wurde.
    void poll(const std::function<void(uint64_t client, const std::string& line)>& onLine);
    void respond(uint64_t client, const std::string& text);
    bool running() const { return listen_ != ~0ull; }

private:
    struct Client {
        unsigned long long sock;
        uint64_t id;
        std::string buffer;
        bool busy = false;
        bool closed = false;
    };
    unsigned long long listen_ = ~0ull;
    std::vector<Client> clients_;
    uint64_t nextId_ = 1;
};

// Schickt Befehle an ein laufendes Spiel und gibt die Antworten aus. Rückgabe: Exit-Code.
int remoteClient(int port, const std::vector<std::string>& commands);
