// Symbole im Stil „Gravur“ (siehe Icons.h): Formen als SVG-Pfade im 24er-Raster (x nach rechts, y nach unten),
// beim ersten Gebrauch in Linienzüge zerlegt. Scharfe Ecken werden aufgetrennt und rund abgeschlossen, damit dicke
// Striche dort keine Spitzen bekommen.
#include "Icons.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace icons {
namespace {

constexpr float kPi = 3.14159265f;

enum class Kind { Line, Thin, Fill, Dash };

struct El {
    const char* d;
    float rot = 0;  // Drehung in Grad um die Mitte (12, 12), wie SVG rotate(): positiv im Uhrzeigersinn
    Kind kind = Kind::Line;
};

struct Piece {
    std::vector<ImVec2> pts;
    bool closed = false;
    bool capStart = false, capEnd = false;  // runde Enden
    Kind kind = Kind::Line;
};

struct Poly {
    std::vector<ImVec2> pts;
    bool closed = false;
};

// Formen (dieselben wie im Entwurf)
const char* const HAND =
    "M7 13V6a1.5 1.5 0 0 1 3 0v5V4.5a1.5 1.5 0 0 1 3 0V11V5.5a1.5 1.5 0 0 1 3 0V11V8a1.5 1.5 0 0 1 3 0v7a7 7 0 0 1-7 7h-1.2"
    "c-2.2 0-3.7-.8-5-2.1L3.3 16.3a1.45 1.45 0 0 1 2.05-2.05L7 15.9Z";
const char* const AXEHEAD = "M12.9 6.2L16.9 9.2L21.8 9.3Q20.4 13.9 16.4 16.5L14.9 11.8L10.9 8.8Z";
const char* const BLADE = "M8.5 12H15.5V16.5Q15.5 20 12 22.5Q8.5 20 8.5 16.5Z";
const char* const PICK = "M2 9.6Q12 -0.2 22 9.6Q12 4.4 2 9.6Z";
const char* const ROCK = "M15.4 11.2L17 9.7L19.3 10.3L19.8 12.8L18 14.4L15.8 13.7Z";
const char* const FLAME =
    "M12 2.8C14.6 6.6 18.2 9 18.2 14.2A6.2 6.2 0 0 1 5.8 14.2C5.8 11.2 7.4 9.4 8.9 7.9C9.2 9.9 10.2 11 11.3 11.4"
    "C10.7 8.4 10.9 5.6 12 2.8Z";
const char* const DROP = "M12 2.8C15.2 7.4 18.2 10.6 18.2 14.8A6.2 6.2 0 0 1 5.8 14.8C5.8 10.6 8.8 7.4 12 2.8Z";
const char* const BONE =
    "M11.2 9.9H7.7A2.6 2.6 0 1 0 5.1 12A2.6 2.6 0 1 0 7.7 14.1H10.4L11.6 12.1Z"
    "M12.6 9.9H16.3A2.6 2.6 0 1 1 18.9 12A2.6 2.6 0 1 1 16.3 14.1H11.8L13 12.1Z";

std::vector<El> elements(Id id) {
    switch (id) {
    case Id::Hand: return {{HAND}};
    case Id::Axe:  // Holzstiel, Bartaxt-Kopf, kleiner Stielüberstand, Schliff an der Schneide
        return {{"M4.5 21L12.5 10.3"}, {AXEHEAD}, {"M14.5 7.4L15 6.7"}, {"M19.6 10.6Q18.8 13 17.2 14.3", 0, Kind::Thin}};
    case Id::Laser:  // Gerät mit Griff, Düse, Strahl und Lichtpunkt, schräg nach oben rechts
        return {{"M3.5 9.5H10.5A1.5 1.5 0 0 1 12 11V13A1.5 1.5 0 0 1 10.5 14.5H3.5A1.5 1.5 0 0 1 2 13V11A1.5 1.5 0 0 1 3.5 9.5Z", -45},
                {"M4.6 14.5L4 18.6H7.2L8 14.5", -45},
                {"M12 10.8H13.8V13.2H12", -45},
                {"M15.8 12H19.4", -45},
                {"M20 12A1.1 1.1 0 1 0 22.2 12A1.1 1.1 0 1 0 20 12Z", -45},
                {"M6.5 11.4V12.6M8.6 11.4V12.6", -45, Kind::Thin}};
    case Id::Drill:  // Erdbohrer: Quergriff, Schaft mit Wendel, Spitze, Boden
        return {{"M6 4H18M6 2.8V5.2M18 2.8V5.2"},
                {"M12 4V19.5"},
                {"M8.8 10.4L15.2 12.2M8.8 13.4L15.2 15.2M8.8 16.4L15.2 18.2"},
                {"M10.4 19.5L12 22.5L13.6 19.5"},
                {"M2.5 17H7M17 17H21.5", 0, Kind::Thin}};
    case Id::Shovel:  // Schaufel: D-Griff, Stiel, Blatt mit Grat
        return {{"M9.5 2.5H14.5M10 2.5L12 5L14 2.5", -45}, {"M12 5V12", -45}, {BLADE, -45}, {"M12 14.2V19.2", -45, Kind::Thin}};
    case Id::Tractor:  // Strahler, Kegel, Pfeile zum Gerät hin, Brocken im Strahl
        return {{"M1.8 12A2.8 2.8 0 1 0 7.4 12A2.8 2.8 0 1 0 1.8 12Z"},
                {"M3.6 12A1 1 0 1 0 5.6 12A1 1 0 1 0 3.6 12Z", 0, Kind::Thin},
                {"M7.4 10.6L21 6.5M7.4 13.4L21 17.5", 0, Kind::Thin},
                {"M11.6 10.2L10 12L11.6 13.8M14.2 10.2L12.6 12L14.2 13.8"},
                {ROCK}};
    case Id::Pickaxe: return {{"M12 7V22", 45}, {PICK, 45}};
    case Id::Lighter:  // Feuerzeug: Körper, Metallkappe mit Reibrad, kleine Flamme darüber
        return {{"M9 10H15A1.5 1.5 0 0 1 16.5 11.5V20A1.5 1.5 0 0 1 15 21.5H9A1.5 1.5 0 0 1 7.5 20V11.5A1.5 1.5 0 0 1 9 10Z"},
                {"M8.5 10V7.5H13.5V10"},
                {"M14 8.4A1.3 1.3 0 1 0 16.6 8.4A1.3 1.3 0 1 0 14 8.4Z", 0, Kind::Thin},
                {"M11 1.6C12.3 3 13 4 13 5A2 2 0 0 1 9 5C9 4 9.7 3 11 1.6Z"},
                {"M7.5 13.5H16.5", 0, Kind::Thin}};
    case Id::Burn: return {{FLAME}};
    case Id::Frost: return {};  // wird in build() gerechnet
    case Id::Cut: return {{DROP}};
    case Id::Bruise:
        return {{"M4.4 12A7.6 7.6 0 1 0 19.6 12A7.6 7.6 0 1 0 4.4 12Z", 0, Kind::Dash},
                {"M8.8 12A3.2 3.2 0 1 0 15.2 12A3.2 3.2 0 1 0 8.8 12Z", 0, Kind::Fill}};
    case Id::Break: return {{BONE, -35}};
    default: return {};
    }
}

// --- SVG-Pfad (Untermenge: M L H V Q C A Z, groß und klein) in Linienzüge -----------------------------------------

class PathReader {
public:
    PathReader(const char* s, std::vector<Poly>& out) : s_(s), out_(out) {}

