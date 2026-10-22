// Messen, wo die Rechenzeit bleibt: PROFILE("name") misst den Rest des Blocks. Befehl "profile" zeigt die Summen.
#pragma once
#include <chrono>
#include <string>

namespace prof {

void add(const char* name, double ms);
void frame(double ms);                 // ein Bild ist fertig (Gesamtzeit), für Bildzeiten und Ruckler
std::string report();                  // Tabelle seit dem letzten reset
void reset();
bool enabled();
void setEnabled(bool on);

class Scope {
public:
    explicit Scope(const char* name) : name_(name), t0_(std::chrono::steady_clock::now()) {}
    ~Scope() { add(name_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0_).count()); }

private:
    const char* name_;
    std::chrono::steady_clock::time_point t0_;
};

// Abschnitte nacheinander messen: mark("a") zählt die Zeit seit dem letzten mark (bzw. dem Anlegen) zu "a"
class Lap {
public:
    Lap() : t_(std::chrono::steady_clock::now()) {}
    void mark(const char* name) {
        const auto t = std::chrono::steady_clock::now();
        add(name, std::chrono::duration<double, std::milli>(t - t_).count());
        t_ = t;
    }

private:
    std::chrono::steady_clock::time_point t_;
};

} // namespace prof

#define PROFILE_CAT2(a, b) a##b
#define PROFILE_CAT(a, b) PROFILE_CAT2(a, b)
#define PROFILE(name) prof::Scope PROFILE_CAT(profScope_, __LINE__)(name)
