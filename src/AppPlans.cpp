// Bauvorlagen im Spiel-Modus (docs/20-Bauvorlagen.md): B setzt Hologramme, E setzt das getragene Teil ein,
// Q halten baut ab bzw. entfernt ein Hologramm. Die Logik dahinter steht in Blueprints.cpp (World).
#include "App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "Materials.h"

namespace {

void line(std::vector<LineVertex>& out, const glm::vec3& a, const glm::vec3& b, const glm::vec4& c) {
    out.push_back({a, c});
    out.push_back({b, c});
}

// Quader (Mitte c, Drehung r, Halbmaße h) als durchsichtige Flächen und Kanten
void holoBox(FrameDesc& fd, const glm::dvec3& O, const glm::dvec3& c, const glm::dquat& r, const glm::dvec3& h, const glm::vec4& fill,
             const glm::vec4& edge, bool overlay) {
    glm::vec3 p[8];
    for (int i = 0; i < 8; ++i)
        p[i] = glm::vec3(c - O + r * glm::dvec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z));
    const int quads[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (auto& q : quads)
        for (int i : {0, 1, 2, 0, 2, 3}) fd.translucent.push_back({p[q[i]], fill});
    const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& k : e) line(overlay ? fd.overlayLines : fd.lines, p[k[0]], p[k[1]], edge);
}

// Weltbegrenzung (achsparallel) eines Bauteils
void pieceBounds(const BuildPlan& bp, const PlanPiece& p, glm::dvec3& mn, glm::dvec3& mx) {
    const glm::dmat3 m = glm::mat3_cast(bp.pieceRot(p));
    glm::dvec3 ext(0);
    for (int k = 0; k < 3; ++k) ext += glm::abs(m[k]) * p.half[k];
    const glm::dvec3 c = bp.pieceCenter(p);
    mn = c - ext;
    mx = c + ext;
}

} // namespace

