// Tag und Nacht: die Uhr der Welt und die Befehle "time" und "sky" (Himmel: Sky.cpp, Shader: sky.frag)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "App.h"

namespace {

std::string fmtDeg(double d) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", d);
    return buf;
}

std::string skyLine(double hours) {
    const sky::State s = sky::at(hours);
    std::string out = "sun " + (s.sunAlt > 0 ? fmtDeg(s.sunAlt) + "° up" : fmtDeg(-s.sunAlt) + "° below the horizon");
    out += ", moon " + (s.moonAlt > 0 ? fmtDeg(s.moonAlt) + "° up" : fmtDeg(-s.moonAlt) + "° below the horizon");
    out += s.moonLight ? ", moonlight" : (s.sunAlt > -1.0 ? ", daylight" : "");
    if (s.stars > 0.05f) out += ", stars " + std::to_string((int)std::lround(s.stars * 100)) + "%";
    return out;
}

}  // namespace

void App::updateDayTime(double dt) {
    if (!dayRunning_ || pauseStopped_ || dayLength_ <= 0) return;
    dayHours_ = std::fmod(dayHours_ + dt * 24.0 / dayLength_, 24.0);
}

std::string App::timeCommand(const std::vector<std::string>& t) {
    const std::string cmd = t.empty() ? "time" : t[0];
    const std::string v = t.size() > 1 ? t[1] : "";
    auto state = [&] {
        char len[32];
        std::snprintf(len, sizeof(len), "%g", dayLength_ / 60.0);
        return "time " + sky::clock(dayHours_) + ", a day lasts " + len + " min, " + (dayRunning_ ? "running" : "stopped") +
               "\n" + skyLine(dayHours_) + (starLines_ ? "\nconstellation lines on" : "");
    };
    if (cmd == "sky") {
        if (v == "lines" || v == "constellations") {
            const std::string w = t.size() > 2 ? t[2] : "";
            if (w == "on" || w == "off") starLines_ = w == "on";
            else if (w.empty()) starLines_ = !starLines_;
            else return "Error: sky lines [on|off]";
            return std::string("constellation lines ") + (starLines_ ? "on" : "off");
        }
        if (!v.empty()) return "Error: sky | sky lines [on|off]";
        return state();
    }
    if (v.empty()) return state();
    if (netClient()) return "Error: only the host can change the time";
    if (v == "stop" || v == "freeze") {
        dayRunning_ = false;
    } else if (v == "run" || v == "start") {
        dayRunning_ = true;
    } else if (v == "length") {
        const double min = t.size() > 2 ? std::atof(t[2].c_str()) : 0.0;
        if (min < 0.2 || min > 14400) return "Error: time length <minutes per day>   (0.2 ... 14400; real time: 1440)";
        dayLength_ = min * 60.0;
    } else if (v == "add") {
        char* end = nullptr;
        const double h = t.size() > 2 ? std::strtod(t[2].c_str(), &end) : 0.0;
        if (t.size() < 3 || end == t[2].c_str()) return "Error: time add <hours>";
        dayHours_ = std::fmod(std::fmod(dayHours_ + h, 24.0) + 24.0, 24.0);
    } else {
        const std::string what = v == "set" && t.size() > 2 ? t[2] : v;
        double h = 0;
        if (!sky::parseTime(what, h))
            return "Error: time [<hh:mm>|dawn|sunrise|morning|noon|afternoon|sunset|dusk|night|midnight] | time add <hours> | "
                   "time stop|run | time length <minutes per day>";
        dayHours_ = h;
    }
    return state();
}
