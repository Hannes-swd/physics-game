#include "Drawing.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <tuple>

#include "Materials.h"
#include "World.h"

namespace {

using dvec2 = glm::dvec2;
using dvec3 = glm::dvec3;

struct ViewDef {
    dvec3 toViewer, right, up;
    const char* name;
};
// Projektionsmethode 1 (Europa)
const ViewDef kViews[3] = {
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, "Vorderansicht"},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, "Draufsicht"},
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, "Seitenansicht von links"},
};

dvec2 proj(const ViewDef& v, const dvec3& p) { return {glm::dot(p, v.right), glm::dot(p, v.up)}; }

std::string mm(double meters) {
    double v = meters * 1000.0;
    double r = std::round(v * 10.0) / 10.0;
    char buf[32];
    if (std::abs(r - std::round(r)) < 1e-6) std::snprintf(buf, sizeof(buf), "%.0f", r);
    else std::snprintf(buf, sizeof(buf), "%.1f", r);
    return buf;
}

bool rayBox(const dvec3& o, const dvec3& d, const dvec3& mn, const dvec3& mx) {
    double t0 = 0, t1 = 1e30;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-15) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
            continue;
        }
        double a = (mn[i] - o[i]) / d[i], b = (mx[i] - o[i]) / d[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    return true;
}

// Liegt vor p (in Richtung t) echtes Material dieses Stücks? Streifende Berührung zählt nicht.
bool pieceOccludes(const solid::Piece& pc, const dvec3& p, const dvec3& t) {
    double t0 = 0.0, t1 = 1e30;
    for (const solid::Face& f : pc.faces) {
        double denom = glm::dot(f.n, t), dist = glm::dot(f.n, p) - f.d;
        if (std::abs(denom) < 1e-12) {
            if (dist > -1e-7) return false;
            continue;
        }
        double tt = -dist / denom;
        if (denom < 0) t0 = std::max(t0, tt);
        else t1 = std::min(t1, tt);
        if (t1 - t0 < 1e-6) return false;
    }
    return t1 - t0 >= 1e-6;
}

struct MeshEdges {
    std::vector<std::pair<dvec3, dvec3>> edges;  // lokal
    std::vector<std::pair<dvec3, dvec3>> normals;  // Normalen der beiden Dreiecke (zweite = 0 bei Rand)
};

MeshEdges meshEdges(const ImportedMesh& m) {
    MeshEdges r;
    using Key = std::tuple<float, float, float, float, float, float>;
    std::map<Key, size_t> index;
    const auto& v = m.vertices;
    for (size_t i = 0; i + 2 < v.size(); i += 3) {
        dvec3 p[3] = {{v[i].px, v[i].py, v[i].pz}, {v[i + 1].px, v[i + 1].py, v[i + 1].pz}, {v[i + 2].px, v[i + 2].py, v[i + 2].pz}};
        dvec3 n = glm::cross(p[1] - p[0], p[2] - p[0]);
        if (glm::length(n) < 1e-18) continue;
        n = glm::normalize(n);
        for (int k = 0; k < 3; ++k) {
            dvec3 a = p[k], b = p[(k + 1) % 3];
            Key ka{(float)a.x, (float)a.y, (float)a.z, (float)b.x, (float)b.y, (float)b.z};
            Key kb{(float)b.x, (float)b.y, (float)b.z, (float)a.x, (float)a.y, (float)a.z};
            Key key = ka < kb ? ka : kb;
            auto it = index.find(key);
            if (it == index.end()) {
                index[key] = r.edges.size();
                r.edges.emplace_back(a, b);
                r.normals.emplace_back(n, dvec3(0));
            } else {
                r.normals[it->second].second = n;
            }
        }
    }
    return r;
}

struct Occluder {
    const Object* o;
    glm::dquat inv;
    std::vector<std::pair<dvec3, dvec3>> pieceBounds;
};

