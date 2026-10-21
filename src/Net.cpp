#include "Net.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <cstring>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace net {

namespace {

constexpr uint32_t kMaxMessage = 512u << 20;  // größere Längenangaben = kaputte Verbindung
const char* kAsk = "WERKSTATT?";
const char* kAnswer = "WERKSTATT!";

bool wsaInit() {
    static bool done = false;
    if (done) return true;
    WSADATA d;
    done = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    return done;
}

void nonBlocking(SOCKET s) {
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
}

void noDelay(SOCKET s) {  // kleine Nachrichten (Eingaben, Lagen) sofort senden
    BOOL on = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(on));
}

std::string ipText(const in_addr& a) {
    char buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

// IPv4-Schnittstellen, die gerade verbunden sind: (Adresse, Netzmaske) in Host-Byte-Reihenfolge
std::vector<std::pair<uint32_t, uint32_t>> interfaces() {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf;
    ULONG r = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && r == ERROR_BUFFER_OVERFLOW; ++tries) {
        buf.resize(size);
        r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                 reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (r != NO_ERROR) return out;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
            if ((ip >> 16) == 0xA9FE) continue;  // 169.254.x.x: kein echtes Netz
            const ULONG bits = std::min<ULONG>(u->OnLinkPrefixLength, 32);
            uint32_t mask = bits == 0 ? 0 : 0xFFFFFFFFu << (32 - bits);
            out.emplace_back(ip, mask);
        }
    }
    return out;
}

} // namespace

// ============================================================================ Nachrichten

void Writer::str(const std::string& s) {
    u32((uint32_t)s.size());
    raw(s.data(), s.size());
}

void Writer::vec3(const glm::vec3& v) {
    f32(v.x);
    f32(v.y);
    f32(v.z);
}

void Writer::dvec3(const glm::dvec3& v) {
    f64(v.x);
    f64(v.y);
    f64(v.z);
}

void Writer::dquat(const glm::dquat& q) {
    f64(q.w);
    f64(q.x);
    f64(q.y);
    f64(q.z);
}

bool Reader::raw(void* out, size_t n) {
    if (!ok_ || (size_t)(end_ - p_) < n) {
        ok_ = false;
        std::memset(out, 0, n);
        return false;
    }
    std::memcpy(out, p_, n);
    p_ += n;
    return true;
}

uint8_t Reader::u8() {
    uint8_t v;
    raw(&v, 1);
    return v;
}

uint32_t Reader::u32() {
    uint32_t v;
    raw(&v, 4);
    return v;
}

float Reader::f32() {
    float v;
    raw(&v, 4);
    return v;
}

double Reader::f64() {
    double v;
    raw(&v, 8);
    return v;
}

std::string Reader::str() {
    uint32_t n = u32();
    if (!ok_ || (size_t)(end_ - p_) < n) {
        ok_ = false;
        return {};
    }
    std::string s(p_, n);
    p_ += n;
    return s;
}

glm::vec3 Reader::vec3() {
    glm::vec3 v;
    v.x = f32();
    v.y = f32();
    v.z = f32();
    return v;
}

glm::dvec3 Reader::dvec3() {
    glm::dvec3 v;
    v.x = f64();
    v.y = f64();
    v.z = f64();
    return v;
}

glm::dquat Reader::dquat() {
    glm::dquat q;
    q.w = f64();
    q.x = f64();
    q.y = f64();
    q.z = f64();
    return q;
}

// ============================================================================ Verbindung

Connection::Connection(uint64_t acceptedSocket) : sock_(acceptedSocket) {
    SOCKET s = (SOCKET)sock_;
    nonBlocking(s);
    noDelay(s);
    sockaddr_in a{};
    int len = sizeof(a);
    if (getpeername(s, (sockaddr*)&a, &len) == 0) address_ = ipText(a.sin_addr);
}

Connection::~Connection() { close(); }

