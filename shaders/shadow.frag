#version 450

layout(location = 1) in vec3 vN;
layout(location = 2) in vec3 vWorldPos;
layout(location = 0) out vec4 outColor;

layout(binding = 3) uniform UBO {
    mat4 viewProj;
    vec3 camPos;
    vec3 lightPos;
    float lightIntensity;
    vec3 lightColor;
    float lightRadius;
    float lightOn;
    float envIntensity;
    float shadowNear;
    float shadowFar;
    float shadowNormalBias;
} ubo;

void main() {
    vec3 toLight = ubo.lightPos - vWorldPos;
    float dist = length(toLight);
    vec3 L = toLight / max(dist, 1e-4);
    float facing = max(dot(normalize(vN), L), 0.0);
    dist -= facing * ubo.shadowNormalBias;
    outColor = vec4(dist, 0.0, 0.0, 1.0);
}
