#version 450
#include "common.glsl"

// Flammen, Rauch, Funken, Kältenebel: Rechtecke zur Kamera gedreht (auf der CPU), Form und Bewegung im Fragment-Shader
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inData;  // xy = Lage im Rechteck (0..1, y = 0 unten), z = Stärke, w = Art·10 + Zufall

layout(location = 0) out vec4 vData;
layout(location = 1) out vec3 vWorld;

void main() {
    vData = inData;
    vWorld = inPos;
    gl_Position = pc.viewProj * vec4(inPos, 1.0);
}