bool occluded(const std::vector<Occluder>& occ, const dvec3& p, const dvec3& t) {
    for (const Occluder& oc : occ) {
        const Object& o = *oc.o;
        dvec3 lp = oc.inv * (p - o.pos), lt = oc.inv * t;
        if (!rayBox(lp, lt, o.bbMin - 1e-6, o.bbMax + 1e-6)) continue;
        if (o.kind == ObjKind::Solid) {
            for (size_t i = 0; i < o.solid.pieces.size(); ++i) {
                if (!rayBox(lp, lt, oc.pieceBounds[i].first - 1e-6, oc.pieceBounds[i].second + 1e-6)) continue;
                if (pieceOccludes(o.solid.pieces[i], lp, lt)) return true;
            }
        } else if (o.imported) {
            const auto& v = o.imported->vertices;
            for (size_t i = 0; i + 2 < v.size(); i += 3) {
                dvec3 a(v[i].px, v[i].py, v[i].pz), b(v[i + 1].px, v[i + 1].py, v[i + 1].pz), c(v[i + 2].px, v[i + 2].py, v[i + 2].pz);
                dvec3 e1 = b - a, e2 = c - a, h = glm::cross(lt, e2);
                double det = glm::dot(e1, h);
                if (std::abs(det) < 1e-18) continue;
                double f = 1.0 / det;
                dvec3 s = lp - a;
                double u = glm::dot(s, h) * f;
                if (u < 0 || u > 1) continue;
                dvec3 q = glm::cross(s, e1);
                double w = glm::dot(lt, q) * f;
                if (w < 0 || u + w > 1) continue;
                if (glm::dot(e2, q) * f > 1e-5) return true;
            }
        }
    }
    return false;
}

void addSegment(DView& view, const ViewDef& vd, const std::vector<Occluder>& occ, const dvec3& a, const dvec3& b,
                double sampleLen) {
    dvec2 pa = proj(vd, a), pb = proj(vd, b);
    if (glm::length(pb - pa) < 1e-7) return;
    int n = std::clamp((int)std::ceil(glm::length(b - a) / sampleLen), 1, 200);
    int state = -1;
    double start = 0;
    auto emit = [&](double t0, double t1, int s) {
        DLine l{pa + (pb - pa) * t0, pa + (pb - pa) * t1, s ? LineStyle::Hidden : LineStyle::Visible};
        view.lines.push_back(l);
        (s ? view.hiddenCount : view.visibleCount)++;
    };
    for (int i = 0; i < n; ++i) {
        dvec3 p = a + (b - a) * ((i + 0.5) / n);
        int s = occluded(occ, p, vd.toViewer) ? 1 : 0;
        if (state < 0) {
            state = s;
        } else if (s != state) {
            emit(start, double(i) / n, state);
            state = s;
            start = double(i) / n;
        }
    }
    emit(start, 1.0, state);
}

