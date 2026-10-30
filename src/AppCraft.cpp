// Herstellen im Spiel-Modus (docs/22-Herstellen.md): C wählt ein Rezept und legt es als orangefarbenes Hologramm hin
// (Bedienung wie der Bauplan-Modus, AppPlans.cpp). Gefüllt wird es wie eine Bauvorlage mit getragenem Material (E) oder
// mit leerer Hand per E direkt aus dem Inventar. Ist alles voll, wird daraus ein einziges Teil, das ins Inventar kommt.
// Die Logik dahinter (Füllen, Teil bauen) steht in Blueprints.cpp (World::fillPlan).
#include "App.h"

#include <algorithm>
#include <cmath>

#include "Materials.h"

namespace {

double density(int material) { return materials()[(size_t)std::clamp(material, 0, (int)materials().size() - 1)].density; }

} // namespace

void App::craftDone(uint32_t crafted, const std::string& text) {
    Object* o = world_->find(crafted);
    if (!o) return;
    if (invStore(crafted, true)) toast(text + " - im Inventar");
    else toast(text + " - passt nicht mehr ins Inventar, es liegt beim Hologramm", false);
}

bool App::craftFill(uint32_t planId) {
    const BuildPlan* bp = world_->findPlan(planId);
    if (!bp || !bp->craft()) return false;
    if (netClient()) {
        toast("Herstellen aus dem Inventar: als Mitspieler im LAN noch nicht möglich", false);
        return true;
    }
    const std::string type = bp->type;
    int used = 0;
    uint32_t crafted = 0;
    std::string msg;
    for (size_t i = 0; !crafted; ++i) {
        bp = world_->findPlan(planId);
        if (!bp || i >= bp->pieces.size()) break;
        // je Bauteil so lange Stücke aus dem Inventar einsetzen, bis es voll ist oder nichts Passendes mehr da ist
        for (int guard = 0; guard < 64 && !crafted; ++guard) {
            bp = world_->findPlan(planId);
            if (!bp || bp->pieceDone(bp->pieces[i])) break;
            const PlanPiece& pc = bp->pieces[i];
            const double need = pc.volume() - pc.filled;
            // das kleinste Stück, das allein reicht (wenig Abfall); sonst das größte
            int pick = -1;
            double pickVol = 0;
            bool pickEnough = false;
            for (size_t k = 0; k < inv_.size(); ++k) {
                if (!planPieceFits(pc, inv_[k].state.material)) continue;
                const double v = inv_[k].mass / density(inv_[k].state.material);
                const bool enough = v >= need;
                if (pick < 0 || (enough && (!pickEnough || v < pickVol)) || (!enough && !pickEnough && v > pickVol)) {
                    pick = (int)k;
                    pickVol = v;
                    pickEnough = enough;
                }
            }
            if (pick < 0) break;
            const InvItem item = inv_[(size_t)pick];
            Object* o = world_->putBack(item.state, bp->pieceCenter(pc), bp->pieceRot(pc));
            if (!o) break;
            inv_.erase(inv_.begin() + pick);
            uint32_t rest = 0;
            const uint32_t partId = o->id;
            if (!world_->fillPlan(planId, (int)i, partId, msg, nullptr, &rest, &crafted)) {
                inv_.push_back(item);  // ging nicht: wieder zurück, wie es war
                world_->remove(partId);
                break;
            }
            ++used;
            if (rest && !invStore(rest, true)) toast("Reststück passt nicht mehr ins Inventar, es liegt beim Hologramm", false);
        }
    }
    invPushContents();
    if (crafted) {
        craftDone(crafted, msg);
        return true;
    }
    // Was fehlt noch?
    std::string missing;
    if (const BuildPlan* p = world_->findPlan(planId)) {
        double vol[4] = {0, 0, 0, 0};
        std::string label[4];
        for (const PlanPiece& pc : p->pieces) {
            if (p->pieceDone(pc)) continue;
            const int m = std::clamp(pc.mat, 0, 3);
            vol[m] += pc.volume() - pc.filled;
            label[m] = pc.material >= 0 ? materials()[(size_t)pc.material].label : planMatLabel((PlanMat)pc.mat);
        }
        for (int m = 0; m < 4; ++m)
            if (vol[m] > 0) missing += (missing.empty() ? "" : ", ") + label[m] + " " + fmtNum(vol[m] * 1e6, 0) + " cm³";
    }
    if (used) toast(std::to_string(used) + " Stück aus dem Inventar eingesetzt, es fehlt noch: " + missing);
    else toast("Im Inventar ist nichts Passendes. Es fehlt: " + missing, false);
    return true;
}

std::string App::craftNeeds(const PlanTemplate& tp) const {
    // Bedarf je Materialart und was davon im Inventar ist
    double need[4] = {0, 0, 0, 0}, have[4] = {0, 0, 0, 0};
    for (const PlanPieceDef& d : tp.pieces) need[std::clamp((int)d.mat, 0, 3)] += 8.0 * d.half.x * d.half.y * d.half.z;
    for (const InvItem& it : inv_)
        for (int m = 0; m < 4; ++m)
            if (m != (int)PlanMat::Any && planMatFits((PlanMat)m, it.state.material)) have[m] += it.mass / density(it.state.material);
    for (const InvItem& it : inv_) have[(int)PlanMat::Any] += it.mass / density(it.state.material);
    std::string s;
    for (int m : {(int)PlanMat::Wood, (int)PlanMat::Stone, (int)PlanMat::Metal, (int)PlanMat::Any}) {
        if (need[m] <= 0) continue;
        s += (s.empty() ? "" : " + ") + std::string(planMatLabel((PlanMat)m)) + " " + fmtNum(need[m] * 1e6, 0) + " cm³" +
             (have[m] >= need[m] * 0.97 ? " (da)" : " (im Inventar " + fmtNum(have[m] * 1e6, 0) + ")");
    }
    return s;
}
