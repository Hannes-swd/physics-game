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
    vec4 water[56];    // Gewässer, je 7 Werte (WaterGpu in Renderer.h): lo + Wellenlänge, hi + Wellenhöhe, Phasen,
                       // Absorption + Meer, Streufarbe + Rastergröße, Strömungsgitter (2)
    vec4 waterInfo;    // x = Anzahl, yz = Richtung der Wellen
} frame;
// Hinweis: Alle Positionen in den Shadern sind relativ zum Darstellungsursprung (nahe der Kamera),
// damit die Genauigkeit auch Kilometer vom Weltursprung entfernt erhalten bleibt.

const vec3 kHorizon = vec3(0.62, 0.72, 0.86);

vec3 tonemap(vec3 x) {
    x *= frame.misc.y;
    vec3 c = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, vec3(1.0 / 2.2));
}

// Himmel in einer Richtung (auch für Spiegelungen auf dem Wasser)
vec3 skyColor(vec3 dir) {
    vec3 zenith = vec3(0.16, 0.34, 0.72);
    vec3 haze = vec3(0.45, 0.47, 0.50);
    vec3 col = dir.y > 0.0 ? mix(kHorizon, zenith, pow(dir.y, 0.55)) : mix(kHorizon, haze, pow(-dir.y, 0.35));
    float s = max(dot(dir, normalize(frame.sunDir.xyz)), 0.0);
    return col + vec3(1.0, 0.9, 0.75) * (pow(s, 900.0) * 30.0 + pow(s, 12.0) * 0.25);
}

// ------------------------------------------------------------------ Wasser
// Wellen: dieselbe Summe aus vier Sinuswellen wie waveHeight in Water.cpp (Physik und Bild passen zusammen).
// Die Phasen kommen von der CPU (doppelte Genauigkeit, enthalten Zeit und Ursprung).
const vec4 kWaveScale = vec4(1.0, 0.61, 0.37, 0.23);   // Wellenlänge relativ zur größten
const vec4 kWaveAmp = vec4(0.55, 0.25, 0.13, 0.07);    // Anteil an der halben Wellenhöhe
const vec4 kWaveAngle = vec4(0.0, 0.65, -0.5, 1.25);   // Richtung gegen die Hauptrichtung (rad)

int waterCount() { return int(frame.waterInfo.x + 0.5); }
bool waterIsSea(int w) { return frame.water[w * 7 + 3].w > 0.5; }

// Strömungsgitter aller Gewässer (FlowGrid in Water.h): je Zelle Strömung x, z (m/s), Schaum 0..1, Stau (m)
layout(std430, set = 0, binding = 4) readonly buffer WaterCells { vec4 waterCells[]; };

// Strömung, Schaum und Stau über p (xz relativ zum Ursprung), bilinear. Ohne Gitter: gleichmäßige Strömung
vec4 waterFlow(int w, vec2 p) {
    vec4 g0 = frame.water[w * 7 + 5], g1 = frame.water[w * 7 + 6];
    if (g0.w < 0.0) return vec4(g1.zw, 0.0, 0.0);
    ivec2 dim = ivec2(g1.xy + 0.5);
    vec2 f = (p - g0.xy) / g0.z - 0.5;
    if (f.x < -0.5 || f.y < -0.5 || f.x > float(dim.x) - 0.5 || f.y > float(dim.y) - 0.5) return vec4(g1.zw, 0.0, 0.0);
    f = clamp(f, vec2(0.0), vec2(dim - 1) - 0.0001);
    ivec2 a = ivec2(floor(f));
    vec2 t = f - vec2(a);
    int base = int(g0.w + 0.5) + a.x + dim.x * a.y;
    vec4 c00 = waterCells[base], c10 = waterCells[base + 1], c01 = waterCells[base + dim.x], c11 = waterCells[base + dim.x + 1];
    return mix(mix(c00, c10, t.x), mix(c01, c11, t.x), t.y);
}

// Auslenkung der Oberfläche über p (xz relativ zum Ursprung), grad = Steigung
float waterWave(int w, vec2 p, out vec2 grad) {
    grad = vec2(0.0);
    float H = frame.water[w * 7 + 1].w;
    if (H <= 0.0) return 0.0;
    float lambda0 = frame.water[w * 7].w;
    vec4 ph = frame.water[w * 7 + 2];
    vec2 d0 = frame.waterInfo.yz;
    float h = 0.0;
    for (int i = 0; i < 4; ++i) {
        float c = cos(kWaveAngle[i]), s = sin(kWaveAngle[i]);
        vec2 d = vec2(d0.x * c - d0.y * s, d0.x * s + d0.y * c);
        float k = 6.2831853 / (lambda0 * kWaveScale[i]);
        float a = 0.5 * H * kWaveAmp[i];
        float th = k * dot(d, p) + ph[i];
        h += a * sin(th);
        grad += a * k * cos(th) * d;
    }
    return h;
}

