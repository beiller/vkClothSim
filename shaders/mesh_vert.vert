#version 450

layout(binding = 0) buffer Pos { float p[]; } posB;
layout(binding = 1) buffer Nrm { float p[]; } nrmB;
layout(binding = 8) buffer PosPrev { float p[]; } posPrevB; // previous tick's positions (soft-body interpolation)
layout(binding = 3) uniform UBO { mat4 viewProj; mat4 view; vec3 camPos; } ubo;
// std140 layout must match ModelUbo in src/vk/ubos.hpp — do not reorder without updating both.
layout(binding = 4) uniform Model { mat4 model; vec3 baseColor; float metallic; float roughness;
    float baseAlpha; float alphaCutoff; float alphaMode; vec3 emissive; float pad0; vec3 ssssWeight;
    float refraction; float ior; float normalScale; float ssssScale; float ssssSplit;
    float interpSoft; float interpAlpha; } modelU;
layout(binding = 5) buffer Uv { float p[]; } uvB;

layout(location = 0) out vec3 vN;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec3 vWorldPos;

void main() {
    int o = gl_VertexIndex * 3;
    int u = gl_VertexIndex * 2;
    vec3 pv = vec3(posB.p[o], posB.p[o + 1], posB.p[o + 2]);
    if (modelU.interpSoft > 0.5) // soft body: mix the last two tick positions at the frame's alpha
        pv = mix(vec3(posPrevB.p[o], posPrevB.p[o + 1], posPrevB.p[o + 2]), pv, modelU.interpAlpha);
    vec4 wp = modelU.model * vec4(pv, 1.0);
    vN = transpose(mat3(inverse(modelU.model))) * vec3(nrmB.p[o], nrmB.p[o + 1], nrmB.p[o + 2]); // inverse-transpose (scale-aware)
    vUv = vec2(uvB.p[u], uvB.p[u + 1]);
    vWorldPos = wp.xyz;
    gl_Position = ubo.viewProj * wp;
}
