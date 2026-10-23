#version 450
#include "common.glsl"

// Wasseroberfläche: Spiegelung des Himmels (Fresnel), Sonnenglanz, feine Kräuselung, Schaum auf den Wellenkämmen.
// Was darunter liegt, haben die Körper schon selbst eingefärbt (waterView in mesh.frag): die Oberfläche deckt nur so viel
// ab, wie sie spiegelt. Ausgabe vormultipliziert: rgb = Spiegelung · Deckung + Glanz, a = Deckung.
layout(location = 0) in vec3 vPos;
layout(location = 1) flat in int vWater;
layout(location = 2) flat in vec3 vSide;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}

// feine Kräuselung (auch auf ruhigem Wasser): Steigung aus zwei Lagen Rauschen, die mit der Zeit wandern
float rippleField(vec2 p, float t) { return noise(p * 3.0 + vec2(t * 0.30, t * 0.21)) + 0.5 * noise(p * 7.3 + vec2(-t * 0.45, t * 0.37)); }
vec2 ripples(vec2 p, float t) {
    float e = 0.01;
    float a = rippleField(p, t);
    return vec2(rippleField(p + vec2(e, 0), t) - a, rippleField(p + vec2(0, e), t) - a) / e * 0.012;
}

// Mit der Strömung treibende Kräuselung und Schaum-Muster (Flow-Map): das Muster wird in zwei um eine halbe Periode
// versetzten Phasen entlang der Strömung verschoben und überblendet; so wandert es, ohne sich endlos zu verzerren.
// Schnelles Wasser ist rauer, der Schaum zieht sich in Strömungsrichtung zu Streifen.
vec2 flowRipples(vec2 p, vec2 u, float t, out float foamTex) {
    const float period = 1.6;
    float ph0 = fract(t / period), ph1 = fract(t / period + 0.5);
    float w0 = 1.0 - abs(1.0 - 2.0 * ph0), w1 = 1.0 - w0;  // Dreieck: Gewicht 0, wenn die Phase zurückspringt
    vec2 p0 = p - u * ph0 * period, p1 = p - u * ph1 * period + vec2(0.37, 0.71);
    float speed = length(u);
    vec2 g = (ripples(p0, t * 0.15) * w0 + ripples(p1, t * 0.15) * w1) * (1.0 + 2.0 * min(speed, 2.5));
    vec2 along = speed > 1e-3 ? u / speed : vec2(1.0, 0.0), across = vec2(-along.y, along.x);
    vec2 q0 = vec2(dot(p0, along) * 2.0, dot(p0, across) * 9.0), q1 = vec2(dot(p1, along) * 2.0, dot(p1, across) * 9.0);
    foamTex = (noise(q0) * 0.65 + noise(q0 * 2.7) * 0.35) * w0 + (noise(q1 + 5.3) * 0.65 + noise(q1 * 2.7 + 1.7) * 0.35) * w1;
    return g;
}

float shadowAt(vec3 wp) {
    if (frame.misc.w < 0.5) return 1.0;
    vec4 lp = frame.lightViewProj * vec4(wp + vec3(0, frame.misc.x * 1.5, 0), 1.0);
    vec3 c = lp.xyz / lp.w;
    vec2 uv = c.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0 || c.z > 1.0) return 1.0;
    float t = 1.0 / float(textureSize(shadowMap, 0).x);
    float s = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) s += texture(shadowMap, vec3(uv + vec2(x, y) * t * 1.2, c.z - 0.0003));
    return s / 9.0;
}

