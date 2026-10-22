// Gemeinsame Daten aller Shader

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 camPos;   // xyz Kamera, w = 1 bei orthografischer Ansicht
    vec4 params;   // x = Raster an, y = Zeit, z = Bau-Modus, w = Linien-Tiefenversatz
} pc;

struct ObjectData {
    mat4 model;
    vec4 color;    // rgb Grundfarbe, a = Rauheit
    vec4 color2;   // rgb zweite Farbe, a = metallisch
    vec4 params;   // x = Shading-Art, y = Zufallswert, z = Hervorhebung, w = Anlauffarben (0 keine, 1 Stahl, 2 Kupfer/Messing)
    vec4 heat0;    // xyz = Ecke des Temperaturgitters (lokal), w = erste Zelle in heatCells (< 0: kein Gitter)
    vec4 heat1;    // xyz = Zellgröße, w = Zellen je Achse (x + 64·y + 4096·z)
};

layout(std430, set = 0, binding = 0) readonly buffer Objects { ObjectData objs[]; };

layout(set = 0, binding = 2) uniform Frame {
    mat4 lightViewProj;
    vec4 sunDir;       // xyz Richtung zur Sonne
    vec4 sunColor;     // rgb
    vec4 skyColor;     // rgb Umgebungslicht von oben
    vec4 groundColor;  // rgb Umgebungslicht von unten
    vec4 misc;         // x = Schatten-Texel in m, y = Belichtung, z = Nebel, w = Schatten an
    vec4 originMod;    // Weltursprung der Darstellung modulo 200 m (für Muster)
    vec4 originF;      // Weltursprung der Darstellung (float, für Achsenlinien)
    vec4 lightPos[8];  // Feuerschein: Position (relativ zum Ursprung)
    vec4 lightColor[8];  // Farbe · Stärke
    vec4 lightInfo;    // x = Anzahl
} frame;
// Hinweis: Alle Positionen in den Shadern sind relativ zum Darstellungsursprung (nahe der Kamera),
// damit die Genauigkeit auch Kilometer vom Weltursprung entfernt erhalten bleibt.

const vec3 kHorizon = vec3(0.62, 0.72, 0.86);

vec3 tonemap(vec3 x) {
    x *= frame.misc.y;
    vec3 c = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, vec3(1.0 / 2.2));
}
