#version 450
#include "common.glsl"

// Ausgabe vormultipliziert: Flammen und Funken leuchten (Alpha 0 = nur addieren), Rauch und Nebel decken ab.
layout(location = 0) in vec4 vData;
layout(location = 1) in vec3 vWorld;
layout(location = 0) out vec4 outColor;

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

float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) {
        s += a * noise(p);
        p = p * 2.07 + 13.7;
        a *= 0.5;
    }
    return s;
}

// Glut- und Flammenfarbe: dunkelrot -> orange -> gelb -> fast weiß
vec3 flameColor(float t) {
    vec3 c = mix(vec3(0.45, 0.02, 0.0), vec3(1.0, 0.28, 0.02), smoothstep(0.0, 0.35, t));
    c = mix(c, vec3(1.0, 0.62, 0.12), smoothstep(0.3, 0.65, t));
    c = mix(c, vec3(1.0, 0.92, 0.7), smoothstep(0.7, 1.0, t));
    return c;
}

void main() {
    vec2 uv = vData.xy;
    float strength = vData.z;
    float kind = floor(vData.w / 10.0 + 0.001);
    float seed = vData.w - kind * 10.0;
    float t = pc.params.y;

    if (kind < 0.5 || kind > 3.5) {
        // ---- Flamme (Art 0) bzw. Brennerflamme (Art 4): unten breit, oben spitz, Zungen steigen auf
        vec2 p = vec2(uv.x * 2.0 - 1.0, uv.y);
        float rise = kind > 3.5 ? 9.0 : 2.2;
        // große, langsame Wirbel formen die Zungen, feine schnelle Wirbel lassen den Rand flackern
        float n = fbm(vec2(p.x * 1.4 + seed * 17.0, p.y * 1.8 - t * rise + seed * 7.0));
        float n2 = fbm(vec2(p.x * 3.0 - seed * 5.0, p.y * 3.5 - t * rise * 1.6));
        float sway = (n - 0.5) * 0.9 * p.y * p.y;  // oben weht die Flamme hin und her
        float x = abs(p.x + sway);
        float width = 0.95 * pow(max(1.0 - p.y, 0.0), 0.65) * (0.75 + 0.5 * n);  // unten breit, oben spitz
        float body = 1.0 - smoothstep(width * 0.35, width, x);
        body *= smoothstep(0.0, 0.1, p.y);                                 // weicher Fuß
        body *= 1.0 - smoothstep(0.35, 0.95, p.y + (n2 - 0.5) * 0.6);       // Zungen reißen oben ab
        float core = (1.0 - smoothstep(0.0, width * 0.6, x)) * (1.0 - smoothstep(0.0, 0.5, p.y));
        float heat = clamp(body * (0.35 + 0.65 * core) * (0.75 + 0.5 * n2), 0.0, 1.0);
        vec3 col;
        if (kind > 3.5) {  // Brenner: blauer Kern, außen heller Schleier
            col = mix(vec3(0.1, 0.25, 1.0), vec3(0.7, 0.82, 1.0), smoothstep(0.3, 0.95, heat)) * heat * 1.2;
        } else {
            col = flameColor(heat) * heat * 0.9;
        }
        col *= strength;
        outColor = vec4(tonemap(col), 0.0);
        return;
    }
    vec2 p = uv * 2.0 - 1.0;
    float r = length(p);
    if (kind < 1.5) {
        // ---- Rauch (Art 1): weiche, wabernde Wolke. Zufall = Helligkeit (Holz hellgrau, Gummi schwarz)
        float n = fbm(p * 1.6 + vec2(seed * 31.0 + vData.z * 7.0, -t * 0.35));
        float m = fbm(p * 4.0 + vec2(-t * 0.6, seed * 13.0));
        float puff = 1.0 - smoothstep(0.0, 1.0, r + (n - 0.5) * 0.9);
        float a = puff * puff * (0.6 + 0.8 * m) * strength;
        vec3 light = mix(frame.groundColor.rgb, frame.skyColor.rgb, 0.65) * 1.4 + frame.sunColor.rgb * 0.12;
        vec3 c = tonemap(light * mix(0.05, 0.55, seed) * (0.8 + 0.4 * n));
        outColor = vec4(c * a, a);
    } else if (kind < 2.5) {
        // ---- Funke (Art 2): kleiner glühender Punkt
        float a = 1.0 - smoothstep(0.0, 1.0, r);
        outColor = vec4(tonemap(vec3(1.0, 0.38, 0.05) * a * a * 1.3 * strength), 0.0);
    } else {
        // ---- Kältenebel (Art 3): weiß, sinkt ab
        float n = fbm(p * 2.0 + vec2(seed * 19.0, t * 0.5));
        float a = (1.0 - smoothstep(0.2, 1.0, r + (n - 0.5) * 0.7)) * strength * 0.6;
        vec3 c = tonemap(vec3(0.9, 0.94, 1.0) * 1.6);
        outColor = vec4(c * a, a);
    }
}