// Wo käme die gewählte Vorlage hin? Auf die angeschaute Stelle; in der Nähe einer anderen Vorlage rastet sie in deren
// Raster ein (1 m waagrecht, 2,5 m Geschosshöhe, 90°), so passen Wand auf Bodenrand, Wand neben Wand, Decke auf Wand.
bool App::planPlacement(const glm::dvec3& eye, const glm::dvec3& fwd, BuildPlan& out, bool& snapped) {
    snapped = false;
    const auto& list = planList();
    if (list.empty()) return false;
    int& index = planIndex();
    index = std::clamp(index, 0, (int)list.size() - 1);
    const PlanTemplate& tp = list[(size_t)index];
    const std::string type = (tp.craft ? kCraftPrefix : "") + tp.id;
    RayHit h;
    glm::dvec3 at;
    if (world_->raycast(eye, fwd, h, hand_.grabbed) && h.t < (tp.craft ? 4.0 : 12.0)) at = eye + fwd * h.t;
    else return false;
    if (tp.craft) {  // Rezept: einfach dort hinlegen, wo man hinschaut (kein Raster), quer vor die Figur
        const double yaw = playYaw_ + planTurns_ * glm::half_pi<double>();
        return world_->makePlan(type, at, std::round(yaw / glm::radians(15.0)) * glm::radians(15.0), true, out);
    }
    uint32_t pid;
    int pi;
    double tp2;
    if (world_->pickPlan(eye, fwd, h.t, pid, pi, tp2, true)) at = eye + fwd * tp2;  // auch auf Hologramme zielen
    double yaw = playYaw_ + planTurns_ * glm::half_pi<double>();
    yaw = std::round(yaw / glm::radians(15.0)) * glm::radians(15.0);
    glm::dvec3 pos = at;
    const bool free = ImGui::GetIO().KeyAlt;
    const BuildPlan* near = nullptr;
    double best = 4.0;
    if (!free)
        for (const BuildPlan& p : world_->plans()) {
            if (p.craft()) continue;
            const double d = glm::length(p.pos - at);
            if (d < best) {
                best = d;
                near = &p;
            }
        }
    if (near) {
        const glm::dquat r = near->rot();
        glm::dvec3 rel = glm::conjugate(r) * (at - near->pos);
        rel.y = std::round(rel.y / 2.5) * 2.5;
        const double q = glm::half_pi<double>();
        yaw = near->yaw + std::round((yaw - near->yaw) / q) * q;
        snapped = true;
        // Rasterplätze rund um die Zielstelle: die Vorlage reicht von der Zielstelle zum Spieler hin (zielt man auf eine
        // Wandkrone, liegt der Boden auf der eigenen Seite, nicht halb in der Wand). Plätze, an denen ein Bauteil zu einem
        // guten Teil in einem anderen steckt, verlieren
        glm::dvec3 toEye = eye - at;
        toEye.y = 0;
        toEye = glm::length(toEye) > 1e-6 ? glm::normalize(toEye) : glm::dvec3(0);
        double bestScore = 1e30;
        const double rx = std::round(rel.x), rz = std::round(rel.z);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                const glm::dvec3 p = near->pos + r * glm::dvec3(rx + dx, rel.y, rz + dz);
                BuildPlan cand;
                if (!world_->makePlan(tp.id, p, yaw, planFixed_, cand)) continue;
                glm::dvec3 cMin(1e30), cMax(-1e30);
                double overlap = 0;
                for (const PlanPiece& a : cand.pieces) {
                    glm::dvec3 aMin, aMax;
                    pieceBounds(cand, a, aMin, aMax);
                    cMin = glm::min(cMin, aMin);
                    cMax = glm::max(cMax, aMax);
                    for (const BuildPlan& other : world_->plans())
                        for (const PlanPiece& b : other.pieces) {
                            glm::dvec3 bMin, bMax;
                            pieceBounds(other, b, bMin, bMax);
                            const glm::dvec3 e = glm::min(aMax, bMax) - glm::max(aMin, bMin) - 0.02;  // 1 cm Spiel je Seite
                            // nur echtes Ineinanderstecken zählt (über 30 % des kleineren), nicht ein Rand auf einer Wandkrone
                            if (e.x > 0 && e.y > 0 && e.z > 0 && e.x * e.y * e.z > 0.3 * std::min(a.volume(), b.volume())) overlap += 1;
                        }
                }
                const glm::dvec3 c = (cMin + cMax) * 0.5, half = (cMax - cMin) * 0.5;
                // gewünschte Mitte: von der Zielstelle um die halbe Ausdehnung der Vorlage zum Spieler hin
                const glm::dvec3 want = at + toEye * (std::abs(toEye.x) * half.x + std::abs(toEye.z) * half.z);
                const double score = overlap * 100 + glm::length(glm::dvec2(c.x - want.x, c.z - want.z)) + 0.5 * std::abs(c.y - want.y);
                if (score < bestScore) {
                    bestScore = score;
                    pos = p;
                }
            }
    } else if (tp.toGround) {  // Fundament ohne Nachbarn: Oberseite auf den höchsten Punkt des Geländes darunter
        const glm::dquat r = glm::angleAxis(yaw, glm::dvec3(0, 1, 0));
        double top = -1e30;
        for (const PlanPieceDef& d : tp.pieces)
            for (int c = 0; c < 4; ++c) {
                const glm::dvec3 l = d.center + glm::dvec3((c & 1) ? d.half.x : -d.half.x, 0, (c & 2) ? d.half.z : -d.half.z);
                const glm::dvec3 wp = pos + r * l;
                top = std::max(top, world_->groundHeight(wp.x, wp.z));
            }
        pos.y = std::max(pos.y, top);
    }
    return world_->makePlan(tp.id, pos, yaw, planFixed_, out);
}

