#include "Profile.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace prof {
namespace {

struct Entry {
    const char* name;
    double total = 0, max = 0;
    long long count = 0;
    double win = 0;  // in der laufenden Sekunde (recent)
};

struct State {
    std::vector<Entry> entries;
    double frames = 0, frameTotal = 0, frameMax = 0;
    long long slow = 0;  // Bilder über 50 ms
    bool on = true;
    std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    // laufende Sekunde und die letzte fertige
    double winFrames = 0, winTotal = 0, winWorst = 0;
    std::chrono::steady_clock::time_point winStart = std::chrono::steady_clock::now();
    Recent recent;
};

State& st() {
    static State s;
    return s;
}

} // namespace

void add(const char* name, double ms) {
    State& s = st();
    if (!s.on) return;
    for (Entry& e : s.entries)  // wenige Einträge: linear ist schneller als eine Tabelle
        if (e.name == name || std::strcmp(e.name, name) == 0) {
            e.total += ms;
            e.max = std::max(e.max, ms);
            ++e.count;
            e.win += ms;
            return;
        }
    s.entries.push_back({name, ms, ms, 1, ms});
}

void frame(double ms) {
    State& s = st();
    if (!s.on) return;
    s.frames += 1;
    s.frameTotal += ms;
    s.frameMax = std::max(s.frameMax, ms);
    if (ms > 50) ++s.slow;
    s.winFrames += 1;
    s.winTotal += ms;
    s.winWorst = std::max(s.winWorst, ms);
    const auto now = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(now - s.winStart).count();
    if (secs < 1.0) return;
    Recent r;
    r.fps = s.winFrames / secs;
    r.frameMs = s.winTotal / s.winFrames;
    r.worstMs = s.winWorst;
    for (Entry& e : s.entries) {
        if (e.win > 0) r.sections.emplace_back(e.name, e.win / s.winFrames);
        e.win = 0;
    }
    std::sort(r.sections.begin(), r.sections.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    s.recent = std::move(r);
    s.winFrames = s.winTotal = s.winWorst = 0;
    s.winStart = now;
}

const Recent& recent() { return st().recent; }

void reset() {
    State& s = st();
    s.entries.clear();
    s.frames = s.frameTotal = s.frameMax = 0;
    s.slow = 0;
    s.since = std::chrono::steady_clock::now();
}

bool enabled() { return st().on; }
void setEnabled(bool on) { st().on = on; }

std::string report() {
    State& s = st();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.since).count();
    std::vector<Entry> e = s.entries;
    std::sort(e.begin(), e.end(), [](const Entry& a, const Entry& b) { return a.total > b.total; });
    char buf[256];
    std::string out;
    std::snprintf(buf, sizeof(buf), "profile over %.1f s", secs);
    out += buf;
    if (s.frames > 0) {
        std::snprintf(buf, sizeof(buf), ", %.0f frames: %.1f ms per frame (%.0f fps), slowest %.0f ms, %lld frames over 50 ms", s.frames,
                      s.frameTotal / s.frames, 1000.0 * s.frames / std::max(s.frameTotal, 1e-9), s.frameMax, s.slow);
        out += buf;
    }
    out += "\n  section                          calls     total ms   ms/s    ms/call   max ms\n";
    for (const Entry& x : e) {
        std::snprintf(buf, sizeof(buf), "  %-30s %8lld %11.1f %7.1f %10.3f %8.1f\n", x.name, x.count, x.total, x.total / std::max(secs, 1e-9),
                      x.total / std::max<long long>(x.count, 1), x.max);
        out += buf;
    }
    return out;
}

} // namespace prof
