#version 450
#include "common.glsl"

layout(location = 0) in vec4 vNear;
layout(location = 1) in vec4 vFar;
layout(location = 2) in vec2 vUv;
layout(location = 0) out vec4 outColor;

// ------------------------------------------------------------------ Nachthimmel
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float hash12(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float vnoise3(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x), mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x), mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm3(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) {
        s += a * vnoise3(p);
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return s;
}

// Farbe eines Sterns (0 blauweiß ... 4 rot), dezent
vec3 starTint(int c) {
    if (c == 0) return vec3(0.80, 0.88, 1.00);
    if (c == 1) return vec3(0.95, 0.97, 1.00);
    if (c == 2) return vec3(1.00, 0.96, 0.86);
    if (c == 3) return vec3(1.00, 0.84, 0.64);
    return vec3(1.00, 0.70, 0.52);
}

// Stern als scharfer Punkt: Größe in Pixeln (px = Winkel eines Pixels), Helligkeit aus der Größenklasse
vec3 starPoint(float ang, float px, float mag, vec3 tint) {
    float size = px * (0.9 + 0.22 * clamp(2.5 - mag, 0.0, 4.0));
    float x = ang / size;
    if (x > 4.0) return vec3(0.0);
    return tint * exp(-x * x) * 2.4 * pow(10.0, -0.4 * mag) / max(size / px, 1.0);
}

// Richtung auf der Würfelfläche -> Richtung (für die schwachen Sterne)
vec3 cubeDir(int face, vec2 uv) {
    if (face == 0) return normalize(vec3(1.0, uv.y, -uv.x));
    if (face == 1) return normalize(vec3(-1.0, uv.y, uv.x));
    if (face == 2) return normalize(vec3(uv.x, 1.0, -uv.y));
    if (face == 3) return normalize(vec3(uv.x, -1.0, uv.y));
    if (face == 4) return normalize(vec3(uv.x, uv.y, 1.0));
    return normalize(vec3(-uv.x, uv.y, -1.0));
}

// Galaktischer Nordpol und Zentrum im Äquatorsystem (für die Milchstraße)
const vec3 kGalPole = vec3(-0.8676, -0.1981, 0.4560);
const vec3 kGalCenter = vec3(-0.0549, -0.8734, -0.4838);