    void run() {
        char cmd = 0;
        while (true) {
            skip();
            if (!*s_) break;
            if (std::isalpha((unsigned char)*s_)) cmd = *s_++;
            else if (!cmd) break;
            const bool rel = std::islower((unsigned char)cmd);
            const char C = (char)std::toupper((unsigned char)cmd);
            const ImVec2 o = rel ? p_ : ImVec2(0, 0);
            float v[7];
            switch (C) {
            case 'M':
                if (!nums(v, 2)) return;
                flush(false);
                p_ = start_ = ImVec2(o.x + v[0], o.y + v[1]);
                cmd = rel ? 'l' : 'L';  // weitere Zahlenpaare sind Linien
                break;
            case 'L':
                if (!nums(v, 2)) return;
                lineTo(ImVec2(o.x + v[0], o.y + v[1]));
                break;
            case 'H':
                if (!nums(v, 1)) return;
                lineTo(ImVec2(o.x + v[0], p_.y));
                break;
            case 'V':
                if (!nums(v, 1)) return;
                lineTo(ImVec2(p_.x, o.y + v[0]));
                break;
            case 'Q': {
                if (!nums(v, 4)) return;
                const ImVec2 a = p_, b(o.x + v[0], o.y + v[1]), e(o.x + v[2], o.y + v[3]);
                for (int i = 1; i <= 12; ++i) {
                    const float t = i / 12.0f, u = 1 - t;
                    lineTo(ImVec2(u * u * a.x + 2 * u * t * b.x + t * t * e.x, u * u * a.y + 2 * u * t * b.y + t * t * e.y));
                }
                break;
            }
            case 'C': {
                if (!nums(v, 6)) return;
                const ImVec2 a = p_, b(o.x + v[0], o.y + v[1]), c(o.x + v[2], o.y + v[3]), e(o.x + v[4], o.y + v[5]);
                for (int i = 1; i <= 14; ++i) {
                    const float t = i / 14.0f, u = 1 - t;
                    const float k0 = u * u * u, k1 = 3 * u * u * t, k2 = 3 * u * t * t, k3 = t * t * t;
                    lineTo(ImVec2(k0 * a.x + k1 * b.x + k2 * c.x + k3 * e.x, k0 * a.y + k1 * b.y + k2 * c.y + k3 * e.y));
                }
                break;
            }
            case 'A':  // nur Kreisbögen ohne Drehung (rx = ry), das reicht für die Formen hier
                if (!nums(v, 7)) return;
                arcTo(v[0], v[3] != 0, v[4] != 0, ImVec2(o.x + v[5], o.y + v[6]));
                break;
            case 'Z':
                if (!cur_.empty()) {
                    const ImVec2 b = cur_.back();
                    if (std::fabs(b.x - start_.x) < 1e-3f && std::fabs(b.y - start_.y) < 1e-3f) cur_.pop_back();
                }
                flush(true);
                p_ = start_;
                cmd = 0;
                break;
            default: return;
            }
        }
        flush(false);
    }

private:
    void skip() {
        while (*s_ == ' ' || *s_ == ',' || *s_ == '\n' || *s_ == '\t') ++s_;
    }
    bool nums(float* v, int n) {
        for (int i = 0; i < n; ++i) {
            skip();
            char* e = nullptr;
            v[i] = std::strtof(s_, &e);
            if (e == s_) return false;
            s_ = e;
        }
        return true;
    }
    void lineTo(ImVec2 q) {
        if (cur_.empty()) cur_.push_back(p_);
        cur_.push_back(q);
        p_ = q;
    }
    void flush(bool closed) {
        if (cur_.size() >= 2) out_.push_back({cur_, closed});
        cur_.clear();
    }
    // SVG-Bogen vom Endpunkt in die Mittelpunktform umrechnen
    void arcTo(float r, bool large, bool sweep, ImVec2 e) {
        const ImVec2 a = p_;
        const float dx = (a.x - e.x) * 0.5f, dy = (a.y - e.y) * 0.5f;
        const float d2 = dx * dx + dy * dy;
        if (d2 < 1e-8f) return;
        if (d2 > r * r) r = std::sqrt(d2);
        float coef = std::sqrt(std::fmax(0.0f, (r * r - d2) / d2));
        if (large == sweep) coef = -coef;
        const float cxp = coef * dy, cyp = -coef * dx;
        const ImVec2 c(cxp + (a.x + e.x) * 0.5f, cyp + (a.y + e.y) * 0.5f);
        const float t1 = std::atan2((dy - cyp) / r, (dx - cxp) / r);
        const float t2 = std::atan2((-dy - cyp) / r, (-dx - cxp) / r);
        float dt = t2 - t1;
        if (!sweep && dt > 0) dt -= 2 * kPi;
        if (sweep && dt < 0) dt += 2 * kPi;
        const int n = std::max(4, (int)std::ceil(std::fabs(dt) / (kPi / 14)));
        for (int i = 1; i < n; ++i) {
            const float t = t1 + dt * i / n;
            lineTo(ImVec2(c.x + r * std::cos(t), c.y + r * std::sin(t)));
        }
        lineTo(e);
    }

