// Eingabe: Kamera, Bau-Werkzeuge (Auswahl/Bewegen, Säge, Bohrer) und Spielfigur
#include "App.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "Materials.h"
#include "Scatter.h"

namespace {
constexpr double kMaxLiftForce = 420.0;  // so viel Kraft (N) bringt die Figur beim Tragen auf
}

// ============================================================================ Auswahl

bool App::isSelected(uint32_t id) const { return std::find(selection_.begin(), selection_.end(), id) != selection_.end(); }

void App::toggleSelected(uint32_t id) {
    auto it = std::find(selection_.begin(), selection_.end(), id);
    if (it != selection_.end()) selection_.erase(it);
    else selection_.push_back(id);
}

glm::dvec3 App::selectionCenter() const {
    glm::dvec3 mn(1e30), mx(-1e30);
    for (uint32_t id : selection_)
        if (Object* o = world_->find(id)) {
            mn = glm::min(mn, o->wMin);
            mx = glm::max(mx, o->wMax);
        }
    return mn.x > mx.x ? orbitTarget_ : (mn + mx) * 0.5;
}

void App::rotateSelected(int axis, double deg) {
    if (selection_.empty()) return;
    glm::dvec3 ax(0);
    ax[axis] = 1;
    glm::dquat q = glm::angleAxis(glm::radians(deg), ax);
    glm::dvec3 c = selectionCenter();
    pushUndo();
    for (uint32_t id : selection_)
        if (Object* o = world_->find(id)) world_->setTransform(*o, c + q * (o->pos - c), q * o->rot);
    shareTransforms(selection_);
}

void App::deleteSelected() {
    if (selection_.empty()) return;
    if (netClient()) {  // der Gastgeber löscht (ein Rückgängig-Schritt für alle)
        std::string cmd = "delete";
        for (uint32_t id : selection_) cmd += " " + std::to_string(id);
        runLine(cmd, "tool");
        selection_.clear();
        return;
    }
    world_->pushUndo();
    for (uint32_t id : selection_) world_->remove(id);
    status_ = std::to_string(selection_.size()) + " part(s) deleted";
    statusTime_ = time_;
    selection_.clear();
}

void App::duplicateSelected() {
    if (selection_.empty()) return;
    glm::dvec3 mn(1e30), mx(-1e30);
    for (uint32_t id : selection_)
        if (Object* o = world_->find(id)) {
            mn = glm::min(mn, o->wMin);
            mx = glm::max(mx, o->wMax);
        }
    glm::dvec3 off(mx.x - mn.x + 0.02, 0, 0);
    if (netClient()) {
        std::string cmd;
        for (uint32_t id : selection_)
            cmd += (cmd.empty() ? "copy " : " ; copy ") + std::to_string(id) + " " + fmtNum(off.x * 100, 3) + " 0 0";
        runLine(cmd, "tool");
        return;
    }
    world_->pushUndo();
    std::vector<uint32_t> copies;
    for (uint32_t id : selection_)
        if (Object* c = world_->duplicate(id, off)) copies.push_back(c->id);
    selection_ = copies;
    status_ = std::to_string(copies.size()) + " part(s) copied";
    statusTime_ = time_;
}

void App::dropSelected() {
    if (selection_.empty()) return;
    pushUndo();
    for (uint32_t id : selection_)
        if (Object* o = world_->find(id)) world_->dropToSurface(*o);
    shareTransforms(selection_);
}

glm::dvec3 App::orbitEye() const {
    return orbitTarget_ + orbitDist_ * glm::dvec3(std::cos(orbitPitch_) * std::sin(orbitYaw_), std::sin(orbitPitch_),
                                                  std::cos(orbitPitch_) * std::cos(orbitYaw_));
}

void App::focusSelected() {
    if (selection_.empty()) return;
    glm::dvec3 mn(1e30), mx(-1e30);
    for (uint32_t id : selection_)
        if (Object* o = world_->find(id)) {
            mn = glm::min(mn, o->wMin);
            mx = glm::max(mx, o->wMax);
        }
    orbitTarget_ = (mn + mx) * 0.5;
    orbitDist_ = std::max(0.15, glm::length(mx - mn) * 1.8);
}

// ============================================================================ Eingabe allgemein

void App::handleInput(float dt) {
    if (loadingBlocks()) return;  // Ladebildschirm: keine Eingaben an die Welt
    ImGuiIO& io = ImGui::GetIO();
    const bool typing = io.WantTextInput;
    escTaken_ = false;
    if (!typing) {
        if (invOpen_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {  // offenes Inventar (Fenster im Spiel): zu
            invOpen(false);
            escTaken_ = true;
        }
        // ESC: offene Einstellungen schließen; im Spiel-Modus sonst das Pausenfenster auf bzw. zu
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !escTaken_ && (showSettings_ || play_)) {
            if (showSettings_) showSettings_ = false;
            else setPause(pause_ == Pause::None ? Pause::Menu : Pause::None);
            escTaken_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) setMode(!play_);
        if (ImGui::IsKeyPressed(ImGuiKey_F1, false) || ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) {
            showConsole_ = !showConsole_;
            focusConsole_ = showConsole_;
            if (showConsole_ && play_) setCursorCaptured(false);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) showInspector_ = !showInspector_;
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && play_) thirdPerson_ = !thirdPerson_;
        if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) {
            if (!selection_.empty()) showDrawing(selection_);
            else if (play_ && lookAt_) showDrawing({lookAt_});
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
            std::string name = "foto_" + std::to_string((long long)(time_ * 1000)) + ".png";
            log(screenshot(name), 3);
        }
    }
    if (play_) playInput(dt);
    else buildInput(dt);
    scrollAccum_ = 0.0;

    // Ziel des Physik-Inspektors
    uint32_t target = 0;
    if (play_) target = hand_.grabbed ? hand_.grabbed : lookAt_;
    else target = selectedId() ? selectedId() : hovered_;
    if (target && target != inspectTarget_ && (play_ || selectedId())) inspectTarget_ = target;
    if (inspectTarget_ && !world_->find(inspectTarget_)) inspectTarget_ = 0;
    world_->setInspect(showInspector_ ? inspectTarget_ : 0);
}

