#version 450

layout(location = 0) in vec3 vN;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec3 vWorldPos;
layout(location = 0) out vec4 outColor;

layout(binding = 3) uniform UBO { mat4 viewProj; vec3 camPos; } ubo;
layout(binding = 4) uniform Model { mat4 model; vec3 baseColor; float metallic; float roughness; } modelU;

const float PI = 3.14159265359;

float dGGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float vSmith(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-4);
}

vec3 fSchlick(vec3 F0, float VoH) {
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}

void main() {
    vec3 N = normalize(vN);
    vec3 V = normalize(ubo.camPos - vWorldPos);
    vec3 L = normalize(vec3(0.4, 0.8, 0.3));
    vec3 H = normalize(V + L);

    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0) + 1e-4;
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    vec3 albedo = modelU.baseColor;

    float a = clamp(modelU.roughness, 0.04, 1.0);
    a = a * a;
    vec3 F0 = mix(vec3(0.04), albedo, modelU.metallic);
    vec3 F = fSchlick(F0, VoH);
    vec3 spec = dGGX(NoH, a) * vSmith(NoV, NoL, a) * F;
    vec3 kd = (1.0 - F) * (1.0 - modelU.metallic);
    vec3 diff = kd * albedo / PI;

    vec3 lightCol = vec3(1.0, 0.97, 0.92) * 3.0;
    vec3 color = (diff + spec) * lightCol * NoL;
    color += albedo * 0.12 * (1.0 - modelU.metallic);

    outColor = vec4(color, 1.0);
}
