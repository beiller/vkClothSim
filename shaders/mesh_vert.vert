#version 450

layout(binding = 0) buffer Pos { float p[]; } posB;
layout(binding = 1) buffer Nrm { float p[]; } nrmB;
layout(binding = 3) uniform UBO { mat4 viewProj; vec3 camPos; } ubo;
layout(binding = 4) uniform Model { mat4 model; vec3 baseColor; float metallic; float roughness; } modelU;
layout(binding = 5) buffer Uv { float p[]; } uvB;

layout(location = 0) out vec3 vN;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec3 vWorldPos;

void main() {
    int o = gl_VertexIndex * 3;
    int u = gl_VertexIndex * 2;
    vec4 wp = modelU.model * vec4(posB.p[o], posB.p[o + 1], posB.p[o + 2], 1.0);
    vN = mat3(modelU.model) * vec3(nrmB.p[o], nrmB.p[o + 1], nrmB.p[o + 2]);
    vUv = vec2(uvB.p[u], uvB.p[u + 1]);
    vWorldPos = wp.xyz;
    gl_Position = ubo.viewProj * wp;
}