void App::planInput(float dt, const glm::dvec3& eye, const glm::dvec3& fwd, bool lmbPress, bool rmbPress) {
    ImGuiIO& io = ImGui::GetIO();
    world_->updatePlans();
    // angeschautes Hologramm (nur wenn kein Teil davor ist)
    planLook_ = 0;
    planLookPiece_ = -1;
    {
        uint32_t pid;
        int pi;
        double t;
        const double maxT = lookAt_ ? std::min(4.5, lookDist_) : 4.5;
        if (world_->pickPlan(eye, fwd, maxT, pid, pi, t)) {
            planLook_ = pid;
            planLookPiece_ = pi;
        }
    }
    // Q halten: fertiges Bauteil abbauen, oder (auf ein Hologramm) die Vorlage entfernen
    uint32_t qTarget = 0;
    uint32_t pid;
    int pi;
    if (lookAt_ && world_->planPieceOf(lookAt_, pid, pi)) qTarget = lookAt_;
    else if (planLook_) qTarget = 0x80000000u | planLook_;
    if (!io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_Q) && qTarget && !netClient()) {
        if (qTarget != dismantleId_) dismantleHold_ = 0;
        dismantleId_ = qTarget;
        dismantleHold_ += dt;
        if (dismantleHold_ >= 0.8) {
            world_->pushUndo();
            std::string err;
            if (qTarget & 0x80000000u) {
                const BuildPlan* bp = world_->findPlan(qTarget & 0x7fffffffu);
                if (bp && bp->craft()) {
                    // Rezept: das schon eingesetzte Material kommt als Klotz je Bauteil zurück (ins Inventar, sonst liegt er da)
                    std::vector<uint32_t> back;
                    for (const PlanPiece& pc : bp->pieces)
                        if (pc.filled > 1e-7 && pc.material >= 0)
                            if (Object* o = world_->addBox(pc.material, glm::dvec3(std::cbrt(pc.filled)), bp->pieceCenter(pc))) back.push_back(o->id);
                    world_->removePlan(qTarget & 0x7fffffffu);
                    int stored = 0;
                    for (uint32_t id : back) stored += invStore(id, true) ? 1 : 0;
                    toast(back.empty() ? "Rezept entfernt"
                                       : "Rezept entfernt, Material zurück" + std::string(stored < (int)back.size() ? " (liegt zum Teil am Boden)" : " im Inventar"));
                } else {
                    world_->removePlan(qTarget & 0x7fffffffu);
                    toast("Vorlage entfernt (fertige Bauteile bleiben stehen)");
                }
            } else if (world_->dismantle(qTarget, &err)) {
                toast("Abgebaut: das Teil ist wieder lose, das Hologramm ist wieder leer");
            } else {
                world_->dropUndo();
                toast(err, false);
            }
            dismantleHold_ = 0;
            dismantleId_ = 0;
        }
    } else {
        dismantleHold_ = 0;
        dismantleId_ = 0;
    }

    // B: Bauvorlagen, C: Rezepte zum Herstellen (dieselbe Bedienung); dieselbe Taste nochmal beendet. Mit Hauptmenü
    // öffnet die Taste erst die Auswahl (Kasten je Vorlage, AppCatalog.cpp), ohne geht es gleich mit der letzten los
    for (const bool craft : {false, true}) {
        if (io.WantTextInput || !ImGui::IsKeyPressed(craft ? ImGuiKey_C : ImGuiKey_B, false)) continue;
        if (opt_.catalogUi && !(planMode_ && planCraft_ == craft)) {
            planMode_ = false;
            catalogOpen(true, craft);
            continue;
        }
        planMode_ = !(planMode_ && planCraft_ == craft);
        planCraft_ = craft;
        if (planMode_ && planList().empty()) {
            planMode_ = false;
            toast(craft ? "Keine Rezepte gefunden (assets/recipes fehlt?)" : "Keine Vorlagen gefunden (assets/blueprints fehlt?)", false);
        }
        if (planMode_) axe_ = false;
    }
    if (!planMode_) {
        planPreviewOk_ = false;
        return;
    }
    const int n = (int)planList().size();
    int& index = planIndex();
    if (scrollAccum_ != 0.0) {
        index = ((index + (scrollAccum_ < 0 ? 1 : -1)) % n + n) % n;
        scrollAccum_ = 0;
    }
    if (!io.WantTextInput) {
        for (int k = 0; k < 10; ++k)
            if (k < n && ImGui::IsKeyPressed(k < 9 ? (ImGuiKey)(ImGuiKey_1 + k) : ImGuiKey_0, false)) index = k;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) planTurns_ = (planTurns_ + (io.KeyShift ? 3 : 1)) % 4;
        if (!planCraft_ && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            planFixed_ = !planFixed_;
            toast(planFixed_ ? "Neue Vorlagen: fest (schweben, zerbrechen nicht)" : "Neue Vorlagen: Physik (fertig = lose und verleimt)");
        }
    }
    planPreviewOk_ = planPlacement(eye, fwd, planPreview_, planSnapped_);
    if (rmbPress) planMode_ = false;
    if (lmbPress && planPreviewOk_) {
        world_->pushUndo();
        std::string err;
        BuildPlan* p = world_->addPlan(planPreview_.type, planPreview_.pos, planPreview_.yaw, planFixed_ || planCraft_, &err);
        if (!p) {
            world_->dropUndo();
            toast(err, false);
        } else if (planCraft_) {  // ein Rezept setzt man meist einmal: gleich zurück zum Laufen und Füllen
            planMode_ = false;
            toast("Rezept gesetzt: Material tragen + E, oder mit leerer Hand E = aus dem Inventar füllen");
        }
    }
}