// ============================================================================ Bau-Modus

void App::buildInput(float) {
    ImGuiIO& io = ImGui::GetIO();
    const double mx = io.MousePos.x * io.DisplayFramebufferScale.x, my = io.MousePos.y * io.DisplayFramebufferScale.y;
    const double dx = mx - lastMx_, dy = my - lastMy_;
    lastMx_ = mx;
    lastMy_ = my;
    const bool overUI = io.WantCaptureMouse;
    if (views_.empty()) return;
    if (!rmbDown_ && !mmbDown_ && !dragging_) activeView_ = viewAt(mx, my);
    const View& v = views_[std::clamp(activeView_, 0, (int)views_.size() - 1)];

    // --- Kamera
    if (!overUI && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) rmbDown_ = true;
    if (!overUI && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) mmbDown_ = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) rmbDown_ = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) mmbDown_ = false;
    auto pan = [&](double px, double py) {
        double wpp = (v.ortho ? 2.0 * v.halfHeight : 2.0 * orbitDist_ * std::tan(fov_ / 2)) / v.h;
        glm::dvec3 fwd = glm::normalize(v.target - v.eye);
        glm::dvec3 right = glm::normalize(glm::cross(fwd, v.up));
        glm::dvec3 up = glm::normalize(glm::cross(right, fwd));
        orbitTarget_ += (-right * px + up * py) * wpp;
    };
    if (rmbDown_) {
        if (v.ortho || io.KeyShift) {
            pan(dx, dy);
        } else {
            orbitYaw_ -= dx * 0.006;
            orbitPitch_ = std::clamp(orbitPitch_ + dy * 0.006, -1.55, 1.55);
        }
    }
    if (mmbDown_) pan(dx, dy);
    if (!overUI && io.MouseWheel != 0.0f) orbitDist_ = std::clamp(orbitDist_ * std::pow(0.87, (double)io.MouseWheel), 0.03, 20000.0);

    // --- Tasten
    if (!io.WantTextInput) {
        const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) viewMode_ = ViewMode::Perspective;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) viewMode_ = ViewMode::Top;
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) viewMode_ = ViewMode::Front;
        if (ImGui::IsKeyPressed(ImGuiKey_4, false)) viewMode_ = ViewMode::Side;
        if (ImGui::IsKeyPressed(ImGuiKey_5, false)) viewMode_ = ViewMode::Quad;
        if (ctrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                if (netClient()) runLine("undo", "key");
                else log(world_->undo() ? "Undone" : "Nothing to undo", 3);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                if (netClient()) runLine("redo", "key");
                else log(world_->redo() ? "Redone" : "Nothing to redo", 3);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected();
            if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
                selection_.clear();
                for (auto& o : world_->objects()) selection_.push_back(o->id);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                if (haveWorld_) saveWorld(false);
                else saveDialog(true);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) saveDialog(false);
        } else {
            if (ImGui::IsKeyPressed(ImGuiKey_V, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false)) tool_ = Tool::Select;
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) tool_ = Tool::Saw;
            if (ImGui::IsKeyPressed(ImGuiKey_B, false)) tool_ = Tool::Drill;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) tool_ = Tool::Round;
            if (ImGui::IsKeyPressed(ImGuiKey_M, false)) tool_ = Tool::Mill;
            if (ImGui::IsKeyPressed(ImGuiKey_P, false)) tool_ = Tool::Plane;
            if (ImGui::IsKeyPressed(ImGuiKey_H, false)) tool_ = Tool::Heat;
            if (tool_ == Tool::Saw) {
                if (ImGui::IsKeyPressed(ImGuiKey_X, false)) sawAxis_ = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) sawAxis_ = 1;
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) sawAxis_ = 2;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelected();
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) focusSelected();
            if (ImGui::IsKeyPressed(ImGuiKey_G, false)) dropSelected();
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                int axis = 1;
                if (v.mode == ViewMode::Front) axis = 2;
                if (v.mode == ViewMode::Side) axis = 0;
                if (shift) axis = axis == 0 ? 2 : 0;  // Y/Z -> X, X -> Z
                rotateSelected(axis, 90.0);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !escTaken_) {
                if (tool_ != Tool::Select) tool_ = Tool::Select;
                else selection_.clear();
            }
        }
    }

    // --- Was liegt unter der Maus?
    hoverValid_ = false;
    hovered_ = 0;
    glm::dvec3 ro, rd;
    if (!overUI && !rmbDown_ && !mmbDown_ && mouseRay(v, mx, my, ro, rd)) {
        hoverValid_ = world_->raycast(ro, rd, hoverHit_);
        if (hoverValid_ && hoverHit_.obj) hovered_ = hoverHit_.obj->id;
    }

    // --- Linke Maustaste
    if (!overUI && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (tool_ == Tool::Select) {
            const bool additive = io.KeyShift || io.KeyCtrl;
            if (hovered_) {
                if (additive) toggleSelected(hovered_);
                else if (!isSelected(hovered_)) select(hovered_);
                else {  // nach vorne holen (Hauptauswahl)
                    toggleSelected(hovered_);
                    selection_.push_back(hovered_);
                }
                if (isSelected(hovered_)) {
                    dragCandidate_ = true;
                    dragView_ = activeView_;
                    dragStartMx_ = mx;
                    dragStartMy_ = my;
                    dragStartHit_ = hoverHit_.point;
                    dragPlaneP_ = hoverHit_.point;
                    dragPlaneN_ = v.ortho ? glm::normalize(v.eye - v.target) : glm::dvec3(0, 1, 0);
                    dragVertical_ = false;
                    dragStarts_.clear();
                    for (uint32_t id : selection_)
                        if (Object* o = world_->find(id)) dragStarts_.emplace_back(id, o->pos);
                }
            } else if (!additive) {
                selection_.clear();
            }
        } else {
            applyTool();
        }
    }
    if (dragCandidate_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && dragView_ >= 0 && dragView_ < (int)views_.size()) {
        const View& dv = views_[dragView_];
        if (!dragging_ && std::hypot(mx - dragStartMx_, my - dragStartMy_) > 4.0) {
            dragging_ = true;
            pushUndo();
        }
        if (dragging_ && mouseRay(dv, mx, my, ro, rd)) {
            const bool vertical = !dv.ortho && io.KeyShift;
            auto intersect = [&](const glm::dvec3& p0, const glm::dvec3& n, glm::dvec3& hit) {
                double den = glm::dot(rd, n);
                if (std::abs(den) < 1e-9) return false;
                double t = glm::dot(p0 - ro, n) / den;
                if (t < 0) return false;
                hit = ro + rd * t;
                return true;
            };
            glm::dvec3 n = dragPlaneN_;
            if (vertical) {
                glm::dvec3 h = dv.eye - dragPlaneP_;
                h.y = 0;
                n = glm::length(h) > 1e-9 ? glm::normalize(h) : glm::dvec3(0, 0, 1);
            }
            if (vertical != dragVertical_) {  // Umschalten: neu ansetzen
                dragVertical_ = vertical;
                glm::dvec3 hit;
                if (intersect(dragPlaneP_, n, hit)) dragStartHit_ = hit;
                dragStarts_.clear();
                for (uint32_t id : selection_)
                    if (Object* o = world_->find(id)) dragStarts_.emplace_back(id, o->pos);
            }
            glm::dvec3 hit;
            if (intersect(dragPlaneP_, n, hit)) {
                glm::dvec3 delta = hit - dragStartHit_;
                if (vertical) delta = glm::dvec3(0, delta.y, 0);
                if (snap_ && !io.KeyAlt) {
                    double s = snapStep();
                    delta = glm::round(delta / s) * s;
                }
                if (world_->buildCollision() && !io.KeyCtrl && !dragStarts_.empty()) {
                    // Teile halten an anderen an und gleiten daran entlang (Strg = durch alles hindurch)
                    Object* first = world_->find(dragStarts_[0].first);
                    if (first) {
                        glm::dvec3 move = dragStarts_[0].second + delta - first->pos;
                        std::vector<uint32_t> ids;
                        for (auto& [id, p0] : dragStarts_) ids.push_back(id);
                        uint32_t blocker = 0;
                        bool blocked = false;
                        world_->sweepMove(ids, move, &blocker, &blocked);
                        if (blocked) {
                            Object* b = world_->find(blocker);
                            status_ = "touching " + (b ? b->name : std::string("the floor")) + " (hold Ctrl to move through)";
                            statusTime_ = time_;
                        }
                    }
                } else {
                    for (auto& [id, p0] : dragStarts_)
                        if (Object* o = world_->find(id)) world_->setTransform(*o, p0 + delta, o->rot);
                }
                std::vector<uint32_t> ids;
                for (auto& [id, p0] : dragStarts_) ids.push_back(id);
                shareTransforms(ids);
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragCandidate_ = dragging_ = false;
}

// ============================================================================ Werkzeuge

void App::updatePreview() {
    preview_.valid = false;
    if (play_ || tool_ == Tool::Select || !hoverValid_ || !hoverHit_.obj) return;
    Object* o = hoverHit_.obj;
    if (o->kind != ObjKind::Solid) return;
    preview_.objId = o->id;
    const glm::dvec3 lp = hoverHit_.localPoint;
    const glm::dvec3 c = (o->bbMin + o->bbMax) * 0.5, size = o->bbMax - o->bbMin;
    const double step = snapStep();
    if (tool_ == Tool::Saw) {
        const int ax = sawAxis_;
        double s = lp[ax] - o->bbMin[ax];
        if (snap_) s = std::round(s / step) * step;
        s = std::clamp(s, 0.0, size[ax]);
        glm::dvec3 q = c;
        q[ax] = o->bbMin[ax] + s;
        glm::dvec3 n(0);
        n[ax] = 1;
        if (std::abs(sawAngle_) > 1e-3)
            n = glm::angleAxis(glm::radians((double)sawAngle_), ax == 1 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0)) * n;
        preview_.plane.n = glm::normalize(n);
        preview_.plane.d = glm::dot(preview_.plane.n, q);
        preview_.distFromEnd = s;
        preview_.valid = true;
    } else if (tool_ == Tool::Drill) {
        glm::dvec3 n = hoverHit_.localNormal, p = lp;
        if (snap_)
            for (int k = 0; k < 3; ++k)
                if (std::abs(n[k]) < 0.9) p[k] = c[k] + std::round((p[k] - c[k]) / step) * step;
        preview_.from = p + n * 0.001;
        preview_.dir = -n;
        preview_.length = drillDepthMm_ > 0 ? drillDepthMm_ / 1000.0 + 0.001 : glm::length(size) * 1.05;
        preview_.valid = true;
    } else if (tool_ == Tool::Round) {
        // nächste Kante zum Mauspunkt, bevorzugt an der Fläche unter der Maus
        const auto& edges = world_->edgesOf(*o);
        const double minExt = std::min({size.x, size.y, size.z});
        const double reach = std::clamp(0.3 * minExt, 0.004, 0.03);
        double best = 1e30;
        preview_.edge = -1;
        for (size_t i = 0; i < edges.size(); ++i) {
            const solid::Edge& e = edges[i];
            glm::dvec3 ab = e.b - e.a;
            double t = std::clamp(glm::dot(lp - e.a, ab) / std::max(1e-18, glm::dot(ab, ab)), 0.0, 1.0);
            double d = glm::length(lp - (e.a + ab * t));
            const glm::dvec3& n = hoverHit_.localNormal;
            if (glm::dot(e.n1, n) < 0.9 && glm::dot(e.n2, n) < 0.9) d *= 1.5;  // Kante gehört nicht zur Fläche unter der Maus
            if (d < best) {
                best = d;
                preview_.edge = (int)i;
            }
        }
        preview_.valid = preview_.edge >= 0 && best < reach;
    } else if (tool_ == Tool::Mill || tool_ == Tool::Plane) {
        const glm::dvec3& n = hoverHit_.localNormal;
        int k = 0;
        for (int i = 1; i < 3; ++i)
            if (std::abs(n[i]) > std::abs(n[k])) k = i;
        if (std::abs(n[k]) < 0.99) return;  // nur ebene, achsparallele Flächen
        preview_.faceAxis = k;
        preview_.faceSign = n[k] > 0 ? 1 : -1;
        const int u = k == 0 ? 1 : 0, v = k == 2 ? 1 : 2;
        double a = lp[u] - c[u], b = lp[v] - c[v];
        if (snap_) {
            a = std::round(a / step) * step;
            b = std::round(b / step) * step;
        }
        preview_.a = a;
        preview_.b = b;
        preview_.valid = true;
    }
}

void App::applyTool() {
    if (!preview_.valid) return;
    static const char* faces[3][2] = {{"-x", "+x"}, {"-y", "+y"}, {"-z", "+z"}};
    const std::string id = std::to_string(preview_.objId);
    // Die neuen Werkzeuge laufen als Befehl: Er steht in der Konsole und lässt sich so auch abschreiben.
    if (tool_ == Tool::Round) {
        runLine(std::string(roundChamfer_ ? "chamfer " : "round ") + id + " e" + std::to_string(preview_.edge) + " " + fmtNum(roundMm_, 2), "tool");
        return;
    }
    if (tool_ == Tool::Mill || tool_ == Tool::Plane) {
        const char* f = faces[preview_.faceAxis][preview_.faceSign > 0 ? 1 : 0];
        if (tool_ == Tool::Plane) runLine("plane " + id + " " + f + " " + fmtNum(planeMm_, 2), "tool");
        else
            runLine("pocket " + id + " " + f + " " + fmtNum(preview_.a * 100, 2) + " " + fmtNum(preview_.b * 100, 2) + " " + fmtNum(millDims_[0], 1) +
                        " " + fmtNum(millDims_[1], 1) + " " + fmtNum(millDims_[2], 1) + (millCornerMm_ > 0 ? " " + fmtNum(millCornerMm_, 1) : ""),
                    "tool");
        return;
    }
    if (netClient()) {  // Mitspieler: Sägen und Bohren als Befehl, der Gastgeber führt sie aus
        auto n = [](double v) { return fmtNum(v, 6); };
        if (tool_ == Tool::Saw)
            runLine("cut " + id + " " + std::string(1, "xyz"[sawAxis_]) + " " + n(preview_.distFromEnd * 100) + " " + n(sawAngle_) + " " +
                        n(sawKerfMm_),
                    "tool");
        else {
            const glm::dvec3 f = preview_.from * 100.0, d = glm::normalize(preview_.dir);
            runLine("drillat " + id + " " + n(f.x) + " " + n(f.y) + " " + n(f.z) + " " + n(d.x) + " " + n(d.y) + " " + n(d.z) + " " +
                        n(drillDiaMm_) + (drillDepthMm_ > 0 ? " " + n(drillDepthMm_ + 1.0) : ""),
                    "tool");
        }
        return;
    }
    std::vector<uint32_t> ids;
    std::string err;
    world_->pushUndo();
    bool ok;
    if (tool_ == Tool::Saw) {
        ok = world_->cut(preview_.objId, preview_.plane, sawKerfMm_ / 1000.0, &ids, &err);
        if (ok) status_ = "Cut at " + fmtNum(preview_.distFromEnd * 100, 1) + " cm -> " + std::to_string(ids.size()) + " parts";
    } else {
        double removed = 0;
        ok = world_->drill(preview_.objId, preview_.from, preview_.dir, drillDiaMm_ / 1000.0,
                           drillDepthMm_ > 0 ? drillDepthMm_ / 1000.0 + 0.001 : 0.0, &ids, &removed, &err);
        if (ok) status_ = "Drilled: D " + fmtNum(drillDiaMm_, 1) + " mm, " + fmtNum(removed * 1e6, 2) + " cm³ of chips";
    }
    if (!ok) {
        world_->discardUndo();
        status_ = "Not possible: " + err;
    } else if (!ids.empty()) {
        select(ids[0]);
    }
    statusTime_ = time_;
    log(status_, ok ? 3 : 2);
}

// ============================================================================ Spiel-Modus

void App::setCursorCaptured(bool on) {
    cursorCaptured_ = on;
    glfwSetInputMode(window_, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (on && glfwRawMouseMotionSupported()) glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwGetCursorPos(window_, &cursorX_, &cursorY_);
    ImGuiIO& io = ImGui::GetIO();
    if (on) io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange);
}

void App::enterPlay() {
    if (play_) return;
    // Figur dort absetzen, wo die Baukamera hinschaut
    glm::dvec3 fwd = orbitTarget_ - orbitEye();
    fwd.y = 0;
    fwd = glm::length(fwd) > 1e-6 ? glm::normalize(fwd) : glm::dvec3(0, 0, -1);
    glm::dvec3 feet = orbitTarget_ - fwd * 2.5;
    RayHit h;
    double groundY = world_->groundHeight(feet.x, feet.z);  // Gelände (flache Welt: 0)
    if (world_->raycast(glm::dvec3(feet.x, std::max(orbitTarget_.y, groundY) + 20.0, feet.z), glm::dvec3(0, -1, 0), h)) groundY = std::max(groundY, h.point.y);
    feet.y = groundY + 0.05;
    if (!netClient()) world_->prepareGround(feet);  // Gelände: Physik dort schon jetzt, die Figur fällt nicht durch
    if (netClient()) {  // die Figur steht beim Gastgeber: dort absetzen lassen
        netSpawn_ = true;
        netSpawnFeet_ = netFeet_ = netFeetTarget_ = feet;
    } else if (!physics_.hasCharacter()) {
        physics_.createCharacter(feet);
    } else {
        physics_.setCharacterPosition(feet);
    }
    playYaw_ = std::atan2(-fwd.x, -fwd.z);
    {  // direkt auf das Ziel schauen
        glm::dvec3 eyeP = feet + glm::dvec3(0, 1.65, 0);
        glm::dvec3 to = orbitTarget_ - eyeP;
        playPitch_ = std::clamp(std::atan2(to.y, glm::length(glm::dvec2(to.x, to.z))), -1.2, 1.2);
    }
    play_ = true;
    dragCandidate_ = dragging_ = false;
    preview_.valid = false;
    if (!netClient()) world_->setSimulating(true);
    setCursorCaptured(true);
    log("Play mode: WASD walk, left mouse grab, right mouse throw, Tab back", 3);
}

void App::leavePlay() {
    if (!play_) return;
    invOpen(false);
    if (pause_ != Pause::None && opt_.pauseUi) {  // das Pausenfenster gehört zum Spiel-Modus
        opt_.pauseUi->close();
        glfwFocusWindow(window_);
    }
    pause_ = Pause::None;
    pauseStopped_ = false;
    glm::dvec3 eye = playerEye(), fwd = playerForward();
    play_ = false;
    hand_.grabbed = 0;
    netWish_ = glm::vec3(0);
    // Figur weg: Läuft die Physik weiter (Mitspieler), wäre sie sonst ein unsichtbares Hindernis
    if (!netClient()) physics_.destroyCharacter();
    if (!netClient() && !peersPlaying()) world_->setSimulating(false);  // Mitspieler laufen noch: Physik weiter
    setCursorCaptured(false);
    orbitDist_ = 2.5;
    orbitTarget_ = eye + fwd * orbitDist_;
    orbitYaw_ = playYaw_;
    orbitPitch_ = -playPitch_;
}

void App::playInput(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    double cx, cy;
    glfwGetCursorPos(window_, &cx, &cy);
    if (cursorCaptured_) {
        playYaw_ -= (cx - cursorX_) * 0.0024 * mouseSens_;
        playPitch_ = std::clamp(playPitch_ - (cy - cursorY_) * 0.0024 * mouseSens_, -1.52, 1.52);
    }
    cursorX_ = cx;
    cursorY_ = cy;

    if (pause_ != Pause::None || showSettings_) return;  // Pausenfenster oder Einstellungen offen: keine Spielsteuerung
    if (invOpen_) {  // Inventar offen: E schließt es (Fenster im Spiel; das Qt-Fenster fängt E selbst ab)
        if (!opt_.inventoryUi && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_E, false)) invOpen(false);
        return;
    }
    if (!cursorCaptured_) {
        if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !io.WantCaptureMouse) setCursorCaptured(true);
        return;
    }

    // Wohin schaue ich?
    const glm::dvec3 eye = playerEye(), fwd = aimForward();
    RayHit h;
    lookAt_ = 0;
    lookHit_ = RayHit();
    if (world_->raycast(eye, fwd, h, hand_.grabbed) && h.obj && h.t < 4.0) {
        lookAt_ = h.obj->id;
        lookDist_ = h.t;
        lookHit_ = h;
    }

    // Maustasten direkt von GLFW (ImGui ignoriert die Maus, solange sie gefangen ist)
    const bool lmbDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool rmbDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    // Axt: X nimmt sie (oder legt sie weg); dann hackt die linke Maustaste statt zu tragen
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
        axe_ = !axe_;
        if (axe_) planMode_ = false;
        toast(axe_ ? "Axt in der Hand: Linksklick auf Holz hackt (X legt sie weg)" : "Axt weggelegt");
    }
    // F: das Teil, auf das man schaut (auch Zeug am Boden), direkt ins Inventar
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false) && !planMode_ && !netClient()) {
        RayHit h;
        const bool hitPart = world_->raycast(eye, fwd, h, 0) && h.obj && h.t <= 3.5;
        ScatterSpot spot;
        double ts = 0;
        Object* o = nullptr;
        if (world_->pickScatter(eye, fwd, hitPart ? h.t : 3.5, spot, ts)) o = world_->takeScatter(spot);
        else if (hitPart) o = h.obj;
        if (o && o->fixed) world_->wakeScatter(*o);
        if (o && !o->fixed) invStore(o->id);
        else if (o) toast(o->name + " ist fest und lässt sich nicht einstecken", false);
    }
    // Bauvorlagen: B Vorlage setzen (dann setzt die linke Maustaste, rechts beendet), Q halten baut ab
    planInput(dt, eye, fwd, lmbDown && !prevLmb_, rmbDown && !prevRmb_);
    if (axe_ && lmbDown && !prevLmb_ && time_ >= axeReady_) {
        axeReady_ = time_ + 0.4;  // ein Hieb braucht etwas Zeit
        if (lookHit_.obj && lookHit_.t < 2.6) {
            const glm::dvec3 p = lookHit_.point, back = eye - p;
            runLine("chop " + std::to_string(lookHit_.obj->id) + " " + fmtNum(p.x * 100, 1) + " " + fmtNum(p.y * 100, 1) + " " +
                        fmtNum(p.z * 100, 1) + " " + fmtNum(back.x, 3) + " " + fmtNum(back.z, 3),
                    "axe");
        }
    }
    HandInput in;
    in.lmbDown = lmbDown && !axe_;
    in.lmbPresses = lmbDown && !prevLmb_ && !axe_;
    in.rmbPresses = rmbDown && !prevRmb_;
    prevLmb_ = lmbDown;
    prevRmb_ = rmbDown;
    in.ePresses = !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_E, false);
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_R, false)) ++(io.KeyShift ? in.tilts : in.turns);
    in.scroll = scrollAccum_;
    if (planMode_) {  // die Maus setzt Vorlagen, R dreht sie: nicht tragen, werfen, drehen
        in.lmbDown = false;
        in.lmbPresses = in.rmbPresses = in.turns = in.tilts = 0;
        in.scroll = 0;
    }
    if (netClient()) {  // Tragen und Werfen rechnet der Gastgeber: sammeln, netUpdate schickt es
        netHand_.lmbDown = in.lmbDown;
        netHand_.lmbPresses += in.lmbPresses;
        netHand_.rmbPresses += in.rmbPresses;
        netHand_.ePresses += in.ePresses;
        netHand_.turns += in.turns;
        netHand_.tilts += in.tilts;
        netHand_.scroll += in.scroll;
        return;
    }
    handActions(hand_, eye, fwd, playYaw_, in);
}