    const char* s_;
    std::vector<Poly>& out_;
    std::vector<ImVec2> cur_;
    ImVec2 p_{0, 0}, start_{0, 0};
};

ImVec2 rotated(ImVec2 q, float deg) {
    if (deg == 0) return q;
    const float a = deg * kPi / 180, cs = std::cos(a), sn = std::sin(a);
    const float x = q.x - 12, y = q.y - 12;
    return ImVec2(12 + x * cs - y * sn, 12 + x * sn + y * cs);
}

// Linienzug an scharfen Ecken (mehr als ~55° Knick) in offene Stücke mit runden Enden teilen
void split(const Poly& poly, Kind kind, std::vector<Piece>& out) {
    std::vector<ImVec2> p;
    for (const ImVec2& q : poly.pts)
        if (p.empty() || std::fabs(q.x - p.back().x) + std::fabs(q.y - p.back().y) > 1e-4f) p.push_back(q);
    const int n = (int)p.size();
    if (n < 2) return;
    auto sharp = [&](int i) {
        const ImVec2 a = p[(i - 1 + n) % n], b = p[i], c = p[(i + 1) % n];
        const float ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
        const float lu = std::sqrt(ux * ux + uy * uy), lv = std::sqrt(vx * vx + vy * vy);
        if (lu < 1e-5f || lv < 1e-5f) return false;
        return (ux * vx + uy * vy) / (lu * lv) < 0.57f;
    };
    if (kind == Kind::Fill) {
        out.push_back({p, true, false, false, kind});
        return;
    }
    if (poly.closed) {
        int s = -1;
        for (int i = 0; i < n && s < 0; ++i)
            if (sharp(i)) s = i;
        if (s < 0) {
            out.push_back({p, true, false, false, kind});
            return;
        }
        Piece cur{{p[s]}, false, true, false, kind};
        for (int k = 1; k <= n; ++k) {
            const int i = (s + k) % n;
            cur.pts.push_back(p[i]);
            if (k == n || sharp(i)) {
                out.push_back(cur);
                cur = Piece{{p[i]}, false, true, false, kind};
            }
        }
        return;
    }
    Piece cur{{p[0]}, false, true, false, kind};
    for (int i = 1; i < n; ++i) {
        cur.pts.push_back(p[i]);
        if (i == n - 1) {
            cur.capEnd = true;
            out.push_back(cur);
        } else if (sharp(i)) {
            out.push_back(cur);
            cur = Piece{{p[i]}, false, true, false, kind};
        }
    }
}

// Gestrichelter geschlossener Linienzug (Strich 2,2, Lücke 2,3 im Raster)
void dash(const Poly& poly, std::vector<Piece>& out) {
    const float on = 2.2f, off = 2.3f;
    std::vector<ImVec2> p = poly.pts;
    if (poly.closed) p.push_back(p.front());
    float phase = 0;
    bool drawing = true;
    Piece cur{{p[0]}, false, true, true, Kind::Dash};
    for (size_t i = 1; i < p.size(); ++i) {
        ImVec2 a = p[i - 1];
        const ImVec2 b = p[i];
        float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        while (len > 1e-5f) {
            const float left = (drawing ? on : off) - phase;
            const float step = std::fmin(left, len);
            const float t = step / len;
            const ImVec2 q(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
            if (drawing) cur.pts.push_back(q);
            phase += step;
            len -= step;
            a = q;
            if (phase >= (drawing ? on : off) - 1e-5f) {
                if (drawing && cur.pts.size() >= 2) out.push_back(cur);
                drawing = !drawing;
                phase = 0;
                cur = Piece{{q}, false, true, true, Kind::Dash};
            }
        }
    }
    if (drawing && cur.pts.size() >= 2) out.push_back(cur);
}

std::vector<Piece> build(Id id) {
    std::vector<Piece> pieces;
    for (const El& e : elements(id)) {
        std::vector<Poly> polys;
        PathReader(e.d, polys).run();
        for (Poly& p : polys) {
            for (ImVec2& q : p.pts) q = rotated(q, e.rot);
            if (e.kind == Kind::Dash) dash(p, pieces);
            else split(p, e.kind, pieces);
        }
    }
    if (id == Id::Frost) {  // Schneekristall: sechs Arme mit je zwei Zweigen
        auto P = [](float r, float a) { return ImVec2(12 + r * std::cos(a), 12 + r * std::sin(a)); };
        for (int k = 0; k < 6; ++k) {
            const float a = (k * 60 - 90) * kPi / 180;
            pieces.push_back({{ImVec2(12, 12), P(8.6f, a)}, false, true, true, Kind::Line});
            const ImVec2 b = P(5.4f, a);
            for (int s = -1; s <= 1; s += 2) {
                const float q = a + s * 0.75f;
                pieces.push_back({{b, ImVec2(b.x + 2.8f * std::cos(q), b.y + 2.8f * std::sin(q))}, false, false, true, Kind::Line});
            }
        }
    }
    return pieces;
}

const std::vector<Piece>& shape(Id id) {
    static std::vector<Piece> cache[(int)Id::Count];
    static bool built[(int)Id::Count] = {};
    const int i = (int)id;
    if (!built[i]) {
        cache[i] = build(id);
        built[i] = true;
    }
    return cache[i];
}

ImU32 withAlpha(ImU32 c, float a) {
    const int al = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * std::fmin(std::fmax(a, 0.0f), 1.0f));
    return (c & ~IM_COL32_A_MASK) | ((ImU32)al << IM_COL32_A_SHIFT);
}

ImU32 lerpCol(ImU32 a, ImU32 b, float t) {
    auto ch = [&](int sh) {
        const float x = (float)((a >> sh) & 0xFF), y = (float)((b >> sh) & 0xFF);
        return (ImU32)(x + (y - x) * t + 0.5f) << sh;
    };
    return ch(IM_COL32_R_SHIFT) | ch(IM_COL32_G_SHIFT) | ch(IM_COL32_B_SHIFT) | ch(IM_COL32_A_SHIFT);
}

} // namespace

