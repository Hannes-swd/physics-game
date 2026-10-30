// Inventar im Spiel-Modus: E öffnet und schließt es, E mit einem Teil in der Hand (oder F auf ein Teil) steckt es ein.
// Eingesteckte Teile sind nicht mehr in der Welt. Grenze ist das Gewicht, das die Figur tragen kann; im Fenster belegt
// jedes Teil eine Fläche im Raster, die seinem Gewicht entspricht (Form etwa wie die des Teils: ein Stock ist lang und
// schmal, ein Stein fast quadratisch). Rechtsklick nimmt ein Teil heraus: Es hängt dann vor der Figur, R/Shift+R und das
// Mausrad drehen und rücken es, Linksklick setzt es als normales Physik-Teil ab.
#include "App.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "Materials.h"
#include "ObjectIO.h"
#include "Scatter.h"

namespace fs = std::filesystem;

namespace {
constexpr double kInvCapacity = 30.0;  // kg, so viel trägt die Figur
constexpr int kInvCols = 16, kInvRows = 10;
constexpr double kCellKg = kInvCapacity / (kInvCols * kInvRows);
constexpr uint32_t kInvMagic = 0x31564E49;  // "INV1"

std::string kg(double m) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), m < 10 ? "%.2f kg" : "%.1f kg", m);
    return buf;
}

// Fläche im Raster: so viele Zellen, wie das Teil wiegt, Seitenverhältnis wie längste zu zweitlängster Seite
void footprint(double mass, glm::dvec2 ext, int& w, int& h) {
    const int cells = std::clamp((int)std::ceil(mass / kCellKg - 1e-6), 1, kInvCols * kInvRows);
    const double r = std::clamp(ext.y > 1e-6 ? ext.x / ext.y : 1.0, 1.0, (double)kInvCols);
    h = std::clamp((int)std::lround(std::sqrt(cells / r)), 1, kInvRows);
    w = (cells + h - 1) / h;
    if (w > kInvCols) {
        w = kInvCols;
        h = std::min(kInvRows, (cells + w - 1) / w);
    }
}
} // namespace

double App::invMass() const {
    double m = 0;
    for (const InvItem& it : inv_) m += it.mass;
    return m;
}

// Kästchen ins Raster legen: von oben links zeilenweise die erste freie Stelle (auch hochkant). keepPlaced: die schon
// liegenden bleiben, wo sie sind, nur neue (x < 0) werden gelegt; sonst alles neu, die großen zuerst
bool App::invPack(std::vector<InvItem>& items, bool keepPlaced) const {
    std::vector<uint8_t> used(kInvCols * kInvRows, 0);
    auto free = [&](int x, int y, int w, int h) {
        if (x + w > kInvCols || y + h > kInvRows) return false;
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i)
                if (used[j * kInvCols + i]) return false;
        return true;
    };
    auto mark = [&](const InvItem& it) {
        for (int j = it.y; j < it.y + it.h; ++j)
            for (int i = it.x; i < it.x + it.w; ++i) used[j * kInvCols + i] = 1;
    };
    std::vector<InvItem*> todo;
    for (InvItem& it : items) {
        if (keepPlaced && it.x >= 0) mark(it);
        else todo.push_back(&it);
    }
    if (!keepPlaced)
        std::stable_sort(todo.begin(), todo.end(), [](const InvItem* a, const InvItem* b) { return a->w * a->h > b->w * b->h; });
    for (InvItem* it : todo) {
        int w, h;
        footprint(it->mass, it->ext, w, h);
        bool placed = false;
        for (int y = 0; y < kInvRows && !placed; ++y)
            for (int x = 0; x < kInvCols && !placed; ++x)
                for (int turn = 0; turn < (w == h ? 1 : 2) && !placed; ++turn) {
                    const int ww = turn ? h : w, hh = turn ? w : h;
                    if (!free(x, y, ww, hh)) continue;
                    it->x = x, it->y = y, it->w = ww, it->h = hh;
                    mark(*it);
                    placed = true;
                }
        if (!placed) return false;
    }
    return true;
}

