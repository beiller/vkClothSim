#version 450

layout(location = 0) in vec3 vN;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec3 vWorldPos;
layout(location = 0) out vec4 outColor;

layout(binding = 3) uniform UBO { mat4 viewProj; vec3 camPos; } ubo;
layout(binding = 4) uniform Model { mat4 model; vec3 baseColor; float metallic; float roughness; } modelU;
layout(binding = 6) uniform sampler2D albedoTex;
layout(binding = 7) uniform sampler2D roughTex;
layout(binding = 8) uniform sampler2D metalTex;
layout(binding = 9) uniform samplerCube envPrefilter;
layout(binding = 10) uniform samplerCube envIrradiance;
layout(binding = 11) uniform sampler2D brdfLtc;

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
    float NoV = max(dot(N, V), 0.0) + 1e-4;

    vec3 albedo = modelU.baseColor * texture(albedoTex, vUv).rgb;
    float metal = modelU.metallic * texture(metalTex, vUv).r;
    float rough = clamp(modelU.roughness * texture(roughTex, vUv).r, 0.04, 1.0);
    float a = rough;
    vec3 F0 = mix(vec3(0.04), albedo, metal);

    vec3 R = reflect(-V, N);
    vec3 irr = texture(envIrradiance, N).rgb;
    vec3 prefilt = textureLod(envPrefilter, R, rough * 4.0).rgb;
    vec3 specF = fSchlick(F0, 1.0 - NoV);
    vec3 indirectDiffuse = irr * albedo * (1.0 - metal) / PI;
    vec3 indirectSpecular = prefilt * specF;

    vec3 L = normalize(vec3(0.4, 0.8, 0.3));
    vec3 H = normalize(V + L);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    vec3 F = fSchlick(F0, VoH);
    vec3 spec = dGGX(NoH, a) * vSmith(NoV, NoL, a) * F;
    vec3 kd = (1.0 - F) * (1.0 - metal);
    vec3 diff = kd * albedo / PI;

    vec3 lightCol = vec3(1.0, 0.97, 0.92) * 1.5;
    vec3 color = (diff + spec) * lightCol * NoL + indirectDiffuse + indirectSpecular;

    outColor = vec4(color, 1.0);
}
