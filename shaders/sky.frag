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
    vec3 zenith = vec3(0.16, 0.34, 0.72);
    vec3 haze = vec3(0.45, 0.47, 0.50);
    vec3 col = dir.y > 0.0 ? mix(kHorizon, zenith, pow(dir.y, 0.55)) : mix(kHorizon, haze, pow(-dir.y, 0.35));
    float s = max(dot(dir, normalize(frame.sunDir.xyz)), 0.0);
    col += vec3(1.0, 0.9, 0.75) * (pow(s, 900.0) * 30.0 + pow(s, 12.0) * 0.25);
    // leichtes Rauschen gegen Farbstufen im Verlauf
    float dither = (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
    outColor = vec4(tonemap(col) + dither, 1.0);
}