bool App::invStore(uint32_t id, bool quiet) {
    if (netClient()) {
        if (!quiet) toast("Inventar: als Mitspieler im LAN noch nicht möglich", false);
        return false;
    }
    Object* o = world_->find(id);
    if (!o || o->fixed || o->body == kNoBody || o->rooted) return false;
    const double m = o->mass;
    if (invMass() + m > kInvCapacity + 1e-9) {
        if (!quiet) toast(o->name + " ist zu schwer fürs Inventar (" + kg(m) + ", frei " + kg(std::max(0.0, kInvCapacity - invMass())) + ")", false);
        return false;
    }
    InvItem it;
    it.key = invNextKey_++;
    it.state = static_cast<const ObjectState&>(*o);
    it.mass = m;
    glm::dvec3 s = o->bbMax - o->bbMin;
    std::sort(&s[0], &s[0] + 3, std::greater<double>());
    it.ext = glm::dvec2(s[0], s[1]);
    it.x = -1;
    std::vector<InvItem> next = inv_;
    next.push_back(it);
    if (!invPack(next, true)) {  // passt nicht in die Lücken: alles neu einräumen
        next = inv_;
        next.push_back(it);
        if (!invPack(next, false)) {
            if (!quiet) toast("Kein Platz mehr im Inventar für " + o->name, false);
            return false;
        }
    }
    inv_ = std::move(next);
    if (hand_.grabbed == id) hand_.grabbed = 0;
    if (inspectTarget_ == id) inspectTarget_ = 0;
    const std::string name = o->name;
    world_->remove(id);
    if (!quiet) toast(name + " im Inventar (" + kg(invMass()) + " / " + kg(kInvCapacity) + ")");
    invPushContents();
    return true;
}

bool App::invTakeOut(uint32_t key) {
    auto itr = std::find_if(inv_.begin(), inv_.end(), [&](const InvItem& i) { return i.key == key; });
    if (itr == inv_.end() || !play_) return false;
    if (hand_.grabbed) hand_.grabbed = 0;  // was man trug, fällt
    const glm::dvec3 eye = playerEye(), fwd = aimForward();
    const glm::dquat yawQ = glm::angleAxis(playYaw_, glm::dvec3(0, 1, 0));
    // vor die Figur, so weit, dass es nicht in ihr steckt, aber nicht in eine Wand
    const double radius = 0.5 * glm::length(itr->ext);
    double dist = std::clamp(radius + 0.45, 0.6, 2.5);
    RayHit h;
    if (world_->raycast(eye, fwd, h, 0) && h.t < dist + radius) dist = std::max(0.4, h.t - radius);
    // so gedreht, wie es im Inventar liegt: längste Seite quer vor der Figur
    glm::dvec3 mn(0), mx(0);
    if (itr->state.kind == ObjKind::Solid) solid::bounds(itr->state.solid, mn, mx);
    const glm::dvec3 s = mx - mn;
    glm::dquat align(1, 0, 0, 0);
    if (s.y > s.x && s.y >= s.z) align = glm::angleAxis(glm::radians(-90.0), glm::dvec3(0, 0, 1));  // Y -> X
    else if (s.z > s.x && s.z > s.y) align = glm::angleAxis(glm::radians(90.0), glm::dvec3(0, 1, 0));  // Z -> X
    Object* o = world_->putBack(itr->state, eye + fwd * dist, yawQ * align);
    if (!o) return false;
    inv_.erase(itr);
    invPushContents();
    hand_.grabbed = o->id;
    hand_.grabLocal = glm::dvec3(0);
    hand_.holdDist = dist;
    hand_.heldByMouse = false;
    hand_.holdRel = align;
    physics_.activate(o->body);
    invOpen(false);
    toast(o->name + ": Linksklick setzt es ab, R / Shift+R dreht, Mausrad rückt, E zurück ins Inventar");
    return true;
}

void App::invOpen(bool on) {
    if (on == invOpen_) return;
    if (on && (!play_ || netClient())) {
        if (netClient()) toast("Inventar: als Mitspieler im LAN noch nicht möglich", false);
        return;
    }
    invOpen_ = on;
    if (on) {
        setCursorCaptured(false);
        invPushContents();
        if (opt_.inventoryUi) opt_.inventoryUi->open(glfwGetWin32Window(window_));
    } else {
        if (opt_.inventoryUi) opt_.inventoryUi->close();
        if (play_ && pause_ == Pause::None) {
            glfwFocusWindow(window_);
            setCursorCaptured(true);
        }
    }
}

void App::invPushContents() {
    if (!opt_.inventoryUi) return;
    std::vector<InventoryUi::Item> items;
    for (const InvItem& it : inv_) {
        InventoryUi::Item v;
        v.key = it.key;
        v.x = it.x, v.y = it.y, v.w = it.w, v.h = it.h;
        v.name = it.state.name;
        const int mat = std::clamp(it.state.material, 0, (int)materials().size() - 1);
        v.info = std::string(materials()[mat].label) + ", " + kg(it.mass);
        items.push_back(v);
    }
    const double m = invMass();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.1f / %.0f kg (%.0f %%)", m, kInvCapacity, 100.0 * m / kInvCapacity);
    opt_.inventoryUi->setContents(kInvCols, kInvRows, items, buf);
}