void draw(ImDrawList* dl, Id id, ImVec2 c, float size, ImU32 color, float stroke) {
    const float s = size / 24.0f;
    const ImVec2 o(c.x - 12 * s, c.y - 12 * s);
    std::vector<ImVec2> pts;
    for (const Piece& p : shape(id)) {
        pts.resize(p.pts.size());
        for (size_t i = 0; i < pts.size(); ++i) pts[i] = ImVec2(o.x + p.pts[i].x * s, o.y + p.pts[i].y * s);
        if (p.kind == Kind::Fill) {
            dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), color);
            continue;
        }
        const float w = p.kind == Kind::Thin ? stroke * 0.68f : stroke;
        dl->AddPolyline(pts.data(), (int)pts.size(), color, p.closed ? ImDrawFlags_Closed : ImDrawFlags_None, w);
        if (p.capStart) dl->AddCircleFilled(pts.front(), w * 0.5f, color, 10);
        if (p.capEnd) dl->AddCircleFilled(pts.back(), w * 0.5f, color, 10);
    }
}

void drawShadowed(ImDrawList* dl, Id id, ImVec2 c, float size, ImU32 color, float stroke) {
    const float a = ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    draw(dl, id, ImVec2(c.x, c.y + 1.0f), size, IM_COL32(0, 0, 0, (int)(150 * a)), stroke + 0.6f);
    draw(dl, id, c, size, color, stroke);
}