// Zusammenhängende Teilstücke auf derselben Geraden zu einer Linie verbinden.
// Verdeckte Linien, die unter sichtbaren liegen, entfallen (sichtbare Kanten haben Vorrang).
void mergeLines(DView& view) {
    struct Seg {
        long long kx, ky, ko;
        dvec2 d;
        double off, t0, t1;
        LineStyle st;
    };
    std::vector<Seg> segs;
    std::vector<DLine> keep;
    for (const DLine& l : view.lines) {
        if (l.style != LineStyle::Visible && l.style != LineStyle::Hidden) {
            keep.push_back(l);
            continue;
        }
        dvec2 d = l.b - l.a;
        double len = glm::length(d);
        if (len < 1e-9) continue;
        d /= len;
        if (d.x < -1e-9 || (std::abs(d.x) <= 1e-9 && d.y < 0)) d = -d;
        double off = d.x * l.a.y - d.y * l.a.x;
        double t0 = glm::dot(l.a, d), t1 = glm::dot(l.b, d);
        if (t0 > t1) std::swap(t0, t1);
        segs.push_back({std::llround(d.x * 1e5), std::llround(d.y * 1e5), std::llround(off * 1e6), d, off, t0, t1, l.style});
    }
    std::sort(segs.begin(), segs.end(), [](const Seg& a, const Seg& b) {
        return std::tie(a.kx, a.ky, a.ko, a.t0) < std::tie(b.kx, b.ky, b.ko, b.t0);
    });
    view.visibleCount = view.hiddenCount = 0;
    auto emit = [&](const Seg& s, double t0, double t1, LineStyle st) {
        dvec2 n(-s.d.y, s.d.x);
        keep.push_back({s.d * t0 + n * s.off, s.d * t1 + n * s.off, st});
        (st == LineStyle::Visible ? view.visibleCount : view.hiddenCount)++;
    };
    for (size_t i = 0; i < segs.size();) {
        size_t j = i;
        while (j < segs.size() && segs[j].kx == segs[i].kx && segs[j].ky == segs[i].ky && segs[j].ko == segs[i].ko) ++j;
        // Intervalle je Linienart vereinigen
        std::vector<std::pair<double, double>> vis, hid;
        for (size_t k = i; k < j; ++k) {
            auto& list = segs[k].st == LineStyle::Visible ? vis : hid;
            if (!list.empty() && segs[k].t0 <= list.back().second + 1e-7) list.back().second = std::max(list.back().second, segs[k].t1);
            else list.emplace_back(segs[k].t0, segs[k].t1);
        }
        for (auto& v : vis) emit(segs[i], v.first, v.second, LineStyle::Visible);
        for (auto h : hid) {  // sichtbare Abschnitte herausschneiden
            double cur = h.first;
            for (auto& v : vis) {
                if (v.second <= cur || v.first >= h.second) continue;
                if (v.first > cur + 1e-7) emit(segs[i], cur, v.first, LineStyle::Hidden);
                cur = std::max(cur, v.second);
            }
            if (h.second > cur + 1e-7) emit(segs[i], cur, h.second, LineStyle::Hidden);
        }
        i = j;
    }
    view.lines.swap(keep);
}

struct AxisGroup {
    dvec3 q;     // Punkt auf der Achse (nächster zum Ursprung)
    dvec3 a;     // Richtung
    double r = 0;
    double tmin = 1e30, tmax = -1e30;
    std::vector<double> radii;
};

} // namespace

