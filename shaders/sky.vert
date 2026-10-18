#version 450
#include "common.glsl"

layout(location = 0) out vec4 vNear;
layout(location = 1) out vec4 vFar;
layout(location = 2) out vec2 vUv;

void main() {
    vec2 ndc = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    mat4 inv = inverse(pc.viewProj);
    vNear = inv * vec4(ndc, 1.0, 1.0);   // Reversed-Z: 1 = nah
    vFar = inv * vec4(ndc, 1e-6, 1.0);   // unendliche Projektion: 0 wäre "unendlich weit"
    vUv = ndc * 0.5 + 0.5;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