void App::addPlans(FrameDesc& fd) {
    const glm::dvec3 O = renderOrigin_;
    const glm::dvec3 cam = play_ ? playerEye() : orbitEye();
    world_->updatePlans();
    const Object* held = world_->find(hand_.grabbed);
    for (const BuildPlan& bp : world_->plans()) {
        if (glm::length(bp.pos - cam) > 150.0) continue;
        for (size_t i = 0; i < bp.pieces.size(); ++i) {
            const PlanPiece& pc = bp.pieces[i];
            if (pc.built) continue;
            // Rezepte (Herstellen) orange, Bauvorlagen blau
            glm::vec3 col = bp.craft() ? glm::vec3(1.0f, 0.65f, 0.2f) : glm::vec3(0.3f, 0.6f, 1.0f);
            float a = 0.13f;
            if (bp.id == planLook_ && (int)i == planLookPiece_) {
                a = 0.3f;
                if (held) col = planPieceFits(pc, held->material) ? glm::vec3(0.3f, 1.0f, 0.4f) : glm::vec3(1.0f, 0.3f, 0.25f);
                else col = bp.craft() ? glm::vec3(1.0f, 0.82f, 0.45f) : glm::vec3(0.55f, 0.85f, 1.0f);
            }
            const glm::dvec3 c = bp.pieceCenter(pc);
            const glm::dquat r = bp.pieceRot(pc);
            holoBox(fd, O, c, r, pc.half, glm::vec4(col, a), glm::vec4(col, 0.85f), false);
            // schon eingesetztes Material: von unten aufgefüllt
            const double f = std::clamp(pc.filled / std::max(1e-9, pc.volume()), 0.0, 1.0);
            if (f > 0.005) {
                glm::dvec3 h = pc.half * 0.98;
                h.y *= f;
                holoBox(fd, O, c + r * glm::dvec3(0, -pc.half.y * 0.98 + h.y, 0), r, h, glm::vec4(col, 0.45f), glm::vec4(col, 0.0f), false);
            }
        }
    }
    if (play_ && planMode_ && planPreviewOk_) {
        const glm::vec3 col = planCraft_ ? glm::vec3(1.0f, 0.75f, 0.35f) : planSnapped_ ? glm::vec3(0.4f, 1.0f, 0.95f) : glm::vec3(0.5f, 0.8f, 1.0f);
        for (const PlanPiece& pc : planPreview_.pieces)
            holoBox(fd, O, planPreview_.pieceCenter(pc), planPreview_.pieceRot(pc), pc.half, glm::vec4(col, 0.1f), glm::vec4(col, 0.7f), true);
    }
}

std::string App::planHudText() {
    std::string s;
    if (planLook_) {
        if (const BuildPlan* bp = world_->findPlan(planLook_); bp && planLookPiece_ >= 0 && planLookPiece_ < (int)bp->pieces.size()) {
            const PlanPiece& pc = bp->pieces[(size_t)planLookPiece_];
            const PlanTemplate* tp = planTemplateFor(bp->type);
            // Rezepte sind klein: cm³ statt dm³
            const double unit = bp->craft() ? 1e6 : 1000;
            s = (tp ? tp->label : bp->type) + " " + std::to_string(planLookPiece_ + 1) + "/" + std::to_string(bp->pieces.size()) + ": " +
                (pc.material >= 0 ? materials()[(size_t)pc.material].label : planMatLabel((PlanMat)pc.mat)) + " " +
                fmtNum(pc.filled * unit, 0) + " / " + fmtNum(pc.volume() * unit, 0) + (bp->craft() ? " cm³" : " dm³");
            if (const Object* g = world_->find(hand_.grabbed))
                s += planPieceFits(pc, g->material) ? "  [E: einsetzen]" : std::string("  (passt nicht: ") + materials()[(size_t)g->material].label + ")";
            else
                s += bp->craft() ? "  [E: aus dem Inventar füllen]" : "  (Material tragen, dann E)";
            s += bp->craft() ? "  [Q halten: Rezept entfernen]" : "  [Q halten: Vorlage entfernen]";
        }
    } else if (lookAt_) {
        uint32_t pid;
        int pi;
        if (world_->planPieceOf(lookAt_, pid, pi)) s = "Bauteil  [Q halten: abbauen]";
    }
    if (dismantleHold_ > 0) s += "  " + fmtNum(std::min(100.0, dismantleHold_ / 0.8 * 100), 0) + " %";
    return s;
}
