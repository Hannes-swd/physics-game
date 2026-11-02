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
    vec4 water[64];    // Gewässer, je 8 Werte (WaterGpu in Renderer.h): lo + Wellenlänge, hi + Wellenhöhe, Phasen,
                       // Absorption + Meer, Streufarbe + Rastergröße, Strömungsgitter (2), Grund (Meer)
    vec4 waterInfo;    // x = Anzahl, yz = Richtung der Wellen
    // Tag und Nacht (Sky.h): Sonne, Mond, Himmel, Sterne
    vec4 sunPos;       // xyz Richtung zur Sonne (auch unter dem Horizont; sunDir ist nachts der Mond)
    vec4 sunDisc;      // rgb Farbe der Sonnenscheibe (0 unter dem Horizont)
    vec4 moonPos;      // xyz Richtung zum Mond, w = Helligkeit der Scheibe
    vec4 zenith;       // rgb Himmel oben
    vec4 horizon;      // rgb Himmel am Horizont, auch die Farbe des Nebels
    vec4 glow;         // rgb Abend-/Morgenrot um die Sonne, w = Sterne sichtbar 0..1
    vec4 skyInfo;      // x = Anzahl Sterne, y = Anzahl Sternbildlinien
    mat4 starRot;      // Himmelsäquator -> Ort (dreht sich mit der Sternzeit)
    vec4 stars[192];   // xyz Richtung (Äquatorsystem), w = Größenklasse + 20 · Farbe (sky::kMaxStars)
    ivec4 starLines[96];  // je zwei Linien: Indizes in stars (sky::kMaxLines / 2)
} frame;
// Hinweis: Alle Positionen in den Shadern sind relativ zum Darstellungsursprung (nahe der Kamera),
// damit die Genauigkeit auch Kilometer vom Weltursprung entfernt erhalten bleibt.

// Technische Ansichten (orthografisch) bleiben immer im Tageslicht
bool orthoView() { return pc.camPos.w > 0.5; }
const vec3 kDaySun = vec3(0.4626, 0.8738, 0.3084);  // feste Sonne der technischen Ansichten

// Farbe des Nebels: der Himmel am Horizont (nachts dunkel)
vec3 fogColor() { return frame.horizon.rgb; }

vec3 tonemap(vec3 x) {
    x *= orthoView() ? 1.0 : frame.misc.y;  // Belichtung: nachts gewöhnt sich das Auge an die Dunkelheit
    vec3 c = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, vec3(1.0 / 2.2));
}

// Mondscheibe (etwas größer als echt, damit man sie gut sieht): Kugel, die von der Sonne beleuchtet wird (daraus die
// Phase), mit dunkleren Meeren
const float kMoonRadius = 0.0105;  // rad
vec3 moonDisc(vec3 dir) {
    vec3 M = frame.moonPos.xyz;
    float c = dot(dir, M);
    if (c < 0.9995 || frame.moonPos.w <= 0.0) return vec3(0.0);
    vec3 right = normalize(cross(M, abs(M.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0)));
    vec3 up = cross(right, M);
    vec2 p = vec2(dot(dir, right), dot(dir, up)) / kMoonRadius;
    float r2 = dot(p, p);
    if (r2 > 1.1) return vec3(0.0);
    vec3 n = p.x * right + p.y * up - sqrt(max(1.0 - r2, 0.0)) * M;  // Oberfläche, zum Betrachter gewandt
    float lit = smoothstep(-0.04, 0.06, dot(n, frame.sunPos.xyz));
    // Meere: ein paar weiche, dunkle Flecken (fest auf der Scheibe)
    float mare = 0.0;
    mare += 0.55 * smoothstep(0.42, 0.05, length(p - vec2(-0.22, 0.30)));
    mare += 0.45 * smoothstep(0.30, 0.02, length(p - vec2(0.25, 0.18)));
    mare += 0.40 * smoothstep(0.34, 0.03, length(p - vec2(0.05, -0.28)));
    mare += 0.30 * smoothstep(0.22, 0.02, length(p - vec2(-0.45, -0.05)));
    float albedo = 1.0 - 0.38 * clamp(mare, 0.0, 1.0);
    float edge = 1.0 - smoothstep(0.88, 1.0, r2);
    float limb = 0.82 + 0.18 * sqrt(max(1.0 - r2, 0.0));
    return vec3(0.95, 0.93, 0.88) * 0.55 * lit * albedo * limb * edge * frame.moonPos.w;
}

// Himmel in einer Richtung (auch für Spiegelungen auf dem Wasser): Verlauf nach der Tageszeit, Abendrot, Sonne, Mond.
// Die Sterne zeichnet nur sky.frag
vec3 skyColor(vec3 dir) {
    if (orthoView()) {  // technische Ansicht: Tageshimmel
        vec3 h = vec3(0.62, 0.72, 0.86);
        return dir.y > 0.0 ? mix(h, vec3(0.16, 0.34, 0.72), pow(dir.y, 0.55)) : mix(h, vec3(0.45, 0.47, 0.50), pow(-dir.y, 0.35));
    }
    vec3 hor = frame.horizon.rgb;
    vec3 col = dir.y > 0.0 ? mix(hor, frame.zenith.rgb, pow(dir.y, 0.55)) : mix(hor, hor * vec3(0.72, 0.65, 0.58), pow(-dir.y, 0.35));
    vec3 S = frame.sunPos.xyz;
    float s = max(dot(dir, S), 0.0);
    // Abend- und Morgenrot: um die Sonne, dicht am Horizont
    float low = 1.0 - smoothstep(0.0, 0.45, abs(dir.y));
    col += frame.glow.rgb * (pow(s, 5.0) * 0.9 + s * 0.15) * low;
    col += frame.sunDisc.rgb * (pow(s, 900.0) * 30.0 + pow(s, 12.0) * 0.25);
    // Mond: Scheibe und ein schwacher Hof
    float m = max(dot(dir, frame.moonPos.xyz), 0.0);
    col += moonDisc(dir) * smoothstep(-0.01, 0.005, dir.y);
    col += vec3(0.55, 0.62, 0.78) * frame.moonPos.w * (pow(m, 600.0) * 0.06 + pow(m, 24.0) * 0.012);
    return col;
}

