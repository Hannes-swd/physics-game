// Tag und Nacht (Sky.cpp): Sonne, Mond und Sterne aus der Uhrzeit der Welt.
// Die Welt liegt auf 50° nördlicher Breite (wie Mitteleuropa), das Datum steht fest auf Ende April: die Sonne geht um
// etwa 5 Uhr auf und um 19 Uhr unter, der Mond (fast voll) etwa eine Stunde später. Norden ist −z, Osten +x.
// Die Sterne sind echte Sterne (Großer Wagen, Orion, Kassiopeia ...) an ihren richtigen Orten; der Himmel dreht sich
// um den Polarstern. Dazu kommen schwache Sterne und die Milchstraße aus dem Shader (sky.frag).
#pragma once
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace sky {

constexpr double kLatitude = 50.0;     // Grad Nord
constexpr double kDayLength = 1200.0;  // ein Tag dauert 20 Minuten (Voreinstellung)
constexpr int kMaxStars = 192;         // Platz im Shader (common.glsl)
constexpr int kMaxLines = 192;         // Sternbildlinien

struct State {
    glm::vec3 sunDir{0, 1, 0};   // Richtung zur Sonne (auch unter dem Horizont)
    glm::vec3 moonDir{0, -1, 0};
    double sunAlt = 0, moonAlt = 0;  // Höhe über dem Horizont (Grad)
    glm::vec3 lightDir{0, 1, 0};     // wer gerade Licht und Schatten gibt: Sonne oder Mond
    glm::vec3 lightColor{0};         // Farbe · Stärke des direkten Lichts
    bool moonLight = false;          // lightDir ist der Mond
    glm::vec3 skyAmbient{0}, groundAmbient{0};  // Umgebungslicht von oben / unten
    glm::vec3 zenith{0}, horizon{0};             // Himmel oben / am Horizont (auch Nebelfarbe)
    glm::vec3 glow{0};               // Abend- und Morgenrot um die Sonne
    glm::vec3 sunDisc{0};            // Farbe der Sonnenscheibe (0: unter dem Horizont)
    float moonBright = 0;            // Mondscheibe 0..1
    float stars = 0;                 // Sterne sichtbar 0..1
    float exposure = 1;              // Belichtung (das Auge gewöhnt sich an die Nacht)
    glm::mat4 starRot{1.0f};         // Himmelsäquator -> Ort (dreht sich mit der Sternzeit)
};

// Himmel um diese Uhrzeit (Stunden 0..24, 12 = Mittag)
State at(double hours);

// Sternkatalog: xyz Richtung im Äquatorsystem (x zum Frühlingspunkt, z zum Himmelsnordpol),
// w = Größenklasse + 20 · Farbe (0 blauweiß, 1 weiß, 2 gelblich, 3 orange, 4 rot)
const std::vector<glm::vec4>& stars();
// Sternbildlinien: Paare von Indizes in stars()
const std::vector<glm::ivec2>& lines();

// "06:30"
std::string clock(double hours);
// "6:30", "6", "18.5", "noon" ... -> Stunden; false wenn unlesbar
bool parseTime(const std::string& s, double& hours);

}  // namespace sky
