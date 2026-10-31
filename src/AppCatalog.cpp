// Auswahl der Bauvorlagen (B) und Rezepte (C) im Spiel-Modus: ein Fenster des Hauptmenüs (CatalogUi, qml/CatalogWindow.qml)
// mit einem Kasten je Vorlage. Die Zeichnung im Kasten rechnet das Spiel hier aus: die Quader der Vorlage schräg von
// vorne oben gesehen, nur die sichtbaren Flächen, schattiert und von hinten nach vorne sortiert. Klick auf einen Kasten:
// der Bauplan-Modus (AppPlans.cpp) beginnt mit dieser Vorlage.
#include "App.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Materials.h"

namespace {

// Kastenmaße der Zeichnung im Fenster (Pixel): damit Quader nicht verzerrt werden
constexpr double kPicW = 148, kPicH = 130;

std::string hexColor(glm::dvec3 c) {
    char buf[8];
    c = glm::clamp(c, 0.0, 1.0) * 255.0;
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", (int)std::lround(c.r), (int)std::lround(c.g), (int)std::lround(c.b));
    return buf;
}

glm::dvec3 matColor(PlanMat m) {
    switch (m) {
    case PlanMat::Wood: return {0.72, 0.52, 0.32};
    case PlanMat::Stone: return {0.62, 0.62, 0.60};
    case PlanMat::Metal: return {0.58, 0.64, 0.72};
    default: return {0.75, 0.75, 0.75};
    }
}

// Eine Fläche im Raum (konvexes Polygon, nach außen zeigende Normale), noch nicht projiziert
struct Face3 {
    std::vector<glm::dvec3> v;
    glm::dvec3 n{0, 1, 0};
    glm::dvec3 color{0.7};
};

// Flächen als Zeichnung: schräg von vorne rechts oben gesehen, nur die Vorderseiten, schattiert, von hinten nach vorne
// sortiert, so groß wie möglich in einen Kasten mit dem Seitenverhältnis aspect (Breite / Höhe) gelegt (0 … 1)
std::vector<CatalogUi::Poly> draw(const std::vector<Face3>& faces, double aspect) {
    const glm::dvec3 toCam = glm::normalize(glm::dvec3(0.55, 0.6, 1.0));
    const glm::dvec3 right = glm::normalize(glm::cross(-toCam, glm::dvec3(0, 1, 0)));
    const glm::dvec3 up = glm::cross(right, -toCam);
    const glm::dvec3 light = glm::normalize(glm::dvec3(0.4, 1.0, 0.6));
    struct Face2 {
        double depth = 0;
        std::vector<glm::dvec2> p;
        glm::dvec3 color;
    };
    std::vector<Face2> out2;
    glm::dvec2 mn(1e30), mx(-1e30);
    for (const Face3& f : faces) {
        if (f.v.size() < 3 || glm::dot(f.n, toCam) <= 1e-6) continue;  // Rückseite
        Face2 g;
        for (const glm::dvec3& w : f.v) {
            g.p.push_back(glm::dvec2(glm::dot(w, right), -glm::dot(w, up)));
            g.depth += glm::dot(w, toCam) / (double)f.v.size();
            mn = glm::min(mn, g.p.back());
            mx = glm::max(mx, g.p.back());
        }
        g.color = f.color * (0.55 + 0.45 * std::max(0.0, glm::dot(f.n, light)));
        out2.push_back(std::move(g));
    }
    std::sort(out2.begin(), out2.end(), [](const Face2& x, const Face2& y) { return x.depth < y.depth; });
    const glm::dvec2 size = glm::max(mx - mn, glm::dvec2(1e-6)), mid = (mn + mx) * 0.5;
    const double s = std::min(0.88 * aspect / size.x, 0.88 / size.y);  // Kastenhöhe je Meter, gleich in x und y
    std::vector<CatalogUi::Poly> out;
    for (const Face2& g : out2) {
        CatalogUi::Poly p;
        for (const glm::dvec2& q : g.p) {
            p.pts.push_back((float)(0.5 + (q.x - mid.x) * s / aspect));
            p.pts.push_back((float)(0.5 + (q.y - mid.y) * s));
        }
        p.color = hexColor(g.color);
        out.push_back(std::move(p));
    }
    return out;
}

// Die Vorlage als Zeichnung: ihre Quader, gefärbt nach der Materialart
std::vector<CatalogUi::Poly> picture(const PlanTemplate& tp) {
    std::vector<Face3> faces;
    for (const PlanPieceDef& d : tp.pieces) {
        const glm::dquat r = d.rot * glm::angleAxis(glm::radians(d.pitchDeg), glm::dvec3(1, 0, 0));
        for (int axis = 0; axis < 3; ++axis)
            for (int sign : {-1, 1}) {
                Face3 f;
                glm::dvec3 n(0);
                n[axis] = sign;
                f.n = r * n;
                f.color = matColor(d.mat);
                const int a = (axis + 1) % 3, b = (axis + 2) % 3;
                const int su[4] = {-1, 1, 1, -1}, sv[4] = {-1, -1, 1, 1};
                for (int k = 0; k < 4; ++k) {
                    glm::dvec3 l(0);
                    l[axis] = sign * d.half[axis];
                    l[a] = su[k] * d.half[a];
                    l[b] = sv[k] * d.half[b];
                    f.v.push_back(d.center + r * l);
                }
                faces.push_back(std::move(f));
            }
    }
    return draw(faces, kPicW / kPicH);
}

// Bauvorlage: Material je Art in dm³ ("Holz 120 dm³ + Stein 40 dm³")
std::string planNeeds(const PlanTemplate& tp) {
    double need[4] = {0, 0, 0, 0};
    for (const PlanPieceDef& d : tp.pieces) need[std::clamp((int)d.mat, 0, 3)] += 8.0 * d.half.x * d.half.y * d.half.z;
    std::string s;
    for (int m : {(int)PlanMat::Wood, (int)PlanMat::Stone, (int)PlanMat::Metal, (int)PlanMat::Any})
        if (need[m] > 0) s += (s.empty() ? "" : " + ") + std::string(planMatLabel((PlanMat)m)) + " " + fmtNum(need[m] * 1000, need[m] < 0.01 ? 1 : 0) + " dm³";
    return s;
}

} // namespace