void App::handActions(Hand& hand, const glm::dvec3& eye, const glm::dvec3& fwd, double yaw, const HandInput& in) {
    auto tryGrab = [&](bool byMouse) {
        RayHit h;
        const bool hitPart = world_->raycast(eye, fwd, h, 0) && h.obj && h.t <= 3.5;
        // Zeug am Boden (Steine, Stöcke, Stämme, Stümpfe): Kleines ist nur Bild, bis man es nimmt (dann ein echtes Teil);
        // Großes ist fest, bis man es anfasst (dann beweglich, meist aber zu schwer zum Tragen; Stümpfe bleiben fest)
        ScatterSpot spot;
        double ts = 0;
        Object* o = nullptr;
        glm::dvec3 at;
        if (world_->pickScatter(eye, fwd, hitPart ? h.t : 3.5, spot, ts) && (o = world_->takeScatter(spot))) {
            at = eye + fwd * ts;
        } else if (hitPart) {
            o = h.obj;
            at = h.point;
            ts = h.t;
        }
        if (!o) return;
        if (o->fixed) world_->wakeScatter(*o);
        if (o->fixed || o->body == kNoBody) return;
        hand.grabbed = o->id;
        hand.grabLocal = glm::conjugate(o->rot) * (at - o->pos);
        hand.holdDist = std::max(0.5, ts);
        hand.heldByMouse = byMouse;
        // Lage merken (relativ zur Blickrichtung): das Teil bleibt so in der Hand, statt am Griffpunkt zu pendeln
        hand.holdRel = glm::conjugate(glm::angleAxis(yaw, glm::dvec3(0, 1, 0))) * o->rot;
        physics_.activate(o->body);
    };
    // Maus links halten = tragen, E = aufnehmen/ablegen ohne Halten
    // Trägt man etwas und schaut auf ein Hologramm einer Bauvorlage, setzt E es dort ein (zu Großes wird abgesägt)
    auto tryInsert = [&]() {
        RayHit h;
        const double maxT = world_->raycast(eye, fwd, h, hand.grabbed) ? std::min(4.5, h.t) : 4.5;
        uint32_t plan;
        int piece;
        double t;
        if (!world_->pickPlan(eye, fwd, maxT, plan, piece, t)) return false;
        world_->pushUndo();
        std::string msg;
        uint32_t rest = 0, crafted = 0;
        const bool ok = world_->fillPlan(plan, piece, hand.grabbed, msg, nullptr, &rest, &crafted);
        if (!ok) world_->dropUndo();
        if (ok) {  // der abgesägte Rest bleibt in der Hand (an seiner Mitte gehalten)
            hand.grabbed = rest;
            hand.grabLocal = glm::dvec3(0);
        }
        if (crafted && &hand == &hand_) {
            world_->dropUndo();  // das Ergebnis ist im Inventar: Rückgängig würde es verdoppeln
            craftDone(crafted, msg);
        } else if (&hand == &hand_) {
            toast(msg, ok);
        }
        return true;
    };
    // Leere Hand, Blick auf ein Rezept-Hologramm: E füllt es mit passendem Material aus dem Inventar
    auto tryCraftFill = [&]() {
        RayHit h;
        const double maxT = world_->raycast(eye, fwd, h, 0) ? std::min(4.5, h.t) : 4.5;
        uint32_t plan;
        int piece;
        double t;
        return world_->pickPlan(eye, fwd, maxT, plan, piece, t) && craftFill(plan);
    };
    // Eigene Figur: E mit einem Teil in der Hand steckt es ins Inventar (außer auf ein Hologramm), ohne öffnet E das
    // Inventar. Mitspieler (kein Inventar): E nimmt auf und legt ab wie bisher
    const bool own = &hand == &hand_;
    for (int i = 0; i < in.ePresses; ++i) {
        if (hand.grabbed) {
            if (!tryInsert() && !(own && invStore(hand.grabbed)) && !own) hand.grabbed = 0;
        } else if (own) {
            if (!tryCraftFill()) invOpen(true);
        } else {
            tryGrab(false);
        }
    }
    if (in.ePresses == 0 && in.lmbPresses > 0 && hand.grabbed && !hand.heldByMouse) {  // ohne Halten getragen: hier absetzen
        if (Object* o = world_->find(hand.grabbed)) physics_.setVelocity(o->body, glm::vec3(0), glm::vec3(0));
        hand.grabbed = 0;
    } else if (in.ePresses == 0 && in.lmbPresses > 0 && !hand.grabbed) {
        tryGrab(true);
    }
    if (hand.grabbed && hand.heldByMouse && !in.lmbDown) hand.grabbed = 0;
    if (in.rmbPresses > 0 && hand.grabbed) {  // werfen
        if (Object* o = world_->find(hand.grabbed)) {
            double m = std::max(0.01, (double)physics_.mass(o->body));
            double j = std::min(m * 9.0, 45.0);
            physics_.addImpulseAt(o->body, glm::vec3(fwd * j), o->pos + o->rot * hand.grabLocal);
        }
        hand.grabbed = 0;
    }
    if (!hand.grabbed) return;
    if (in.scroll != 0.0) hand.holdDist = std::clamp(hand.holdDist + in.scroll * 0.15, 0.4, 3.5);
    // R: getragenes Teil um 15° drehen (um die Hochachse), Shift+R: nach vorne kippen (um die Querachse)
    for (int i = 0; i < in.turns; ++i) hand.holdRel = glm::normalize(glm::angleAxis(glm::radians(15.0), glm::dvec3(0, 1, 0)) * hand.holdRel);
    for (int i = 0; i < in.tilts; ++i) hand.holdRel = glm::normalize(glm::angleAxis(glm::radians(15.0), glm::dvec3(1, 0, 0)) * hand.holdRel);
}