// e: Richtung im Äquatorsystem, px: Winkel eines Pixels
vec3 nightSky(vec3 e, float px, float altitude) {
    vec3 col = vec3(0.0);
    // Milchstraße: ein Band entlang der galaktischen Ebene, zum Zentrum (Schütze) heller, mit Staubwolken
    float b = dot(e, kGalPole);
    float band = exp(-b * b / 0.018);
    float center = pow(max(dot(e, kGalCenter), 0.0), 3.0);
    float clouds = fbm3(e * 9.0);
    float dust = smoothstep(0.35, 0.75, fbm3(e * 22.0 + 5.0)) * exp(-b * b / 0.002);
    float milky = band * (0.45 + 0.9 * clouds) * (1.0 + 1.6 * center) * (1.0 - 0.6 * dust);
    col += vec3(0.80, 0.84, 1.00) * milky * 0.0065;

    // Schwache Sterne: auf einem Würfel um den Betrachter, je Zelle höchstens einer (in der Milchstraße dichter)
    vec3 a = abs(e);
    int face;
    vec2 uv;
    if (a.x >= a.y && a.x >= a.z) { face = e.x > 0.0 ? 0 : 1; uv = vec2(e.x > 0.0 ? -e.z : e.z, e.y) / a.x; }
    else if (a.y >= a.z) { face = e.y > 0.0 ? 2 : 3; uv = vec2(e.x, e.y > 0.0 ? -e.z : e.z) / a.y; }
    else { face = e.z > 0.0 ? 4 : 5; uv = vec2(e.z > 0.0 ? e.x : -e.x, e.y) / a.z; }
    const float N = 150.0;
    vec2 cell = floor((uv * 0.5 + 0.5) * N);
    vec2 seed = cell + vec2(float(face) * 311.0, float(face) * 173.0);
    float h = hash12(seed);
    if (h < 0.06 * (1.0 + 2.0 * band)) {
        vec2 at = cell + 0.25 + 0.5 * vec2(hash12(seed + 7.3), hash12(seed + 19.1));
        vec3 sd = cubeDir(face, at / N * 2.0 - 1.0);
        float mag = max(6.6 + log(max(hash12(seed + 3.7), 1e-4)) / 1.0986, 3.6);  // jede Größenklasse 3× mehr Sterne
        int tint = int(hash12(seed + 11.0) * 4.99);
        float ang = length(e - sd);
        col += starPoint(ang, px, mag, starTint(tint));
    }

    // Die hellen Sterne der Sternbilder (echte Orte)
    int n = int(frame.skyInfo.x + 0.5);
    for (int i = 0; i < n; ++i) {
        vec4 s = frame.stars[i];
        float c = dot(e, s.xyz);
        if (c < 0.9999) continue;  // weiter als 0,8° weg
        int tint = int((s.w + 5.0) / 20.0);
        float mag = s.w - 20.0 * float(tint);
        float ang = length(e - s.xyz);
        // ganz leichtes Funkeln, nur tief am Horizont (dort ist mehr Luft)
        float tw = 1.0 + (1.0 - smoothstep(0.05, 0.35, altitude)) * 0.35 * sin(pc.params.y * (7.0 + float(i % 5)) + float(i) * 2.4);
        col += starPoint(ang, px, mag, starTint(tint)) * tw;
    }

    // Sternbildlinien (nur wenn eingeschaltet): dünn und leise, mit Abstand zu den Sternen
    int nl = int(frame.skyInfo.y + 0.5);
    float line = 0.0;
    for (int i = 0; i < nl; ++i) {
        ivec4 q = frame.starLines[i / 2];
        ivec2 ab = (i % 2 == 0) ? q.xy : q.zw;
        vec3 A = frame.stars[ab.x].xyz, B = frame.stars[ab.y].xyz;
        vec3 nrm = normalize(cross(A, B));
        float d = abs(dot(e, nrm));
        if (d > px * 3.0) continue;
        if (dot(cross(A, e), nrm) < 0.0 || dot(cross(e, B), nrm) < 0.0) continue;  // außerhalb des Bogens
        float gap = smoothstep(0.006, 0.010, min(length(e - A), length(e - B)));
        line = max(line, (1.0 - smoothstep(0.4 * px, 1.4 * px, d)) * gap);
    }
    col += vec3(0.45, 0.60, 0.95) * line * 0.035;

    // Licht auf dem Weg durch die Luft: zum Horizont schwächer
    float ext = exp(-0.22 * (1.0 / (max(altitude, 0.0) + 0.06) - 1.0));
    return col * ext;
}

void main() {
    if (orthoView()) {
        // Technische Ansicht: ruhiger Verlauf wie in CAD-Programmen
        vec3 top = vec3(0.23, 0.25, 0.29), bottom = vec3(0.12, 0.13, 0.15);
        outColor = vec4(mix(top, bottom, vUv.y), 1.0);
        return;
    }
    vec3 dir = normalize(vFar.xyz / vFar.w - vNear.xyz / vNear.w);
    float px = length(fwidth(dir));  // Winkel eines Pixels (vor jeder Verzweigung)
    vec3 col = skyColor(dir);
    if (frame.glow.w > 0.001 && dir.y > -0.02) {
        // Sterne und Milchstraße drehen sich mit dem Himmel (Äquatorsystem -> Ort: starRot, zurück: transponiert)
        vec3 e = transpose(mat3(frame.starRot)) * dir;
        col += nightSky(e, max(px, 1e-5), dir.y) * frame.glow.w;
    }
    if (waterCount() > 0) {
        if (insideWater(pc.camPos.xyz)) {
            // unter Wasser: der Strahl läuft durch das Wasser bis zur Oberfläche (oder ins Unendliche)
            col = waterView(col, pc.camPos.xyz, pc.camPos.xyz + dir * 1e5, pc.camPos.xz);
        } else if (dir.y < 0.0) {
            // Meer hinter dem Ende der Oberfläche (über 25 km): so flach angeschaut spiegelt es nur den Himmel
            for (int w = 0; w < waterCount(); ++w)
                if (waterIsSea(w) && pc.camPos.y > frame.water[w * 8 + 1].y) col = skyColor(vec3(dir.x, -dir.y, dir.z));
        }
    }
    // leichtes Rauschen gegen Farbstufen im Verlauf
    float dither = (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * 1.5 / 255.0;
    outColor = vec4(tonemap(col) + dither, 1.0);
}
