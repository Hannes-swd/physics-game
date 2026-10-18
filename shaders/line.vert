#version 450
#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 vColor;

void main() {
    vColor = inColor;
    gl_Position = pc.viewProj * vec4(inPos, 1.0);
    // Kanten leicht zur Kamera ziehen, damit sie nicht in der Fläche verschwinden (Reversed-Z)
    if (pc.params.w > 0.5) {
        if (pc.camPos.w > 0.5) gl_Position.z += 2e-4 * gl_Position.w;
        else gl_Position.z *= 1.003;
    }
}