// Ein Teil (Inventar) als Zeichnung: seine Flächen in Materialfarbe, längste Seite quer (wie es im Kasten liegt)
std::vector<CatalogUi::Poly> App::objectPicture(const ObjectState& s, double aspect) {
    if (s.kind != ObjKind::Solid || s.solid.pieces.empty()) return {};
    glm::dvec3 mn, mx;
    solid::bounds(s.solid, mn, mx);
    const glm::dvec3 ext = mx - mn, mid = (mn + mx) * 0.5;
    glm::dquat align(1, 0, 0, 0);
    if (ext.y > ext.x && ext.y >= ext.z) align = glm::angleAxis(glm::radians(-90.0), glm::dvec3(0, 0, 1));  // Y -> X
    else if (ext.z > ext.x && ext.z > ext.y) align = glm::angleAxis(glm::radians(90.0), glm::dvec3(0, 1, 0));  // Z -> X
    const Material& m = materials()[(size_t)std::clamp(s.material, 0, (int)materials().size() - 1)];
    const glm::dvec3 color = glm::pow(glm::dvec3(m.color), glm::dvec3(1.0 / 2.2));  // linear -> Bildschirm
    std::vector<Face3> faces;
    for (const solid::Piece& p : s.solid.pieces)
        for (const solid::Face& f : p.faces) {
            if (f.internal) continue;  // ganz verdeckt
            Face3 g;
            g.n = align * f.n;
            g.color = color;
            for (const glm::dvec3& v : f.v) g.v.push_back(align * (v - mid));
            faces.push_back(std::move(g));
            if (faces.size() > 3000) break;  // genug für ein kleines Bild
        }
    if (aspect >= 1) return draw(faces, aspect);
    // hochkant liegendes Kästchen: quer zeichnen, dann um 90° drehen
    std::vector<CatalogUi::Poly> polys = draw(faces, 1.0 / aspect);
    for (CatalogUi::Poly& p : polys)
        for (size_t k = 0; k + 1 < p.pts.size(); k += 2) {
            const float x = p.pts[k], y = p.pts[k + 1];
            p.pts[k] = 1.0f - y;
            p.pts[k + 1] = x;
        }
    return polys;
}

void App::catalogOpen(bool on, bool craft) {
    if (!opt_.catalogUi) return;
    if (!on) {
        if (!catalogOpen_) return;
        catalogOpen_ = false;
        opt_.catalogUi->close();
        if (play_ && pause_ == Pause::None && !invOpen_) {
            glfwFocusWindow(window_);
            setCursorCaptured(true);
        }
        return;
    }
    if (!play_) return;
    const auto& list = craft ? craftRecipes() : planTemplates();
    if (list.empty()) {
        toast(craft ? "Keine Rezepte gefunden (assets/recipes fehlt?)" : "Keine Vorlagen gefunden (assets/blueprints fehlt?)", false);
        return;
    }
    invOpen(false);
    std::vector<CatalogUi::Item> items;
    for (const PlanTemplate& t : list) {
        CatalogUi::Item it;
        it.label = t.label;
        it.info = craft ? craftNeeds(t) : planNeeds(t);
        it.polys = picture(t);
        items.push_back(std::move(it));
    }
    catalogOpen_ = true;
    catalogCraft_ = craft;
    setCursorCaptured(false);
    const int current = std::clamp(craft ? craftType_ : planType_, 0, (int)list.size() - 1);
    opt_.catalogUi->open(glfwGetWin32Window(window_), craft ? "Herstellen" : "Baupläne", craft ? "C" : "B", items, current);
}

bool App::catalogChoose(int index) {
    const auto& list = catalogCraft_ ? craftRecipes() : planTemplates();
    if (index < 0 || index >= (int)list.size()) return false;
    (catalogCraft_ ? craftType_ : planType_) = index;
    planCraft_ = catalogCraft_;
    planMode_ = true;
    axe_ = false;
    catalogOpen(false);
    return true;
}

void App::updateCatalog() {
    if (!catalogOpen_ || !opt_.catalogUi) return;
    for (int guard = 0; guard < 16 && catalogOpen_; ++guard) {
        const CatalogUi::Event e = opt_.catalogUi->poll();
        if (e.kind == CatalogUi::Event::None) break;
        if (e.kind == CatalogUi::Event::Close) catalogOpen(false);
        else if (e.kind == CatalogUi::Event::Choose) catalogChoose(e.index);
    }
}