bool Connection::connectTo(const std::string& host, int port, std::string* err) {
    close();
    closed_ = false;
    in_.clear();
    out_.clear();
    if (!wsaInit()) {
        if (err) *err = "network not available";
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        if (err) *err = "address not found: " + host;
        return false;
    }
    sockaddr_in addr = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
    freeaddrinfo(res);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        if (err) *err = "no socket";
        return false;
    }
    nonBlocking(s);
    noDelay(s);
    address_ = ipText(addr.sin_addr);
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(s);
        if (err) *err = "connection refused";
        return false;
    }
    sock_ = (uint64_t)s;
    connecting_ = true;
    return true;
}

void Connection::send(Msg type, const Writer& w) {
    if (closed_) return;
    const uint32_t len = (uint32_t)w.data().size() + 1;
    out_.append(reinterpret_cast<const char*>(&len), 4);
    out_.push_back((char)type);
    out_ += w.data();
}

void Connection::send(Msg type) { send(type, Writer()); }

bool Connection::update() {
    if (closed_ || sock_ == ~0ull) return false;
    SOCKET s = (SOCKET)sock_;
    if (connecting_) {
        fd_set wr, ex;
        FD_ZERO(&wr);
        FD_ZERO(&ex);
        FD_SET(s, &wr);
        FD_SET(s, &ex);
        timeval tv{0, 0};
        if (select(0, nullptr, &wr, &ex, &tv) < 0 || FD_ISSET(s, &ex)) {
            close();
            return false;
        }
        if (!FD_ISSET(s, &wr)) return true;  // steht noch nicht
        int e = 0, len = sizeof(e);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&e, &len);
        if (e != 0) {
            close();
            return false;
        }
        connecting_ = false;
    }
    size_t sent = 0;
    while (sent < out_.size()) {
        int n = ::send(s, out_.data() + sent, (int)std::min<size_t>(out_.size() - sent, 1u << 20), 0);
        if (n > 0) {
            sent += (size_t)n;
        } else {
            if (WSAGetLastError() == WSAEWOULDBLOCK) break;
            closed_ = true;
            break;
        }
    }
    out_.erase(0, sent);
    char buf[64 * 1024];
    while (!closed_) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n > 0) {
            in_.append(buf, (size_t)n);
        } else {
            if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) break;
            closed_ = true;  // Gegenseite hat beendet; schon empfangene Nachrichten bleiben lesbar
        }
    }
    return !closed_;
}

bool Connection::next(Msg& type, std::string& payload) {
    if (in_.size() < 4) return false;
    uint32_t len;
    std::memcpy(&len, in_.data(), 4);
    if (len == 0 || len > kMaxMessage) {
        in_.clear();
        close();
        return false;
    }
    if (in_.size() < 4 + (size_t)len) return false;
    type = (Msg)(uint8_t)in_[4];
    payload.assign(in_, 5, len - 1);
    in_.erase(0, 4 + (size_t)len);
    return true;
}

void Connection::close() {
    if (sock_ != ~0ull) {
        // Was schon im Puffer liegt (z.B. "abgelehnt"), noch kurz versuchen loszuwerden
        if (!out_.empty() && !connecting_) ::send((SOCKET)sock_, out_.data(), (int)std::min<size_t>(out_.size(), 1u << 16), 0);
        shutdown((SOCKET)sock_, SD_BOTH);
        closesocket((SOCKET)sock_);
    }
    sock_ = ~0ull;
    out_.clear();
    connecting_ = false;
    closed_ = true;
}

// ============================================================================ Gastgeber: Verbindungen annehmen

bool Listener::start(int port) {
    stop();
    if (!wsaInit()) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    BOOL excl = TRUE;  // kein zweites Programm auf demselben Port
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // ganzes LAN, nicht nur dieser Rechner
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 8) != 0) {
        closesocket(s);
        return false;
    }
    nonBlocking(s);
    sock_ = (uint64_t)s;
    return true;
}

void Listener::stop() {
    if (sock_ != ~0ull) closesocket((SOCKET)sock_);
    sock_ = ~0ull;
}

std::unique_ptr<Connection> Listener::accept() {
    if (sock_ == ~0ull) return nullptr;
    SOCKET c = ::accept((SOCKET)sock_, nullptr, nullptr);
    if (c == INVALID_SOCKET) return nullptr;
    return std::make_unique<Connection>((uint64_t)c);
}

// ============================================================================ Spielesuche im LAN

