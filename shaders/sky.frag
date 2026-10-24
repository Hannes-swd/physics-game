#version 450
#include "common.glsl"

layout(location = 0) in vec4 vNear;
layout(location = 1) in vec4 vFar;
layout(location = 2) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    if (pc.camPos.w > 0.5) {
        // Technische Ansicht: ruhiger Verlauf wie in CAD-Programmen
        vec3 top = vec3(0.23, 0.25, 0.29), bottom = vec3(0.12, 0.13, 0.15);
        outColor = vec4(mix(top, bottom, vUv.y), 1.0);
        return;
    }
    vec3 dir = normalize(vFar.xyz / vFar.w - vNear.xyz / vNear.w);
    vec3 col = skyColor(dir);
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
    float dither = (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
    outColor = vec4(tonemap(col) + dither, 1.0);
}
