#version 450

layout(binding = 0) buffer Pos { float p[]; } posB;
layout(binding = 1) buffer Nrm { float p[]; } nrmB;
layout(binding = 2) buffer Col { float p[]; } colB;
layout(binding = 3) uniform UBO { mat4 viewProj; } ubo;

layout(location = 0) out vec3 vN;
layout(location = 1) out vec3 vCol;

void main() {
    int o = gl_VertexIndex * 3;
    vN = vec3(nrmB.p[o], nrmB.p[o + 1], nrmB.p[o + 2]);
    vCol = vec3(colB.p[o], colB.p[o + 1], colB.p[o + 2]);
    gl_Position = ubo.viewProj * vec4(posB.p[o], posB.p[o + 1], posB.p[o + 2], 1.0);
}