void App::grabForces(float) { holdForces(hand_, playerEye(), aimForward(), playYaw_); }

void App::holdForces(Hand& hand, const glm::dvec3& eye, const glm::dvec3& fwd, double yaw) {
    hand.tooHeavy = false;
    if (!hand.grabbed) return;
    Object* o = world_->find(hand.grabbed);
    if (!o || o->body == kNoBody || o->fixed) {
        hand.grabbed = 0;
        return;
    }
    glm::dvec3 pos;
    glm::dquat rot;
    physics_.getTransform(o->body, pos, rot);
    const glm::dvec3 p = pos + rot * hand.grabLocal;
    const glm::dvec3 target = eye + fwd * hand.holdDist;
    if (glm::length(target - p) > 2.0) {  // hängt fest -> loslassen
        hand.grabbed = 0;
        return;
    }
    const double m = std::max(0.01, (double)physics_.mass(o->body));
    const glm::dvec3 v = glm::dvec3(physics_.pointVelocity(o->body, p));
    const double w = 12.0;  // Federsteifigkeit (rad/s) des "Arms"
    glm::dvec3 f = m * (w * w * (target - p) - 2.0 * w * v) + glm::dvec3(0, m * 9.81, 0);
    if (glm::length(f) > kMaxLiftForce) {
        f = glm::normalize(f) * kMaxLiftForce;
        hand.tooHeavy = m * 9.81 > kMaxLiftForce * 0.95;
    }
    physics_.addForceAt(o->body, glm::vec3(f), p);
    world_->addExternalForce(o->id, f);
    // Lage halten: Drehfeder zur gemerkten Lage (dreht mit der Blickrichtung), begrenzt auf das Moment, das man mit
    // zwei Händen aufbringt. Lange, schwere Teile (Tür, Balken) lassen sich so nur langsam drehen und kippen.
    BodyState s;
    if (physics_.bodyState(o->body, s) && s.dynamic && std::abs(glm::determinant(s.invInertiaWorld)) > 1e-30) {
        const glm::dquat target = glm::angleAxis(yaw, glm::dvec3(0, 1, 0)) * hand.holdRel;
        glm::dquat err = target * glm::conjugate(rot);
        if (err.w < 0) err = -err;
        const double sinHalf = glm::length(glm::dvec3(err.x, err.y, err.z));
        const glm::dvec3 axis = sinHalf > 1e-9 ? glm::dvec3(err.x, err.y, err.z) / sinHalf : glm::dvec3(0);
        const double angle = 2.0 * std::atan2(sinHalf, err.w);
        const double wr = 8.0;  // Drehfeder (rad/s)
        glm::dvec3 torque = glm::inverse(s.invInertiaWorld) * (axis * (wr * wr * angle) - s.angVel * (2.0 * wr));
        const double maxTorque = 60.0;  // N·m
        if (glm::length(torque) > maxTorque) torque *= maxTorque / glm::length(torque);
        physics_.addTorque(o->body, glm::vec3(torque));
    }
}