// ------------------------------------------------------------------ Wasser
// Wellen: dieselbe Summe aus vier Sinuswellen wie waveHeight in Water.cpp (Physik und Bild passen zusammen).
// Die Phasen kommen von der CPU (doppelte Genauigkeit, enthalten Zeit und Ursprung).
const vec4 kWaveScale = vec4(1.0, 0.61, 0.37, 0.23);   // Wellenlänge relativ zur größten
const vec4 kWaveAmp = vec4(0.55, 0.25, 0.13, 0.07);    // Anteil an der halben Wellenhöhe
const vec4 kWaveAngle = vec4(0.0, 0.65, -0.5, 1.25);   // Richtung gegen die Hauptrichtung (rad)

int waterCount() { return int(frame.waterInfo.x + 0.5); }
bool waterIsSea(int w) { return frame.water[w * 8 + 3].w > 0.5; }

// Strömungsgitter aller Gewässer (FlowGrid in Water.h): je Zelle Strömung x, z (m/s), Schaum 0..1, Stau (m)
layout(std430, set = 0, binding = 4) readonly buffer WaterCells { vec4 waterCells[]; };

// Strömung, Schaum und Stau über p (xz relativ zum Ursprung), bilinear. Ohne Gitter: gleichmäßige Strömung
vec4 waterFlow(int w, vec2 p) {
    vec4 g0 = frame.water[w * 8 + 5], g1 = frame.water[w * 8 + 6];
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
    float H = frame.water[w * 8 + 1].w;
    if (H <= 0.0) return 0.0;
    float lambda0 = frame.water[w * 8].w;
    vec4 ph = frame.water[w * 8 + 2];
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

// Grund unter p (xz relativ zum Ursprung): Becken der Boden, Meer die Grundkarte (Fenster um die Kamera, 1 m je Zelle,
// bilinear wie GroundMap::at), außerhalb des Fensters der Standardgrund. Wie kWaterGroundCells in Renderer.h
const int kGroundCells = 128;
float groundCell(int start, ivec2 c) {
    int i = c.x + kGroundCells * c.y;
    return waterCells[start + i / 4][i % 4];
}
float waterBottom(int w, vec2 p) {
    float base = frame.water[w * 8].y;
    vec4 gr = frame.water[w * 8 + 7];
    if (!waterIsSea(w) || gr.w < 0.0) return base;
    vec2 f = p - gr.xy - 0.5;
    if (any(lessThan(f, vec2(-0.5))) || any(greaterThan(f, vec2(float(kGroundCells) - 0.5)))) return base;
    f = clamp(f, vec2(0.0), vec2(float(kGroundCells - 1) - 0.0001));
    ivec2 a = ivec2(floor(f));
    vec2 t = f - vec2(a);
    int s = int(gr.w + 0.5);
    float h = mix(mix(groundCell(s, a), groundCell(s, a + ivec2(1, 0)), t.x), mix(groundCell(s, a + ivec2(0, 1)), groundCell(s, a + ivec2(1, 1)), t.x), t.y);
    return h + gr.z;
}

// Wasserkörper als Quader (Meer: seitlich unbegrenzt, unten der Grund unter waveAt), Oberfläche auf der Höhe von Wellen
// und Stau über waveAt
void waterBox(int w, vec2 waveAt, out vec3 lo, out vec3 hi) {
    lo = frame.water[w * 8].xyz;
    hi = frame.water[w * 8 + 1].xyz;
    if (waterIsSea(w)) {
        lo = vec3(-1e7, waterBottom(w, waveAt), -1e7);
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
        if (lo.y >= hi.y) continue;  // Grund über dem Spiegel (Land, Ufer): hier kein Wasser
        // Strecke im Quader (Slab-Methode)
        vec3 inv = 1.0 / mix(d, vec3(1e-9), lessThan(abs(d), vec3(1e-9)));
        vec3 t0 = (lo - a) * inv, t1 = (hi - a) * inv;
        vec3 tn = min(t0, t1), tf = max(t0, t1);
        float t_in = max(max(tn.x, tn.y), max(tn.z, 0.0));
        float t_out = min(min(tf.x, tf.y), min(tf.z, 1.0));
        if (t_out <= t_in) continue;
        float L = (t_out - t_in) * len;
        vec3 T = exp(-frame.water[w * 8 + 3].rgb * L);
        color = color * T + frame.water[w * 8 + 4].rgb * waterLight() * (1.0 - T);
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
        vec3 sigma = frame.water[w * 8 + 3].rgb;
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
