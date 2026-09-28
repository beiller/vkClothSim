#version 450

layout(location = 0) in vec3 vN;
layout(location = 2) in vec3 vWorldPos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushVP { mat4 viewProj; vec4 lightPosBias; } pc;

void main() {
    vec3 toLight = pc.lightPosBias.xyz - vWorldPos;
    float dist = length(toLight);
    vec3 L = toLight / max(dist, 1e-4);
    float facing = max(dot(normalize(vN), L), 0.0);
    dist -= facing * pc.lightPosBias.w;
    outColor = vec4(dist, 0.0, 0.0, 1.0);
}