// Läuft, solange jemand spielt: eigene Figur und/oder Mitspieler. Beim Mitspieler nur die Eingabe merken.
void App::updatePlay(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    glm::vec3 wish(0);
    bool jump = jumpRequest_;
    jumpRequest_ = false;
    const glm::vec3 fwd(-std::sin(playYaw_), 0, -std::cos(playYaw_));
    const glm::vec3 right(std::cos(playYaw_), 0, -std::sin(playYaw_));
    bool run = false;
    if (play_ && cursorCaptured_ && !io.WantTextInput) {
        if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) wish += fwd;
        if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) wish -= fwd;
        if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) wish += right;
        if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) wish -= right;
        run = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
        jump = jump || ImGui::IsKeyPressed(ImGuiKey_Space, false);
    }
    if (play_ && autoMoveTime_ > 0) {  // per Befehl "spieler laufe"
        wish += fwd * (float)autoMove_.x + right * (float)autoMove_.y;
        run = run || autoRun_;
        autoMoveTime_ = std::max(0.0, autoMoveTime_ - dt);
    }
    if (glm::length(wish) > 0) wish = glm::normalize(wish) * (run ? 7.0f : 4.0f);
    // Wasser: Waten bremst, ab der Brust schwimmt die Figur. W mit Blick nach unten taucht, Leertaste hoch, Strg hinunter
    const float swim = swimDepth(netClient() ? netFeet_ : physics_.characterPosition());
    if (swim > 0.5f) {
        wish *= 0.5f;
        if (play_ && cursorCaptured_ && !io.WantTextInput) {
            if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) {
                wish.x *= std::cos((float)playPitch_);
                wish.z *= std::cos((float)playPitch_);
                wish.y += std::sin((float)playPitch_) * (run ? 3.5f : 2.0f);
            }
            if (glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS) wish.y += 1.5f;
            if (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) wish.y -= 1.5f;
        }
        jump = false;
    } else if (swim > 0.25f) {
        wish *= 1.0f - (swim - 0.25f) * 1.6f;
    }
    if (netClient()) {  // der Gastgeber bewegt die Figur
        netWish_ = wish;
        netJump_ = netJump_ || jump;
        return;
    }
    world_->step(dt, [&](float h) {
        if (play_) grabForces(h);
        peerForces(h);
    });
    if (play_) {
        BodyHandle ignore = kNoBody;
        if (Object* g = world_->find(hand_.grabbed)) ignore = g->body;
        physics_.updateCharacter(dt, wish, jump, ignore, 0, swim);
        // Stufe hinauf: die Figur steht sofort oben, die Kamera gleitet nach (schneller, als man laufen oder springen kann)
        const double y = physics_.characterPosition().y, dy = y - lastFeetY_;
        if (physics_.characterOnGround() && dy > 0.04 && dy < 0.5 && dy > 8.0 * dt) eyeLag_ -= dy;
        lastFeetY_ = y;
        eyeLag_ = std::max(-0.5, eyeLag_ * std::exp(-dt / 0.09));
    }
    updatePeerFigures(dt);
}