void drawEngraved(ImDrawList* dl, Id id, ImVec2 c, float size, ImU32 color, float stroke) {
    const float a = ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    draw(dl, id, ImVec2(c.x, c.y + 1.0f), size, IM_COL32(255, 236, 200, (int)(46 * a)), stroke);  // heller Schein an der Unterkante
    draw(dl, id, ImVec2(c.x, c.y - 0.8f), size, IM_COL32(20, 10, 4, (int)(225 * a)), stroke);    // Schatten der Oberkante
    draw(dl, id, c, size, color, stroke);
}

ImU32 tokenColor(bool selected) { return selected ? IM_COL32(125, 86, 52, 255) : IM_COL32(107, 68, 34, 255); }

void woodToken(ImDrawList* dl, ImVec2 c, float r, bool selected) {
    // Schatten unter der Marke
    for (int i = 4; i >= 1; --i) dl->AddCircleFilled(ImVec2(c.x, c.y + 2), r + i * 1.1f, IM_COL32(0, 0, 0, 18), 48);
    if (selected)  // moosgrünes Leuchten
        for (int i = 7; i >= 1; --i) dl->AddCircle(c, r + i * 1.0f, IM_COL32(182, 224, 122, (int)(70 * (1 - i / 8.0f))), 48, 1.6f);

    // Holz: Verlauf von hell (etwas über der Mitte) nach dunkel am Rand, als enge Ringe übereinander
    const ImU32 c0 = selected ? IM_COL32(155, 108, 63, 255) : IM_COL32(138, 96, 56, 255);
    const ImU32 c1 = selected ? IM_COL32(125, 86, 52, 255) : IM_COL32(107, 68, 34, 255);
    const ImU32 c2 = IM_COL32(70, 44, 22, 255);
    const ImVec2 hl(c.x, c.y - 0.24f * r);
    const int steps = 18;
    for (int i = 0; i < steps; ++i) {
        const float t = 1 - (float)i / steps;  // 1 außen, gegen 0 innen
        const ImVec2 m(c.x + (hl.x - c.x) * (1 - t), c.y + (hl.y - c.y) * (1 - t));
        const ImU32 col = t > 0.5f ? lerpCol(c1, c2, (t - 0.5f) / 0.5f) : lerpCol(c0, c1, t / 0.5f);
        dl->AddCircleFilled(m, r * t, col, 48);
    }
    // feine Maserung: zwei helle Bögen
    dl->PathArcTo(c, r * 0.62f, kPi * 1.08f, kPi * 1.42f, 16);
    dl->PathStroke(IM_COL32(201, 151, 92, 38), ImDrawFlags_None, 1.0f);
    dl->PathArcTo(c, r * 0.78f, kPi * 1.6f, kPi * 1.9f, 16);
    dl->PathStroke(IM_COL32(201, 151, 92, 30), ImDrawFlags_None, 1.0f);

    // Rand: dunkler Reif, innen ein heller (gewählt: moosgrün)
    dl->AddCircle(c, r - 2.8f, selected ? IM_COL32(182, 224, 122, 255) : IM_COL32(201, 151, 92, 90), 48, 1.6f);
    dl->AddCircle(c, r - 1.0f, IM_COL32(36, 21, 10, 255), 48, 2.0f);
}

} // namespace icons