// Oberfläche über p: Wellen und Stau an Hindernissen (wie World::surfaceHeight)
float waterSurface(int w, vec2 p, out vec2 grad) { return waterWave(w, p, grad) + waterFlow(w, p).w; }

// Wasserkörper als Quader (Meer: unten und seitlich unbegrenzt), Oberfläche auf der Höhe von Wellen und Stau über waveAt
void waterBox(int w, vec2 waveAt, out vec3 lo, out vec3 hi) {
    lo = frame.water[w * 7].xyz;
    hi = frame.water[w * 7 + 1].xyz;
    if (waterIsSea(w)) {
        lo = vec3(-1e7);
        hi.xz = vec2(1e7);
    }
    vec2 g;
    hi.y += waterSurface(w, waveAt, g);
}

// Liegt der Punkt im Wasser? (Wellen über dem Punkt)
bool insideWater(vec3 p) {
    for (int w = 0; w < waterCount(); ++w) {
        vec3 lo, hi;
        waterBox(w, p.xz, lo, hi);
        if (all(greaterThan(p, lo)) && all(lessThan(p, hi))) return true;
    }
    return false;
}

// Licht, das im Wasser gestreut wird (Himmel und Sonne von oben)
vec3 waterLight() {
    return frame.skyColor.rgb * 1.6 + frame.sunColor.rgb * 0.3 * max(frame.sunDir.y, 0.0);
}

// Farbe auf dem Weg a -> b durch das Wasser: Absorption (rot zuerst) und Streulicht. waveAt: dort zählen die Wellen
vec3 waterView(vec3 color, vec3 a, vec3 b, vec2 waveAt) {
    int n = waterCount();
    vec3 d = b - a;
    float len = length(d);
    for (int w = 0; w < n; ++w) {
        vec3 lo, hi;
        waterBox(w, waveAt, lo, hi);
        // Strecke im Quader (Slab-Methode)
        vec3 inv = 1.0 / mix(d, vec3(1e-9), lessThan(abs(d), vec3(1e-9)));
        vec3 t0 = (lo - a) * inv, t1 = (hi - a) * inv;
        vec3 tn = min(t0, t1), tf = max(t0, t1);
        float t_in = max(max(tn.x, tn.y), max(tn.z, 0.0));
        float t_out = min(min(tf.x, tf.y), min(tf.z, 1.0));
        if (t_out <= t_in) continue;
        float L = (t_out - t_in) * len;
        vec3 T = exp(-frame.water[w * 7 + 3].rgb * L);
        color = color * T + frame.water[w * 7 + 4].rgb * waterLight() * (1.0 - T);
    }
    return color;
}

// Sonne unter Wasser: rgb = Faktor für das direkte Sonnenlicht (Absorption auf dem Weg nach unten, Lichtflecken der
// Wellen am Grund), a = Faktor für das Umgebungslicht. p relativ zum Ursprung
vec4 waterSun(vec3 p, float time) {
    int n = waterCount();
    vec4 r = vec4(1.0);
    for (int w = 0; w < n; ++w) {
        vec3 lo, hi;
        waterBox(w, p.xz, lo, hi);
        if (any(lessThan(p, lo)) || any(greaterThan(p, hi))) continue;
        float depth = hi.y - p.y;
        vec3 sigma = frame.water[w * 7 + 3].rgb;
        vec3 L = normalize(frame.sunDir.xyz);
        r.rgb *= exp(-sigma * depth / max(L.y, 0.2));
        r.a *= exp(-dot(sigma, vec3(0.333)) * depth * 0.6);
        // Lichtflecken (Kaustik): die Wellen bündeln das Licht. Prozedural, wandert mit der Zeit. Das Muster liegt dort, wo
        // der Sonnenstrahl durch die Oberfläche kam (sonst würde es an senkrechten Wänden zu Streifen)
        vec2 q = (p.xz + L.xz * (depth / max(L.y, 0.2)) + frame.originMod.xz) * 2.2;
        float c = 0.0;
        for (int i = 0; i < 3; ++i) {
            q = mat2(1.6, 1.2, -1.2, 1.6) * q + vec2(time * 0.35, -time * 0.27);
            c += abs(sin(q.x + 1.7 * sin(q.y * 0.8 + time * 0.6)) * sin(q.y + 1.3 * sin(q.x * 0.7 - time * 0.5)));
        }
        c = pow(1.0 - c / 3.0, 5.0);
        float far = length(p - pc.camPos.xyz);                      // in der Ferne ausblenden (kein Flimmern)
        float k = smoothstep(0.02, 0.15, depth) * exp(-depth * 0.25) * (1.0 - smoothstep(12.0, 35.0, far));
        r.rgb *= 1.0 + k * (min(c, 0.6) * 4.0 - 0.45);
    }
    return r;
}