bool makeThreeView(World& w, const std::vector<uint32_t>& ids, TechDrawing& out, std::string* err) {
    out = TechDrawing{};
    std::vector<Object*> objs;
    for (uint32_t id : ids)
        if (Object* o = w.find(id)) objs.push_back(o);
    if (objs.empty()) {
        if (err) *err = "no parts selected";
        return false;
    }

    dvec3 mn(1e30), mx(-1e30);
    std::vector<Occluder> occ;
    std::map<std::string, int> mats, labels;
    for (Object* o : objs) {
        out.ids.push_back(o->id);
        mn = glm::min(mn, o->wMin);
        mx = glm::max(mx, o->wMax);
        out.mass += o->mass;
        mats[materials()[o->material].name]++;
        labels[materials()[o->material].label]++;
        out.title += (out.title.empty() ? "" : ", ") + o->name;
        Occluder oc{o, glm::conjugate(o->rot), {}};
        for (auto& p : o->solid.pieces) {
            dvec3 a, b;
            solid::bounds(p, a, b);
            oc.pieceBounds.emplace_back(a, b);
        }
        occ.push_back(std::move(oc));
    }
    for (auto& [name, n] : mats) out.material += (out.material.empty() ? "" : ", ") + name;
    for (auto& [name, n] : labels) out.sheetMaterial += (out.sheetMaterial.empty() ? "" : ", ") + name;
    out.sheetTitle = out.title;
    if (out.title.size() > 60) {
        out.title = std::to_string(objs.size()) + " parts (assembly)";
        out.sheetTitle = std::to_string(objs.size()) + " Teile (Baugruppe)";
    }
    out.size = mx - mn;
    const double sampleLen = std::max({out.size.x, out.size.y, out.size.z}) / 300.0 + 1e-6;

    // Kanten in Weltkoordinaten sammeln
    std::vector<std::pair<dvec3, dvec3>> features;
    std::vector<MeshEdges> meshData(objs.size());
    std::map<std::tuple<long long, long long, long long, long long, long long, long long>, AxisGroup> axes;
    for (size_t k = 0; k < objs.size(); ++k) {
        Object* o = objs[k];
        auto toW = [&](const dvec3& p) { return o->pos + o->rot * p; };
        if (o->kind == ObjKind::Solid) {
            for (size_t i = 0; i + 1 < o->edges.size(); i += 2) features.emplace_back(toW(dvec3(o->edges[i])), toW(dvec3(o->edges[i + 1])));
            for (auto& pc : o->solid.pieces)
                for (auto& f : pc.faces) {
                    if (f.internal || f.smooth != solid::Smooth::Cylinder) continue;
                    dvec3 a = glm::normalize(o->rot * f.sa);
                    for (int c = 0; c < 3; ++c)
                        if (std::abs(a[c]) > 1e-9) {
                            if (a[c] < 0) a = -a;
                            break;
                        }
                    dvec3 sc = toW(f.sc);
                    dvec3 q = sc - a * glm::dot(sc, a);
                    auto key = std::make_tuple(std::llround(a.x * 1000), std::llround(a.y * 1000), std::llround(a.z * 1000),
                                               std::llround(q.x * 1e4), std::llround(q.y * 1e4), std::llround(q.z * 1e4));
                    AxisGroup& g = axes[key];
                    g.q = q;
                    g.a = a;
                    g.r = std::max(g.r, f.sr);
                    if (std::find_if(g.radii.begin(), g.radii.end(), [&](double r) { return std::abs(r - f.sr) < 1e-6; }) == g.radii.end())
                        g.radii.push_back(f.sr);
                    for (auto& v : f.v) {
                        double s = glm::dot(toW(v) - q, a);
                        g.tmin = std::min(g.tmin, s);
                        g.tmax = std::max(g.tmax, s);
                    }
                }
        } else if (o->imported) {
            meshData[k] = meshEdges(*o->imported);
            const MeshEdges& me = meshData[k];
            for (size_t i = 0; i < me.edges.size(); ++i) {
                bool boundary = glm::length(me.normals[i].second) < 0.5;
                if (boundary || glm::dot(me.normals[i].first, me.normals[i].second) < std::cos(glm::radians(30.0)))
                    features.emplace_back(toW(me.edges[i].first), toW(me.edges[i].second));
            }
        }
    }

    for (int vi = 0; vi < 3; ++vi) {
        const ViewDef& vd = kViews[vi];
        DView& view = out.views[vi];
        view.name = vd.name;
        // Ausdehnung
        view.min = dvec2(1e30);
        view.max = dvec2(-1e30);
        for (int i = 0; i < 8; ++i) {
            dvec3 c((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
            view.min = glm::min(view.min, proj(vd, c));
            view.max = glm::max(view.max, proj(vd, c));
        }
        for (auto& e : features) addSegment(view, vd, occ, e.first, e.second, sampleLen);

        // Umrisse gekrümmter Flächen (Zylinder, Kugel) für diese Blickrichtung
        for (size_t k = 0; k < objs.size(); ++k) {
            Object* o = objs[k];
            auto toW = [&](const dvec3& p) { return o->pos + o->rot * p; };
            const dvec3 t = vd.toViewer;
            if (o->kind == ObjKind::Solid) {
                for (auto& pc : o->solid.pieces)
                    for (auto& f : pc.faces) {
                        if (f.internal || f.smooth == solid::Smooth::None) continue;
                        dvec3 sc = toW(f.sc), n;
                        if (f.smooth == solid::Smooth::Cylinder) {
                            dvec3 sa = glm::normalize(o->rot * f.sa);
                            dvec3 tp = t - sa * glm::dot(t, sa);
                            if (glm::length(tp) < 1e-6) continue;
                            n = glm::normalize(tp);
                        } else {
                            n = t;
                        }
                        double d = glm::dot(n, sc);
                        const std::vector<std::vector<dvec3>> single = {f.v};
                        for (const auto& poly : f.partial ? f.vis : single) {
                            std::vector<dvec3> hits;
                            for (size_t i = 0; i < poly.size(); ++i) {
                                dvec3 a = toW(poly[i]), b = toW(poly[(i + 1) % poly.size()]);
                                double sa = glm::dot(n, a) - d, sb = glm::dot(n, b) - d;
                                if ((sa < 0) != (sb < 0)) hits.push_back(a + (b - a) * (sa / (sa - sb)));
                            }
                            if (hits.size() == 2) addSegment(view, vd, occ, hits[0], hits[1], sampleLen);
                        }
                    }
            } else if (o->imported) {
                const MeshEdges& me = meshData[k];
                for (size_t i = 0; i < me.edges.size(); ++i) {
                    dvec3 n1 = o->rot * me.normals[i].first, n2 = o->rot * me.normals[i].second;
                    if (glm::length(n2) < 0.5) continue;
                    if ((glm::dot(n1, t) > 0) != (glm::dot(n2, t) > 0))
                        addSegment(view, vd, occ, toW(me.edges[i].first), toW(me.edges[i].second), sampleLen);
                }
            }
        }

        mergeLines(view);

        // Mittellinien und Durchmesser
        std::vector<std::pair<dvec2, double>> crosses;
        for (auto& [key, g] : axes) {
            double c = std::abs(glm::dot(g.a, vd.toViewer));
            double over = std::max(0.002, 0.15 * g.r);
            if (c > 0.999) {
                dvec2 p = proj(vd, g.q);
                bool dup = false;
                for (auto& cr : crosses)
                    if (glm::length(cr.first - p) < 1e-5) dup = true;
                if (!dup) {
                    crosses.emplace_back(p, g.r);
                    double L = g.r + over;
                    view.lines.push_back({p - dvec2(L, 0), p + dvec2(L, 0), LineStyle::Center});
                    view.lines.push_back({p - dvec2(0, L), p + dvec2(0, L), LineStyle::Center});
                }
                for (double r : g.radii) view.labels.push_back({p + dvec2(r * 0.72, r * 0.72), "Ø" + mm(2 * r)});
            } else if (c < 0.001) {
                dvec3 a = g.q + g.a * (g.tmin - over), b = g.q + g.a * (g.tmax + over);
                view.lines.push_back({proj(vd, a), proj(vd, b), LineStyle::Center});
            }
        }
    }

    // Lage auf dem Blatt (y nach oben): Draufsicht unten, Vorderansicht darüber, Seitenansicht rechts
    const double W = out.size.x, H = out.size.y, D = out.size.z;
    const double gap = std::max({W, H, D}) * 0.35 + 0.01;
    DView& front = out.views[0];
    DView& top = out.views[1];
    DView& side = out.views[2];
    top.offset = -top.min;
    front.offset = dvec2(-front.min.x, -front.min.y + D + gap);
    side.offset = dvec2(-side.min.x + W + gap, -side.min.y + D + gap);
    out.sheetMin = dvec2(0, 0);
    out.sheetMax = dvec2(W + gap + D, D + gap + H);

    // Bemaßung (Gesamtmaße)
    front.dims.push_back({dvec2(front.min.x, front.max.y), dvec2(front.max.x, front.max.y), dvec2(0, 1), 8, mm(W)});
    front.dims.push_back({dvec2(front.min.x, front.min.y), dvec2(front.min.x, front.max.y), dvec2(-1, 0), 8, mm(H)});
    top.dims.push_back({dvec2(top.min.x, top.min.y), dvec2(top.min.x, top.max.y), dvec2(-1, 0), 8, mm(D)});
    return true;
}

// ============================================================================ Blatt

Sheet layoutSheet(const TechDrawing& d) {
    Sheet s;
    const double areaX0 = 25, areaY0 = 22, areaW = 297 - 25 - 22, areaH = 210 - 22 - 40;
    dvec2 ext = d.sheetMax - d.sheetMin;
    const double ratios[] = {10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001};
    const char* names[] = {"10:1", "5:1", "2:1", "1:1", "1:2", "1:5", "1:10", "1:20", "1:50", "1:100", "1:200", "1:500", "1:1000"};
    int pick = 12;
    for (int i = 0; i < 13; ++i)
        if (ext.x * 1000 * ratios[i] <= areaW && ext.y * 1000 * ratios[i] <= areaH) {
            pick = i;
            break;
        }
    s.scale = ratios[pick];
    s.scaleText = names[pick];
    const double k = 1000 * s.scale;  // Papier-mm pro Modell-m
    const dvec2 org(areaX0 + (areaW - ext.x * k) / 2, areaY0 + (areaH - ext.y * k) / 2);
    auto P = [&](const DView& v, const dvec2& p) {
        dvec2 sp = p + v.offset - d.sheetMin;
        return dvec2(org.x + sp.x * k, org.y + (ext.y - sp.y) * k);
    };

    // Rahmen
    auto rect = [&](double x0, double y0, double x1, double y1, LineStyle st) {
        s.lines.push_back({{x0, y0}, {x1, y0}, st});
        s.lines.push_back({{x1, y0}, {x1, y1}, st});
        s.lines.push_back({{x1, y1}, {x0, y1}, st});
        s.lines.push_back({{x0, y1}, {x0, y0}, st});
    };
    rect(20, 10, 287, 200, LineStyle::Frame);

    for (int vi = 0; vi < 3; ++vi) {
        const DView& v = d.views[vi];
        // erst verdeckte, dann Mittel-, dann sichtbare Linien (sichtbare liegen oben)
        for (LineStyle st : {LineStyle::Hidden, LineStyle::Center, LineStyle::Visible})
            for (const DLine& l : v.lines)
                if (l.style == st) s.lines.push_back({P(v, l.a), P(v, l.b), st});
        for (const DLabel& lb : v.labels) {
            dvec2 p = P(v, lb.p);
            s.lines.push_back({P(v, lb.p - (lb.p - lb.p)), p + dvec2(6, -6), LineStyle::Thin});
            s.texts.push_back({p + dvec2(11, -7.5), lb.text, 3.0, false});
        }
        for (const DDim& dm : v.dims) {
            dvec2 a = P(v, dm.a), b = P(v, dm.b);
            if (std::abs(dm.normal.y) > 0.5) {  // waagerechte Maßlinie
                double sgn = dm.normal.y > 0 ? -1.0 : 1.0;  // Papier-y zeigt nach unten
                double y = (sgn < 0 ? std::min(a.y, b.y) : std::max(a.y, b.y)) + sgn * dm.offset;
                s.lines.push_back({{a.x, a.y + sgn * 1.0}, {a.x, y + sgn * 2.0}, LineStyle::Thin});
                s.lines.push_back({{b.x, b.y + sgn * 1.0}, {b.x, y + sgn * 2.0}, LineStyle::Thin});
                double x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
                s.lines.push_back({{x0, y}, {x1, y}, LineStyle::Thin});
                s.tris.push_back({{x0, y}, {x0 + 2.5, y - 0.6}, {x0 + 2.5, y + 0.6}});
                s.tris.push_back({{x1, y}, {x1 - 2.5, y - 0.6}, {x1 - 2.5, y + 0.6}});
                s.texts.push_back({{(x0 + x1) / 2, y - 2.2}, dm.text, 3.5, false});
            } else {  // senkrechte Maßlinie
                double sgn = dm.normal.x > 0 ? 1.0 : -1.0;
                double x = (sgn < 0 ? std::min(a.x, b.x) : std::max(a.x, b.x)) + sgn * dm.offset;
                s.lines.push_back({{a.x + sgn * 1.0, a.y}, {x + sgn * 2.0, a.y}, LineStyle::Thin});
                s.lines.push_back({{b.x + sgn * 1.0, b.y}, {x + sgn * 2.0, b.y}, LineStyle::Thin});
                double y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
                s.lines.push_back({{x, y0}, {x, y1}, LineStyle::Thin});
                s.tris.push_back({{x, y0}, {x - 0.6, y0 + 2.5}, {x + 0.6, y0 + 2.5}});
                s.tris.push_back({{x, y1}, {x - 0.6, y1 - 2.5}, {x + 0.6, y1 - 2.5}});
                s.texts.push_back({{x - 2.2, (y0 + y1) / 2}, dm.text, 3.5, true});
            }
        }
    }

    // Schriftfeld
    const double tx0 = 287 - 125, ty0 = 200 - 30;
    rect(tx0, ty0, 287, 200, LineStyle::Frame);
    s.lines.push_back({{tx0, ty0 + 10}, {287, ty0 + 10}, LineStyle::Thin});
    s.lines.push_back({{tx0, ty0 + 20}, {287, ty0 + 20}, LineStyle::Thin});
    s.lines.push_back({{tx0 + 62, ty0 + 10}, {tx0 + 62, 200}, LineStyle::Thin});
    const std::string& sheetTitle = d.sheetTitle.empty() ? d.title : d.sheetTitle;
    std::string title = sheetTitle.size() > 45 ? sheetTitle.substr(0, 42) + "..." : sheetTitle;
    s.texts.push_back({{tx0 + 62.5, ty0 + 5}, title, 4.0, false});
    s.texts.push_back({{tx0 + 31, ty0 + 15}, "Werkstoff: " + (d.sheetMaterial.empty() ? d.material : d.sheetMaterial).substr(0, 22), 2.8, false});
    s.texts.push_back({{tx0 + 93.5, ty0 + 15}, "Masse: " + fmtNum(d.mass, 3) + " kg", 2.8, false});
    s.texts.push_back({{tx0 + 31, ty0 + 25}, "Maßstab " + s.scaleText + "  (Maße in mm)", 2.8, false});
    s.texts.push_back({{tx0 + 93.5, ty0 + 25}, "Projektionsmethode 1", 2.8, false});
    // Symbol Projektionsmethode 1: Kegelstumpf (schmales Ende links), rechts daneben die Ansicht von links
    {
        double cx = tx0 - 18, cy = ty0 + 15;
        s.lines.push_back({{cx - 14, cy - 2}, {cx - 6, cy - 4}, LineStyle::Thin});
        s.lines.push_back({{cx - 6, cy - 4}, {cx - 6, cy + 4}, LineStyle::Thin});
        s.lines.push_back({{cx - 6, cy + 4}, {cx - 14, cy + 2}, LineStyle::Thin});
        s.lines.push_back({{cx - 14, cy + 2}, {cx - 14, cy - 2}, LineStyle::Thin});
        s.lines.push_back({{cx - 16, cy}, {cx - 4, cy}, LineStyle::Center});
        const int N = 24;
        for (int i = 0; i < N; ++i) {
            double a0 = 2 * 3.14159265 * i / N, a1 = 2 * 3.14159265 * (i + 1) / N;
            s.lines.push_back({{cx + 5 + 4 * std::cos(a0), cy + 4 * std::sin(a0)}, {cx + 5 + 4 * std::cos(a1), cy + 4 * std::sin(a1)}, LineStyle::Thin});
            s.lines.push_back({{cx + 5 + 2 * std::cos(a0), cy + 2 * std::sin(a0)}, {cx + 5 + 2 * std::cos(a1), cy + 2 * std::sin(a1)}, LineStyle::Thin});
        }
    }
    return s;
}

static std::string xmlEscape(const std::string& t) {
    std::string r;
    for (char c : t) {
        if (c == '&') r += "&amp;";
        else if (c == '<') r += "&lt;";
        else if (c == '>') r += "&gt;";
        else if (c == '"') r += "&quot;";
        else r += c;
    }
    return r;
}

std::string sheetToSvg(const Sheet& s) {
    std::ostringstream o;
    o.precision(4);
    o << std::fixed;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << s.width << "mm\" height=\"" << s.height
      << "mm\" viewBox=\"0 0 " << s.width << " " << s.height << "\">\n";
    o << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    o << "<g fill=\"none\" stroke=\"black\" stroke-linecap=\"round\">\n";
    for (const SheetLine& l : s.lines) {
        const char* style = "";
        double w = 0.25;
        switch (l.style) {
        case LineStyle::Visible: w = 0.5; break;
        case LineStyle::Hidden: w = 0.25; style = " stroke-dasharray=\"2.5,1.2\" stroke-linecap=\"butt\""; break;
        case LineStyle::Center: w = 0.25; style = " stroke-dasharray=\"8,1.2,1,1.2\" stroke-linecap=\"butt\""; break;
        case LineStyle::Thin: w = 0.25; break;
        case LineStyle::Frame: w = 0.7; break;
        }
        o << "<line x1=\"" << l.a.x << "\" y1=\"" << l.a.y << "\" x2=\"" << l.b.x << "\" y2=\"" << l.b.y
          << "\" stroke-width=\"" << w << "\"" << style << "/>\n";
    }
    o << "</g>\n<g fill=\"black\" stroke=\"none\">\n";
    for (const SheetTri& t : s.tris)
        o << "<polygon points=\"" << t.a.x << "," << t.a.y << " " << t.b.x << "," << t.b.y << " " << t.c.x << "," << t.c.y << "\"/>\n";
    o << "</g>\n<g font-family=\"Arial, Helvetica, sans-serif\" fill=\"black\" text-anchor=\"middle\" dominant-baseline=\"middle\">\n";
    for (const SheetText& t : s.texts) {
        o << "<text x=\"" << t.p.x << "\" y=\"" << t.p.y << "\" font-size=\"" << t.size << "\"";
        if (t.vertical) o << " transform=\"rotate(-90 " << t.p.x << " " << t.p.y << ")\"";
        o << ">" << xmlEscape(t.text) << "</text>\n";
    }
    o << "</g>\n</svg>\n";
    return o.str();
}

std::string drawingSummary(const TechDrawing& d, const Sheet& s) {
    std::ostringstream o;
    o << "three-view drawing: " << d.title << "\n";
    o << "scale " << s.scaleText << " on A4 landscape, first-angle projection\n";
    o << "overall size W x H x D: " << mm(d.size.x) << " x " << mm(d.size.y) << " x " << mm(d.size.z) << " mm\n";
    o << "material: " << d.material << ", mass " << fmtNum(d.mass, 4) << " kg\n";
    for (const DView& v : d.views) {
        int center = 0;
        for (auto& l : v.lines) center += l.style == LineStyle::Center;
        const std::string name = v.name == "Vorderansicht" ? "front view" : v.name == "Draufsicht" ? "top view" : "left side view";
        o << name << ": " << v.visibleCount << " visible, " << v.hiddenCount << " hidden line segments, " << center << " center lines";
        if (!v.labels.empty()) {
            o << ", holes/cylinders:";
            for (auto& lb : v.labels) o << " " << lb.text;
        }
        o << "\n";
    }
    return o.str();
}