void App::updateInventory() {
    if (!invOpen_ || !opt_.inventoryUi) return;
    for (int guard = 0; guard < 16; ++guard) {
        const InventoryUi::Event e = opt_.inventoryUi->poll();
        if (e.kind == InventoryUi::Event::None) break;
        if (e.kind == InventoryUi::Event::Close) invOpen(false);
        else if (e.kind == InventoryUi::Event::TakeOut) invTakeOut(e.key);
        if (!invOpen_) break;
    }
}

// Ohne Hauptmenü (--world, --script): dasselbe als einfaches Fenster im Spiel
void App::drawInventoryWindow() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Inventar", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    const double m = invMass();
    ImGui::Text("%.1f / %.0f kg (%.0f %%)", m, kInvCapacity, 100.0 * m / kInvCapacity);
    const float cell = 34.0f;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(o, ImVec2(o.x + cell * kInvCols, o.y + cell * kInvRows), IM_COL32(200, 200, 200, 255), 0, 0, 2.0f);
    uint32_t take = 0;
    for (const InvItem& it : inv_) {
        const ImVec2 a(o.x + it.x * cell + 2, o.y + it.y * cell + 2), b(o.x + (it.x + it.w) * cell - 2, o.y + (it.y + it.h) * cell - 2);
        const bool hover = ImGui::IsMouseHoveringRect(a, b);
        dl->AddRectFilled(a, b, hover ? IM_COL32(120, 120, 120, 255) : IM_COL32(80, 80, 80, 255));
        dl->AddRect(a, b, IM_COL32(20, 20, 20, 255));
        dl->PushClipRect(a, b, true);
        dl->AddText(ImVec2(a.x + 3, a.y + 2), IM_COL32(255, 255, 255, 255), it.state.name.c_str());
        dl->PopClipRect();
        if (hover) {
            ImGui::SetTooltip("%s, %s", it.state.name.c_str(), kg(it.mass).c_str());
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) take = it.key;
        }
    }
    ImGui::Dummy(ImVec2(cell * kInvCols, cell * kInvRows));
    ImGui::TextDisabled("Rechtsklick: herausnehmen   E: schliessen");
    ImGui::End();
    if (take) invTakeOut(take);
}

// Mit der Welt gespeichert: inventory.bin im Ordner der Welt (Material mit Namen, falls sich die Liste ändert)
bool App::invSave(std::string* err) const {
    if (!haveWorld_) return true;
    net::Writer w;
    w.u32(kInvMagic);
    w.u32((uint32_t)inv_.size());
    for (const InvItem& it : inv_) {
        w.str(materials()[std::clamp(it.state.material, 0, (int)materials().size() - 1)].name);
        Object tmp;
        static_cast<ObjectState&>(tmp) = it.state;
        writeObject(w, tmp, true);
    }
    const fs::path file = worldInfo_.dir / L"inventory.bin";
    fs::path tmp = file;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(w.data().data(), (std::streamsize)w.data().size());
        if (!out) {
            if (err) *err = "inventory.bin could not be written";
            return false;
        }
    }
    return worlds::replaceFile(tmp, file, err);
}

void App::invLoad() {
    inv_.clear();
    invNextKey_ = 1;
    if (!haveWorld_) return;
    std::ifstream in(worldInfo_.dir / L"inventory.bin", std::ios::binary);
    if (!in) return;
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    net::Reader r(data);
    if (r.u32() != kInvMagic) return;
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && r.ok(); ++i) {
        const std::string mat = r.str();
        InvItem it;
        if (!readObject(r, it.state, true)) break;
        const int idx = findMaterialIndex(mat.c_str());
        if (idx >= 0) it.state.material = idx;
        // Gewicht und Maße wie in der Welt: einmal als Teil aufbauen wäre teuer, darum aus der Form
        glm::dvec3 mn(1e30), mx(-1e30);
        double vol = 0;
        if (it.state.kind == ObjKind::Solid) {
            solid::bounds(it.state.solid, mn, mx);
            vol = solid::volume(it.state.solid);
        } else if (it.state.imported) {
            vol = it.state.imported->volume;
            for (const solid::Vertex& v : it.state.imported->vertices) {
                mn = glm::min(mn, glm::dvec3(v.px, v.py, v.pz));
                mx = glm::max(mx, glm::dvec3(v.px, v.py, v.pz));
            }
        }
        it.mass = vol * materials()[std::clamp(it.state.material, 0, (int)materials().size() - 1)].density;
        glm::dvec3 s = glm::max(mx - mn, glm::dvec3(0));
        std::sort(&s[0], &s[0] + 3, std::greater<double>());
        it.ext = glm::dvec2(s[0], s[1]);
        it.key = invNextKey_++;
        it.x = -1;
        inv_.push_back(std::move(it));
    }
    if (!invPack(inv_, false)) log("Inventory: not everything fits the grid any more", 2);
}
