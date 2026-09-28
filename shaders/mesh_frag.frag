#version 450

layout(location = 0) in vec3 vN;
layout(location = 1) in vec2 vUv;
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
    float shadowBiasBase;
    float shadowBiasSlope;
    float shadowSearchScale;
    float shadowMaxRadius;
} ubo;
layout(binding = 4) uniform Model { mat4 model; vec3 baseColor; float metallic; float roughness; } modelU;
layout(binding = 6) uniform sampler2D albedoTex;
layout(binding = 7) uniform sampler2D roughTex;
layout(binding = 8) uniform sampler2D metalTex;
layout(binding = 9) uniform samplerCube envPrefilter;
layout(binding = 10) uniform samplerCube envIrradiance;
layout(binding = 12) uniform samplerCube shadowCube;

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

const float SHADOW_TEXELS = 1024.0;

float shadowSample(vec3 dir, float dist, float bias) {
    return step(dist - bias, texture(shadowCube, dir).r);
}

vec3 shadowTangent(vec3 N, vec3 L) {
    vec3 ref = abs(N.x) > 0.5 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(ref, L));
}

const vec2 POISSON[16] = {
    vec2(0.0, 0.0),
    vec2(0.5, -0.5),
    vec2(-0.5, -0.5),
    vec2(-0.5, 0.5),
    vec2(0.5, 0.5),
    vec2(-1.0, -0.2),
    vec2(-0.4, -0.9),
    vec2(-0.6, 0.8),
    vec2(-0.9, -0.1),
    vec2(0.4, 0.2),
    vec2(-0.2, 0.9),
    vec2(0.7, -0.6),
    vec2(0.8, 0.3),
    vec2(0.2, -0.7),
    vec2(0.9, 0.6),
    vec2(0.5, -0.4)
};

float pcfShadow(vec3 N, vec3 L, float dist, float bias, float radius) {
    vec3 t = shadowTangent(N, L);
    vec3 b = cross(L, t);
    float texel = PI / (2.0 * SHADOW_TEXELS);
    float sum = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec3 dir = normalize(L + (t * POISSON[i].x + b * POISSON[i].y) * texel * radius);
        sum += shadowSample(dir, dist, bias);
    }
    return sum / 16.0;
}

float pcssShadow(vec3 N, vec3 L, float NoL, float dist) {
    if (dist > ubo.shadowFar || dist < ubo.shadowNear)
        return 1.0;
    float bias = ubo.shadowBiasBase + ubo.shadowBiasSlope * (1.0 - NoL);
    vec3 t = shadowTangent(N, L);
    vec3 b = cross(L, t);
    float texel = PI / (2.0 * SHADOW_TEXELS);
    float sum = 0.0;
    int count = 0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            float d = texture(shadowCube, normalize(L + (t * float(x) + b * float(y)) * texel * ubo.shadowSearchScale)).r;
            if (d < dist) {
                sum += d;
                ++count;
            }
        }
    if (count == 0)
        return 1.0;
    float avg = sum / float(count);
    float penumbra = (ubo.lightRadius * (dist - avg) / max(avg * dist, 1e-4)) / texel;
    float scale = SHADOW_TEXELS / 256.0;
    float radius = clamp(penumbra, (1.0 + ubo.lightRadius * 3.0) * scale, ubo.shadowMaxRadius * scale);
    return pcfShadow(N, L, dist, bias, radius);
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
    vec3 indirectDiffuse = irr * albedo * (1.0 - metal) * ubo.envIntensity / PI;
    vec3 indirectSpecular = prefilt * specF * ubo.envIntensity;

    vec3 color = indirectDiffuse + indirectSpecular;
    if (ubo.lightOn > 0.5) {
        vec3 Ld = ubo.lightPos - vWorldPos;
        float d2 = dot(Ld, Ld);
        float dist = sqrt(d2);
        vec3 L = Ld / max(dist, 1e-4);
        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0) {
            vec3 H = normalize(V + L);
            float NoH = max(dot(N, H), 0.0);
            float VoH = max(dot(V, H), 0.0);
            vec3 F = fSchlick(F0, VoH);
            vec3 spec = dGGX(NoH, a) * vSmith(NoV, NoL, a) * F / max(4.0 * NoL * NoH, 1e-4);
            vec3 kd = (1.0 - F) * (1.0 - metal);
            vec3 diff = kd * albedo / PI;
            float shadow = pcssShadow(N, L, NoL, dist);
            vec3 radiance = ubo.lightColor * ubo.lightIntensity / max(d2, 1e-3);
            color += radiance * NoL * (spec + diff) * shadow;
        }
    }

    outColor = vec4(color, 1.0);
}
