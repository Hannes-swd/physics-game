// Animierte Spielfigur aus einer glTF-Binärdatei (.glb): Skelett, Farbmaterialien (keine Texturen), Bewegungsabläufe.
// Die Haut wird jedes Bild auf der CPU verformt (wenige tausend Punkte), Abläufe werden weich überblendet.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "Solid.h"

class AvatarModel {
public:
    // Ein Stück der Figur mit einer Farbe (zusammenhängender Bereich in der Ausgabe von pose())
    struct Part {
        std::string material;
        glm::vec3 color{1.0f};
        uint32_t first = 0, count = 0;
    };
    // ein Ablauf mit Gewicht; time in Sekunden (wird auf die Länge des Ablaufs umgebrochen)
    struct Layer {
        int clip = -1;
        float time = 0.0f;
        float weight = 0.0f;
    };
    // zusätzliche Drehung eines Knochens um sein Gelenk (nach den Abläufen, Kinder drehen mit).
    // Achse im Raum der Figur: x rechts, y oben, -z vorn. Beispiel: Oberschenkel um +x = Knie nach vorn.
    struct Bend {
        int node = -1;
        glm::vec3 axis{1, 0, 0};
        float angle = 0.0f;
    };

    // height: so groß wird die Figur (m, Sohle bis Scheitel)
    bool load(const uint8_t* data, size_t size, float height, std::string* error = nullptr);
    bool loadBuiltin(float height, std::string* error = nullptr);  // eingebaute Figur (assets/characters/worker.glb)
    bool valid() const { return !parts_.empty(); }

    int clip(const std::string& name) const;  // Name ohne Vorsilbe ("Walk" findet "CharacterArmature|Walk"), -1 = fehlt
    float clipLength(int clip) const;
    // Geschwindigkeit (m/s), zu der die Schritte eines Laufablaufs passen (aus der Fußbewegung geschätzt)
    float clipGroundSpeed(int clip) const { return clip >= 0 && clip < (int)clips_.size() ? clips_[clip].groundSpeed : 0.0f; }
    // Anteil der Länge (0..1), an dem der erste Fuß am höchsten schwingt. Damit lassen sich Laufabläufe im Gleichschritt mischen.
    float clipSync(int clip) const { return clip >= 0 && clip < (int)clips_.size() ? clips_[clip].sync : 0.0f; }

    int node(const std::string& name) const;   // Knochen nach Namen, -1 = fehlt
    glm::vec3 restPosition(int node) const;      // Gelenk in Ruhelage, Raum der Figur (m)
    // Knochen, die nicht in der Kette hängen (z.B. Fuß-Ziele für IK unter der Wurzel), bewegen sich mit einem anderen mit
    void setFollow(int follower, int leader);

    // Figur in Lage setzen. Ausgabe: Dreiecke in Metern, Füße bei y = 0, Blick nach -z, Höhe wie bei load() gewählt.
    void pose(const Layer* layers, int count, const Bend* bends, int bendCount, std::vector<solid::Vertex>& out) const;
    const std::vector<Part>& parts() const { return parts_; }
    float height() const { return height_; }

private:
    struct Node {
        int parent = -1;
        glm::vec3 t{0.0f}, s{1.0f};
        glm::quat r{1, 0, 0, 0};
    };
    struct Channel {
        int node = -1;
        int path = 0;  // 0 = Verschiebung, 1 = Drehung, 2 = Größe
        bool step = false;
        std::vector<float> times;
        std::vector<glm::vec4> values;
    };
    struct Clip {
        std::string name;
        float length = 0.0f, groundSpeed = 0.0f, sync = 0.0f;
        std::vector<Channel> channels;
    };
    struct SkinVertex {
        glm::vec3 p, n;
        uint16_t joint[4];
        float weight[4];
    };

    void evaluate(const Layer* layers, int count, const Bend* bends, int bendCount, std::vector<glm::mat4>& global) const;

    std::vector<Node> nodes_;
    std::vector<std::string> names_;
    std::vector<int> order_;  // Eltern vor Kindern
    std::vector<int> joints_;
    std::vector<glm::mat4> inverseBind_;
    std::vector<SkinVertex> vertices_;
    std::vector<uint32_t> triangles_;  // Punktnummern, nach Teilen sortiert
    std::vector<Part> parts_;
    std::vector<Clip> clips_;
    glm::mat4 fit_{1.0f};  // Ausrichtung: Füße auf 0, Blick nach -z, gewünschte Höhe
    glm::mat3 unfit_{1.0f};  // Richtungen aus dem Raum der Figur zurück ins Modell
    float height_ = 1.8f;
    int foot_ = -1;
    std::vector<std::pair<int, int>> follows_;  // (Folger, Führer)
    std::vector<glm::mat4> rest_;               // Ruhelage (Modellraum)
};
