#version 450
#include "common.glsl"

// Wasseroberfläche ohne Vertex-Puffer: das Gitter entsteht aus gl_VertexIndex.
// pc.params: x = Gewässer, y = Zeit, z = 0 Oberfläche / 1 Seitenwände (Becken), w = Vierecke je Seite
layout(location = 0) out vec3 vPos;
layout(location = 1) flat out int vWater;
layout(location = 2) flat out vec3 vSide;  // Seitenwand: Normale nach außen, Oberfläche: 0

const ivec2 kCorner[6] = ivec2[](ivec2(0, 0), ivec2(0, 1), ivec2(1, 1), ivec2(0, 0), ivec2(1, 1), ivec2(1, 0));

// Meer: Gitter um die Kamera, nahe fein (8 cm), nach außen immer gröber, bis 25 km
float seaMap(float s) { return 8.0 * s + 300.0 * s * s * s + 25000.0 * pow(abs(s), 9.0) * sign(s); }

void main() {
    int w = int(pc.params.x + 0.5), n = max(int(pc.params.w + 0.5), 1);
    vWater = w;
    int quad = gl_VertexIndex / 6;
    ivec2 cor = kCorner[gl_VertexIndex % 6];
    vec3 lo = frame.water[w * 8].xyz, hi = frame.water[w * 8 + 1].xyz;
    vec3 p;
    vec2 g;
    vSide = vec3(0.0);
    if (pc.params.z < 0.5) {
        vec2 uv = vec2(ivec2(quad % n, quad / n) + cor) / float(n);
        float fade = 1.0;
        if (waterIsSea(w)) {
            vec2 s = uv * 2.0 - 1.0;
            p = vec3(pc.camPos.x + seaMap(s.x), hi.y, pc.camPos.z + seaMap(s.y));
            // weit weg wären die Ecken zu grob für die Wellen: dort glatt (die Normalen im Fragment-Shader haben sie trotzdem)
            float lambda0 = frame.water[w * 8].w;
            fade = 1.0 - smoothstep(3.0 * lambda0, 12.0 * lambda0, length(p.xz - pc.camPos.xz));
        } else {
            p = vec3(mix(lo.x, hi.x, uv.x), hi.y, mix(lo.z, hi.z, uv.y));
        }
        p.y += waterWave(w, p.xz, g) * fade + waterFlow(w, p.xz).w;  // Wellen und Stau vor Hindernissen
    } else {
        // Seitenwände des Beckens: 4 Seiten mit je n Vierecken, oben folgen sie den Wellen
        int side = quad / n;
        float u = float(quad % n + cor.x) / float(n);
        vec3 a, b;  // untere Kante
        if (side == 0) {
            a = vec3(lo.x, lo.y, lo.z);
            b = vec3(hi.x, lo.y, lo.z);
            vSide = vec3(0, 0, -1);
        } else if (side == 1) {
            a = vec3(hi.x, lo.y, lo.z);
            b = vec3(hi.x, lo.y, hi.z);
            vSide = vec3(1, 0, 0);
        } else if (side == 2) {
            a = vec3(hi.x, lo.y, hi.z);
            b = vec3(lo.x, lo.y, hi.z);
            vSide = vec3(0, 0, 1);
        } else {
            a = vec3(lo.x, lo.y, hi.z);
            b = vec3(lo.x, lo.y, lo.z);
            vSide = vec3(-1, 0, 0);
        }
        p = mix(a, b, u);
        if (cor.y == 1) p.y = hi.y + waterSurface(w, p.xz, g);
    }
    vPos = p;
    gl_Position = pc.viewProj * vec4(p, 1.0);
}
