#include "Remote.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

namespace {

bool wsaInit() {
    static bool done = false;
    if (done) return true;
    WSADATA d;
    done = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    return done;
}

void sendAll(SOCKET s, const std::string& data) {
    size_t off = 0;
    int tries = 0;
    while (off < data.size() && tries < 2000) {
        int n = send(s, data.data() + off, (int)(data.size() - off), 0);
        if (n > 0) {
            off += (size_t)n;
        } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
            Sleep(1);
            ++tries;
        } else {
            return;
        }
    }
}

} // namespace

RemoteServer::~RemoteServer() { stop(); }

bool RemoteServer::start(int port) {
    if (!wsaInit()) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 4) != 0) {
        closesocket(s);
        return false;
    }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    listen_ = (unsigned long long)s;
    return true;
}

void RemoteServer::stop() {
    for (auto& c : clients_) closesocket((SOCKET)c.sock);
    clients_.clear();
    if (listen_ != ~0ull) closesocket((SOCKET)listen_);
    listen_ = ~0ull;
}

void RemoteServer::poll(const std::function<void(uint64_t, const std::string&)>& onLine) {
    if (listen_ == ~0ull) return;
    for (;;) {
        SOCKET c = accept((SOCKET)listen_, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        u_long nb = 1;
        ioctlsocket(c, FIONBIO, &nb);
        Client cl;
        cl.sock = (unsigned long long)c;
        cl.id = nextId_++;
        clients_.push_back(cl);
    }
    for (size_t i = 0; i < clients_.size(); ++i) {
        char buf[4096];
        for (;;) {
            int n = recv((SOCKET)clients_[i].sock, buf, sizeof(buf), 0);
            if (n > 0) {
                clients_[i].buffer.append(buf, (size_t)n);
            } else if (n == 0) {
                clients_[i].closed = true;
                break;
            } else {
                if (WSAGetLastError() != WSAEWOULDBLOCK) clients_[i].closed = true;
                break;
            }
        }
        // Nächste Zeile nur, wenn die vorige beantwortet ist (respond() kann sofort aus onLine kommen)
        size_t nl;
        while (!clients_[i].busy && (nl = clients_[i].buffer.find('\n')) != std::string::npos) {
            std::string line = clients_[i].buffer.substr(0, nl);
            clients_[i].buffer.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            clients_[i].busy = true;
            onLine(clients_[i].id, line);
        }
    }
    for (size_t i = 0; i < clients_.size();) {
        if (clients_[i].closed && !clients_[i].busy) {
            closesocket((SOCKET)clients_[i].sock);
            clients_.erase(clients_.begin() + i);
        } else {
            ++i;
        }
    }
}

void RemoteServer::respond(uint64_t client, const std::string& text) {
    for (Client& cl : clients_) {
        if (cl.id != client) continue;
        std::string resp = text;
        if (!resp.empty() && resp.back() != '\n') resp += '\n';
        if (!cl.closed) sendAll((SOCKET)cl.sock, resp + kRemoteEnd + "\n");
        cl.busy = false;
        return;
    }
}

int remoteClient(int port, const std::vector<std::string>& commandsIn) {
    if (!wsaInit()) return 2;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::fprintf(stderr, "No connection to the game (port %d). Is Werkstatt.exe running?\n", port);
        closesocket(s);
        return 2;
    }
    std::vector<std::string> commands = commandsIn;
    if (commands.empty()) {
        std::string line;
        while (std::getline(std::cin, line)) commands.push_back(line);
    }
    int code = 0;
    std::string pending;
    for (const std::string& cmd : commands) {
        sendAll(s, cmd + "\n");
        std::printf("> %s\n", cmd.c_str());
        for (;;) {
            size_t pos = pending.find(std::string(kRemoteEnd) + "\n");
            if (pos != std::string::npos) {
                std::string resp = pending.substr(0, pos);
                pending.erase(0, pos + std::string(kRemoteEnd).size() + 1);
                std::fwrite(resp.data(), 1, resp.size(), stdout);
                if (resp.rfind("Error", 0) == 0 || resp.find("\nError") != std::string::npos) code = 1;
                break;
            }
            char buf[4096];
            int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0) {
                std::fprintf(stderr, "connection closed\n");
                closesocket(s);
                return 2;
            }
            pending.append(buf, (size_t)n);
        }
    }
    closesocket(s);
    return code;
}
