#version 450
#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;  // Farbe je Eckpunkt (Gelände), sonst weiß

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vLocalPos;
layout(location = 3) flat out uint vObj;
layout(location = 4) out vec4 vColor;  // rgb Farbe (Gelände), a = 0 Rinde

void main() {
    ObjectData o = objs[gl_InstanceIndex];
    vec4 wp = o.model * vec4(inPos, 1.0);
    // Bewuchs (Flora.h): Spitzen von Gras und Blumen (a = Höhe / 2 m) wiegen sich im Wind; zum Rand params.w (m) hin
    // in den Boden. Ferne Stufe des Grases: heat0.x (m) = innerer Rand, davor im Boden (dort steht das nahe Gras)
    if (int(o.params.x + 0.5) == 13 && inColor.a > 0.0) {
        float h = inColor.a * 2.0;
        vec2 ap = wp.xz + frame.originMod.xz;
        float t = pc.params.y;
        float gust = 0.6 + 0.4 * sin(t * 0.45 + ap.x * 0.031 + ap.y * 0.023);
        float sway = h * gust * (0.07 * sin(t * 1.7 + dot(ap, vec2(0.23, 0.17))) + 0.035 * sin(t * 3.9 + dot(ap, vec2(1.3, -0.9))) + 0.05);
        wp.xz += vec2(0.8, 0.6) * sway;
        wp.y -= abs(sway) * 0.35;
        float d = length(pc.camPos.xz - wp.xz);
        if (o.params.w > 0.0) wp.y -= h * (1.0 - clamp((o.params.w - d) / (o.params.w * 0.25), 0.0, 1.0));
        if (o.heat0.x > 0.0) wp.y -= h * (1.0 - clamp((d - o.heat0.x * 0.8) / (o.heat0.x * 0.2), 0.0, 1.0));
    }
    vWorldPos = wp.xyz;
    vNormal = mat3(o.model) * inNormal;
    vLocalPos = inPos;
    vObj = gl_InstanceIndex;
    vColor = inColor;
    gl_Position = pc.viewProj * wp;
}
