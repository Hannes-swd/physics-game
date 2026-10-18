#version 450
#include "common.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vLocalPos;
layout(location = 3) flat in uint vObj;

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

// ------------------------------------------------------------------ Rauschen
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float vnoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash13(i), b = hash13(i + vec3(1, 0, 0));
    float c = hash13(i + vec3(0, 1, 0)), d = hash13(i + vec3(1, 1, 0));
    float e = hash13(i + vec3(0, 0, 1)), g = hash13(i + vec3(1, 0, 1));
    float h = hash13(i + vec3(0, 1, 1)), k = hash13(i + vec3(1, 1, 1));
    return mix(mix(mix(a, b, f.x), mix(c, d, f.x), f.y), mix(mix(e, g, f.x), mix(h, k, f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) {
        s += a * vnoise(p);
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return s;
}

// ------------------------------------------------------------------ Materialien (prozedural, im Objektraum)
// Blendet Muster aus, die feiner als ein Pixel wären (sonst flimmert es in der Ferne)
float aaFade(vec3 p, float freq) {
    float fw = length(fwidth(p)) * freq;
    return 1.0 - smoothstep(0.3, 0.9, fw);
}

vec3 wood(vec3 p, vec3 c1, vec3 c2, float seed, inout float rough) {
    // Faser entlang der lokalen X-Achse, Stammmitte liegt außerhalb des Bretts
    vec2 center = vec2(-0.10 - 0.15 * fract(seed * 3.7), 0.18 + 0.2 * fract(seed * 5.1));
    float r = length(p.yz - center);
    r += 0.006 * fbm(p * vec3(3.0, 25.0, 25.0) + seed * 13.0) + 0.002 * sin(p.x * 7.0 + seed * 10.0);
    float rings = fract(r * 170.0);
    float late = smoothstep(0.5, 0.9, rings) * (1.0 - smoothstep(0.9, 1.0, rings));
    late = mix(0.3, late, aaFade(p, 170.0));
    float fiber = mix(0.5, vnoise(p * vec3(8.0, 260.0, 260.0) + seed), aaFade(p, 260.0));
    vec3 col = mix(c1, c2, late * 0.8);
    col *= 0.86 + 0.2 * fiber;
    col *= 0.9 + 0.2 * fbm(p * vec3(1.2, 9.0, 9.0) + seed * 3.0);
    rough = mix(rough, rough + 0.15, late);
    return col;
}

vec3 brick(vec3 p, vec3 c1, vec3 c2, float seed) {
    float n = fbm(p * 35.0 + seed * 7.0);
    vec3 col = mix(c2, c1, smoothstep(0.25, 0.75, n));
    float speck = smoothstep(0.80, 0.86, vnoise(p * 380.0)) * aaFade(p, 380.0);
    float light = smoothstep(0.85, 0.9, vnoise(p * 300.0 + 5.0)) * aaFade(p, 300.0);
    col = mix(col, col * 0.55, speck);
    col = mix(col, vec3(0.75, 0.62, 0.5), light * 0.4);
    return col * (0.9 + 0.2 * vnoise(p * 6.0));
}

vec3 concrete(vec3 p, vec3 c1, vec3 c2) {
    vec3 col = mix(c2, c1, fbm(p * 12.0));
    float pores = smoothstep(0.83, 0.87, vnoise(p * 260.0)) * aaFade(p, 260.0);
    return mix(col, col * 0.45, pores) * (0.93 + 0.12 * vnoise(p * 90.0));
}

vec3 stone(vec3 p, vec3 c1, vec3 c2, inout float rough) {
    float dark = mix(0.22, smoothstep(0.62, 0.68, vnoise(p * 320.0)), aaFade(p, 320.0));
    float white = mix(0.06, smoothstep(0.78, 0.82, vnoise(p * 260.0 + 9.0)), aaFade(p, 260.0));
    float pink = mix(0.08, smoothstep(0.75, 0.8, vnoise(p * 140.0 + 3.0)), aaFade(p, 140.0));
    vec3 col = c1 * (0.9 + 0.2 * fbm(p * 20.0));
    col = mix(col, vec3(0.62, 0.45, 0.42), pink * 0.6);
    col = mix(col, c2, dark);
    col = mix(col, vec3(0.85), white * 0.7);
    rough = mix(rough, 0.25, dark);
    return col;
}

vec3 metal(vec3 p, vec3 c1, vec3 c2, inout float rough) {
    float brushed = mix(0.5, vnoise(p * vec3(12.0, 900.0, 900.0)), aaFade(p, 900.0));
    float blotch = fbm(p * 10.0);
    rough = clamp(rough + 0.12 * (brushed - 0.5) + 0.1 * (blotch - 0.5), 0.08, 1.0);
    return mix(c2, c1, 0.6 + 0.4 * brushed) * (0.95 + 0.1 * blotch);
}

// Boden der Werkstatt mit Raster. rel = Position relativ zum Darstellungsursprung
vec3 ground(vec3 rel, inout float rough) {
    vec3 wp = rel + frame.originMod.xyz;   // für Muster (wiederholen sich ohnehin)
    vec3 wabs = rel + frame.originF.xyz;    // für die Achsen
    vec3 col = mix(vec3(0.085, 0.085, 0.08), vec3(0.15, 0.145, 0.135), fbm(wp * 0.6));
    col *= 0.92 + 0.12 * mix(0.5, vnoise(wp * 40.0), aaFade(wp, 40.0));
    // Fugen alle 2 m
    vec2 t = abs(fract(wp.xz / 2.0 + 0.5) - 0.5) * 2.0;
    float joint = 1.0 - smoothstep(0.0, 0.006, min(1.0 - t.x, 1.0 - t.y) * 2.0);
    col *= 1.0 - 0.35 * joint;
    if (pc.params.x > 0.5) {
        vec2 c1 = wp.xz / 0.1;
        vec2 c2 = wp.xz;
        vec2 w1 = fwidth(c1), w2 = fwidth(c2);
        vec2 g1 = abs(fract(c1 - 0.5) - 0.5) / max(w1, 1e-4);
        vec2 g2 = abs(fract(c2 - 0.5) - 0.5) / max(w2, 1e-4);
        float minor = (1.0 - min(min(g1.x, g1.y), 1.0)) * (1.0 - smoothstep(0.04, 0.2, max(w1.x, w1.y)));
        float major = (1.0 - min(min(g2.x, g2.y) / 1.5, 1.0)) * (1.0 - smoothstep(0.08, 0.35, max(w2.x, w2.y)));
        col = mix(col, vec3(0.30, 0.32, 0.36), minor * 0.4);
        col = mix(col, vec3(0.42, 0.45, 0.50), major * 0.7);
        // Achsen
        float ax = 1.0 - min(abs(wabs.z) / max(fwidth(wabs.z) * 1.5, 1e-5), 1.0);
        float az = 1.0 - min(abs(wabs.x) / max(fwidth(wabs.x) * 1.5, 1e-5), 1.0);
        col = mix(col, vec3(0.9, 0.2, 0.15), ax);
        col = mix(col, vec3(0.2, 0.4, 0.95), az);
    }
    rough = 0.85;
    return col;
}

// ------------------------------------------------------------------ Licht
float shadowFactor(vec3 wp, vec3 N) {
    if (frame.misc.w < 0.5) return 1.0;
    vec4 lp = frame.lightViewProj * vec4(wp + N * frame.misc.x * 1.5, 1.0);
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
    ObjectData o = objs[vObj];
    int shading = int(o.params.x + 0.5);
    float seed = o.params.y;
    float rough = o.color.a;
    float metallic = o.color2.a;
    vec3 p = vLocalPos;

    vec3 albedo;
    if (shading == 0) albedo = wood(p, o.color.rgb, o.color2.rgb, seed, rough);
    else if (shading == 1) albedo = brick(p, o.color.rgb, o.color2.rgb, seed);
    else if (shading == 2) albedo = metal(p, o.color.rgb, o.color2.rgb, rough);
    else if (shading == 3) albedo = stone(p, o.color.rgb, o.color2.rgb, rough);
    else if (shading == 4) albedo = o.color.rgb * (0.85 + 0.3 * fbm(p * 60.0));
    else if (shading == 5) albedo = concrete(p, o.color.rgb, o.color2.rgb);
    else if (shading == 10) albedo = ground(vWorldPos, rough);
    else albedo = o.color.rgb;

    vec3 N = normalize(vNormal);
    bool ortho = pc.camPos.w > 0.5;
    vec3 V = normalize(pc.camPos.xyz - vWorldPos);
    vec3 L = normalize(frame.sunDir.xyz);
    vec3 H = normalize(L + V);
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 1e-3);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    float a = max(rough * rough, 0.002);
    float a2 = a * a;
    float dd = NoH * NoH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * dd * dd);
    float k = a * 0.5;
    float Vis = 0.25 / ((NoV * (1.0 - k) + k) * (NoL * (1.0 - k) + k));
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
    vec3 kd = (1.0 - F) * (1.0 - metallic);

    float sh = shadowFactor(vWorldPos, N);
    vec3 direct = (kd * albedo / PI + D * Vis * F) * frame.sunColor.rgb * NoL * sh;

    vec3 amb = mix(frame.groundColor.rgb, frame.skyColor.rgb, N.y * 0.5 + 0.5);
    vec3 R = reflect(-V, N);
    vec3 env = mix(frame.groundColor.rgb * 1.2, frame.skyColor.rgb * 1.6, smoothstep(-0.3, 0.5, R.y));
    vec3 Fa = f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - NoV, 5.0);
    float ao = 0.75 + 0.25 * (N.y * 0.5 + 0.5);
    vec3 color = direct + amb * albedo * (1.0 - metallic) * ao + env * Fa * (1.0 - 0.6 * rough) * ao;

    // Nebel nur in der Perspektive
    if (!ortho) {
        float dist = length(pc.camPos.xyz - vWorldPos);
        float fog = 1.0 - exp(-dist * frame.misc.z);
        color = mix(color, kHorizon, fog);
    }

    // Hervorhebung (1 = Maus darüber, 2 = ausgewählt, 3 = Werkzeugziel)
    float hl = o.params.z;
    if (hl > 0.5) {
        vec3 hc = hl > 2.5 ? vec3(1.0, 0.25, 0.15) : (hl > 1.5 ? vec3(1.0, 0.55, 0.1) : vec3(0.35, 0.7, 1.0));
        float rim = pow(1.0 - NoV, 2.0);
        color = mix(color, hc, 0.10 + 0.55 * rim);
    }

    outColor = vec4(tonemap(color), 1.0);
}
