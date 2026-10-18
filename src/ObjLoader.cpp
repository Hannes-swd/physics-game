#include "ObjLoader.h"

#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {

int resolveIndex(int idx, size_t count) {
    if (idx > 0) return idx - 1;
    if (idx < 0) return (int)count + idx;
    return -1;
}

} // namespace

bool loadObj(const std::filesystem::path& path, ObjModel& out) {
    std::ifstream in(path);
    if (!in) {
        out.error = "Datei konnte nicht geöffnet werden";
        return false;
    }
    out = ObjModel{};
    out.name = path.stem().u8string();

    std::vector<glm::vec3> pos, nrm;
    std::vector<std::unordered_set<int>> groupIdx(1);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            glm::vec3 p;
            ss >> p.x >> p.y >> p.z;
            pos.push_back(p);
        } else if (tag == "vn") {
            glm::vec3 n;
            ss >> n.x >> n.y >> n.z;
            nrm.push_back(n);
        } else if (tag == "o" || tag == "g") {
            if (!groupIdx.back().empty()) groupIdx.emplace_back();
        } else if (tag == "f") {
            std::vector<int> vi, ni;
            std::string tok;
            while (ss >> tok) {
                int v = 0, t = 0, n = 0;
                size_t s1 = tok.find('/');
                v = std::stoi(tok.substr(0, s1));
                if (s1 != std::string::npos) {
                    size_t s2 = tok.find('/', s1 + 1);
                    std::string ts = tok.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
                    if (!ts.empty()) t = std::stoi(ts);
                    if (s2 != std::string::npos && s2 + 1 < tok.size()) n = std::stoi(tok.substr(s2 + 1));
                }
                (void)t;
                vi.push_back(resolveIndex(v, pos.size()));
                ni.push_back(resolveIndex(n, nrm.size()));
            }
            if (vi.size() < 3) continue;
            bool ok = true;
            for (int v : vi) ok = ok && v >= 0 && v < (int)pos.size();
            if (!ok) continue;
            for (int v : vi) groupIdx.back().insert(v);
            glm::vec3 fn = glm::cross(pos[vi[1]] - pos[vi[0]], pos[vi[2]] - pos[vi[0]]);
            if (glm::length(fn) > 1e-12f) fn = glm::normalize(fn);
            for (size_t i = 1; i + 1 < vi.size(); ++i) {
                size_t ids[3] = {0, i, i + 1};
                for (size_t k : ids) {
                    glm::vec3 p = pos[vi[k]];
                    glm::vec3 n = (ni[k] >= 0 && ni[k] < (int)nrm.size()) ? nrm[ni[k]] : fn;
                    out.vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z});
                }
            }
        }
    }
    for (auto& g : groupIdx) {
        if (g.size() < 4) continue;
        std::vector<glm::vec3> pts;
        for (int i : g) pts.push_back(pos[i]);
        out.groups.push_back(std::move(pts));
    }
    if (out.vertices.empty()) {
        out.error = "Keine Flächen in der Datei gefunden";
        return false;
    }
    return true;
}