bool Beacon::start() {
    stop();
    if (!wsaInit()) return false;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)kDiscoveryPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(s);
        return false;
    }
    nonBlocking(s);
    sock_ = (uint64_t)s;
    return true;
}

void Beacon::stop() {
    if (sock_ != ~0ull) closesocket((SOCKET)sock_);
    sock_ = ~0ull;
}

void Beacon::answer(const std::string& name, int port, int players) {
    if (sock_ == ~0ull) return;
    char buf[512];
    for (;;) {
        sockaddr_in from{};
        int len = sizeof(from);
        int n = recvfrom((SOCKET)sock_, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &len);
        if (n <= 0) break;
        buf[n] = 0;
        if (std::strncmp(buf, kAsk, std::strlen(kAsk)) != 0) continue;
        std::string reply = std::string(kAnswer) + " " + std::to_string(kProtocol) + " " + std::to_string(port) + " " +
                            std::to_string(players) + " " + name;
        sendto((SOCKET)sock_, reply.data(), (int)reply.size(), 0, (sockaddr*)&from, len);
    }
}

void Search::update(double now) {
    if (!wsaInit()) return;
    if (sock_ == ~0ull) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return;
        BOOL on = TRUE;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(on));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesocket(s);
            return;
        }
        nonBlocking(s);
        sock_ = (uint64_t)s;
    }
    SOCKET s = (SOCKET)sock_;
    if (now - lastPing_ >= 1.0) {
        lastPing_ = now;
        std::string ask = std::string(kAsk) + std::to_string(kProtocol);
        // An alle im Netz (255.255.255.255), zusätzlich an jedes eigene Teilnetz und an diesen Rechner selbst
        std::vector<uint32_t> targets = {0xFFFFFFFFu, 0x7F000001u};
        for (auto& [ip, mask] : interfaces()) targets.push_back(ip | ~mask);
        for (uint32_t t : targets) {
            sockaddr_in to{};
            to.sin_family = AF_INET;
            to.sin_port = htons((u_short)kDiscoveryPort);
            to.sin_addr.s_addr = htonl(t);
            sendto(s, ask.data(), (int)ask.size(), 0, (sockaddr*)&to, sizeof(to));
        }
    }
    char buf[512];
    for (;;) {
        sockaddr_in from{};
        int len = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &len);
        if (n <= 0) break;
        buf[n] = 0;
        std::istringstream in(buf);
        std::string tag, name;
        uint32_t protocol = 0;
        int port = 0, players = 0;
        in >> tag >> protocol >> port >> players;
        std::getline(in, name);
        if (tag != kAnswer || port <= 0) continue;
        if (!name.empty() && name[0] == ' ') name.erase(0, 1);
        if (protocol != kProtocol) name += " (andere Version)";
        std::string address = ipText(from.sin_addr);
        auto it = std::find_if(found_.begin(), found_.end(), [&](const FoundGame& f) { return f.address == address && f.port == port; });
        if (it == found_.end()) {
            found_.push_back({address, port, name, players, now});
        } else {
            it->name = name;
            it->players = players;
            it->seen = now;
        }
    }
    // Spiel auf diesem Rechner antwortet doppelt (127.0.0.1 und LAN-Adresse): nur die LAN-Adresse zeigen
    auto loopbackTwin = [&](const FoundGame& f) {
        if (f.address.rfind("127.", 0) != 0) return false;
        for (const FoundGame& g : found_)
            if (&g != &f && g.port == f.port && g.name == f.name && g.address.rfind("127.", 0) != 0) return true;
        return false;
    };
    std::vector<FoundGame> keep;
    for (const FoundGame& f : found_)
        if (now - f.seen <= 3.5 && !loopbackTwin(f)) keep.push_back(f);
    found_ = keep;
}

void Search::stop() {
    if (sock_ != ~0ull) closesocket((SOCKET)sock_);
    sock_ = ~0ull;
    found_.clear();
    lastPing_ = -100;
}

std::vector<std::string> localAddresses() {
    std::vector<std::string> out;
    if (!wsaInit()) return out;
    for (auto& [ip, mask] : interfaces()) {
        in_addr a{};
        a.s_addr = htonl(ip);
        out.push_back(ipText(a));
    }
    return out;
}

} // namespace net