void main() {
    int w = vWater;
    vec3 V = normalize(pc.camPos.xyz - vPos);
    float dist = length(pc.camPos.xyz - vPos);
    vec3 L = normalize(frame.sunDir.xyz);
    vec3 N;
    float crest = 0.0, streamFoam = 0.0;
    vec4 flow = waterFlow(w, vPos.xz);  // Strömung x, z, Schaum, Stau
    if (dot(vSide, vSide) > 0.5) {
        if (dot(V, vSide) <= 0.0) discard;  // Seitenwand von innen: man schaut durch die Oberfläche
        N = vSide;
    } else {
        vec2 g;
        float h = waterWave(w, vPos.xz, g);
        float H = frame.water[w * 7 + 1].w;
        // Wellen, die kleiner als ein Pixel werden (zum Horizont hin), glätten: sonst flimmert es. In der Ferne ist das
        // Wasser ein Spiegel des Himmels
        float footprint = length(fwidth(vPos.xz)), lambda0 = frame.water[w * 7].w;
        float resolved = 1.0 - smoothstep(0.08 * lambda0, 0.5 * lambda0, footprint);
        g *= resolved;
        crest = H > 0.0 ? h / (0.5 * H) * resolved : 0.0;
        // Stau vor Hindernissen: Wasser wölbt sich auf (Steigung über eine halbe Zelle)
        vec4 g0 = frame.water[w * 7 + 5];
        if (g0.w >= 0.0) {
            float e = 0.5 * g0.z;
            g += vec2(waterFlow(w, vPos.xz + vec2(e, 0)).w - waterFlow(w, vPos.xz - vec2(e, 0)).w,
                      waterFlow(w, vPos.xz + vec2(0, e)).w - waterFlow(w, vPos.xz - vec2(0, e)).w) / (2.0 * e);
        }
        // Kräuselung und Schaum treiben mit der Strömung
        float foamTex;
        g += flowRipples(vPos.xz + frame.originMod.xz, flow.xy, pc.params.y, foamTex) * (1.0 - smoothstep(0.02, 0.1, footprint));
        streamFoam = smoothstep(0.05, 0.4, flow.z * (0.3 + 1.2 * foamTex)) * (1.0 - smoothstep(0.3, 1.5, footprint));
        N = normalize(vec3(-g.x, 1.0, -g.y));
    }

    // Unter Wasser ist die Kamera, wenn sie im Wasserkörper liegt (nicht nach der Normale: auf die Rückseite einer fernen
    // Welle schaut man auch von oben)
    bool under = pc.camPos.w < 0.5 && insideWater(pc.camPos.xyz);
    vec3 col;            // deckende Farbe (nicht vormultipliziert)
    float alpha;         // Deckung
    vec3 glow = vec3(0); // leuchtet zusätzlich (Sonnenglanz)
    if (!under) {
        // von oben: Fresnel (Wasser, n = 1,33), Spiegelung des Himmels, Glanz der Sonne
        float NoV = max(dot(N, V), 1e-3);
        alpha = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);
        vec3 R = reflect(-V, N);
        R.y = abs(R.y);  // Wellen spiegeln sonst "unter den Horizont"
        col = skyColor(R);
        vec3 Hv = normalize(L + V);
        float NoH = max(dot(N, Hv), 0.0), NoL = max(dot(N, L), 0.0);
        float a2 = 0.0004;  // sehr glatt (GGX, Rauheit 0,14)
        float dd = NoH * NoH * (a2 - 1.0) + 1.0;
        float D = a2 / (3.14159 * dd * dd);
        float Fs = 0.02 + 0.98 * pow(1.0 - max(dot(V, Hv), 0.0), 5.0);
        glow = frame.sunColor.rgb * min(D * Fs / (4.0 * NoV), 40.0) * step(0.0, NoL) * shadowAt(vPos);
    } else {
        // von unten: im Snellius-Fenster (bis 48,6° vom Lot) sieht man hinaus, außerhalb spiegelt die Oberfläche das Wasser
        float NoV = max(-dot(N, V), 1e-3);
        float tir = 1.0 - smoothstep(0.62, 0.70, NoV);
        alpha = 0.1 + 0.9 * tir;
        col = frame.water[w * 7 + 4].rgb * waterLight();
        glow = (1.0 - tir) * waterLight() * 0.15;
    }
    // Schaum auf den Wellenkämmen und dort, wo Wasser gegen Hindernisse läuft (treibt mit der Strömung davon)
    float foam = smoothstep(0.6, 1.0, crest) * smoothstep(0.35, 0.8, noise((vPos.xz + frame.originMod.xz) * 4.0 + pc.params.y * 0.3));
    foam = max(foam, streamFoam);
    col = mix(col, waterLight() * 0.55, foam);
    alpha = mix(alpha, 1.0, foam * 0.9);
    // Nebel wie bei den Körpern (nur in der Perspektive)
    if (pc.camPos.w < 0.5) {
        float fog = 1.0 - exp(-dist * frame.misc.z);
        col = mix(col, kHorizon, fog);
        alpha = mix(alpha, 1.0, fog);
        glow *= 1.0 - fog;
    }
    outColor = vec4(tonemap(col) * alpha + tonemap(glow), alpha);
}
