#version 450

layout(location = 0) in vec3 vN;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec3 vWorldPos;
layout(location = 0) out vec4 outColor;

layout(binding = 3) uniform UBO {
    mat4 viewProj;
    mat4 view;
    vec3 camPos;
    float envIntensity;
    float shadowTexels;
    // no pad here: a std140 float array has 16-byte stride (it would shift frustum to 208);
    // the vec4 below 16-aligns to 160 and leaves 148..160 unused, matching the C++ ViewUbo
    vec4 frustum; // tanL tanR tanU tanD (signed, supports the asymmetric VR frustums)
    float zNear;
    float zFar;
    vec2 screenSize;
    vec4 pad2;
} ubo;
layout(binding = 4) uniform Model { mat4 model; vec3 baseColor; float metallic; float roughness; float baseAlpha; float alphaCutoff; float alphaMode; vec3 emissive; float ssssWeight; float refraction; vec2 pad; }
    modelU;
layout(binding = 6) uniform sampler2D albedoTex;
layout(binding = 7) uniform sampler2D mrTex; // glTF metallicRoughness: G = roughness, B = metallic
layout(binding = 9) uniform samplerCube envPrefilter;
layout(binding = 10) uniform samplerCube envIrradiance;
layout(binding = 11) uniform sampler2D normalTex; // tangent-space normal; (0.5, 0.5, 1) = off
layout(binding = 2) uniform sampler2D sceneDepth; // scene depth copy (glass refraction)
layout(binding = 12) uniform samplerCubeArray shadowCube;
layout(binding = 14) uniform sampler2D emissiveTex;
layout(binding = 15) uniform sampler2D sceneColor; // the SSS-blurred scene (glass refraction; mips = roughness blur)
layout(push_constant) uniform PassId { float passId; } passU; // 0 = opaque (alpha out = the ssssWeight side channel), 1 = transparent (alpha out = blend factor)

const int MAX_LIGHTS = 8;
struct Light {
    vec4 posIntensity;
    vec4 colorRadius;
    vec4 onNearFar;
    vec4 biasParams;
};
layout(binding = 13) uniform Lights {
    int count;
    Light lights[MAX_LIGHTS];
} lts;

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

float shadowSample(vec3 dir, float dist, float bias, int idx) {
    return step(dist - bias, texture(shadowCube, vec4(dir, idx)).r);
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

float pcfShadow(vec3 N, vec3 L, float dist, float bias, float radius, int idx) {
    vec3 t = shadowTangent(N, L);
    vec3 b = cross(L, t);
    float texel = PI / (2.0 * ubo.shadowTexels);
    float sum = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec3 dir = normalize(L + (t * POISSON[i].x + b * POISSON[i].y) * texel * radius);
        sum += shadowSample(dir, dist, bias, idx);
    }
    return sum / 16.0;
}

float pcssShadow(vec3 N, vec3 L, float NoL, float dist, Light li, int idx) {
    if (dist > li.onNearFar.z || dist < li.onNearFar.y)
        return 1.0;
    float bias = li.biasParams.x + li.biasParams.y * (1.0 - NoL);
    vec3 t = shadowTangent(N, L);
    vec3 b = cross(L, t);
    float texel = PI / (2.0 * ubo.shadowTexels);
    float sum = 0.0;
    int count = 0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            float d = texture(shadowCube, vec4(normalize(L + (t * float(x) + b * float(y)) * texel * li.biasParams.z), idx)).r;
            if (d < dist) {
                sum += d;
                ++count;
            }
        }
    if (count == 0)
        return 1.0;
    float avg = sum / float(count);
    float penumbra = (li.colorRadius.w * (dist - avg) / max(avg * dist, 1e-4)) / texel;
    float scale = ubo.shadowTexels / 256.0;
    float radius = clamp(penumbra, (1.0 + li.colorRadius.w * 3.0) * scale, li.biasParams.w * scale);
    return pcfShadow(N, L, dist, bias, radius, idx);
}

// the depth copy is 0..1 (near = 0, far = 1); linearize to a view distance (positive in
// front). the background (db = 1) gives zFar; the near plane (db = 0) gives zNear
float depthToDist(float db) {
    return (ubo.zNear * ubo.zFar) / ((1.0 - db) * ubo.zFar + db * ubo.zNear);
}

// clip (-1..1 after the viewport flip) + distance -> view space (z = -dist); the signed
// frustum tangents make this exact for the asymmetric VR eye projections
vec3 clipToView(vec2 clip, float dist) {
    vec4 f = ubo.frustum;
    return vec3(((f.x + f.y) + clip.x * (f.y - f.x)) * 0.5 * dist,
                ((f.z + f.w) + clip.y * (f.w - f.z)) * 0.5 * dist,
                -dist);
}

// perturb N by the normal map; the 8-bit neutral texel (128,128,255) is not exactly (0,0,1)
void applyNormalMap(inout vec3 N, vec2 uv, vec3 worldPos) {
    vec3 nT = texture(normalTex, uv).rgb * 2.0 - 1.0;
    const float neu = 0.004; // 128/255 * 2 - 1
    if (abs(nT.x) <= neu && abs(nT.y) <= neu && abs(nT.z - 1.0) <= neu)
        return;
    // derivative-based TBN (no tangent attribute in the geometry)
    vec3 dpdx = dFdx(worldPos);
    vec3 dpdy = dFdy(worldPos);
    vec2 dudx = dFdx(uv);
    vec2 dudy = dFdy(uv);
    vec3 T = dpdx * dudy.y - dpdy * dudx.y;
    vec3 B = -(dpdx * dudy.x - dpdy * dudx.x);
    if (dot(T, T) < 1e-12 || dot(B, B) < 1e-12)
        return; // degenerate uv on this pixel: keep the analytic normal
    T = normalize(T - N * dot(T, N));
    B = cross(N, T);
    N = normalize(T * nT.x + B * nT.y + N * nT.z);
}

void main() {
    vec3 N = normalize(vN);
    if (!gl_FrontFacing)
        N = -N; // two-sided: light the back face as front-facing; an un-flipped N gives NoV/NoH ~ 0 and
                // the GGX denominator max(4*NoL*NoH, 1e-4) then blows the specular up to white
    vec3 V = normalize(ubo.camPos - vWorldPos);
    applyNormalMap(N, vUv, vWorldPos);
    float NoV = max(dot(N, V), 0.0) + 1e-4;

    vec4 mr = texture(mrTex, vUv);
    vec4 alb = texture(albedoTex, vUv);
    vec3 albedo = modelU.baseColor * alb.rgb;
    float alpha = modelU.baseAlpha * alb.a;
    if (modelU.alphaMode > 0.5 && modelU.alphaMode < 1.5 && alpha < modelU.alphaCutoff)
        discard; // MASK: alpha test
    float metal = modelU.metallic * mr.b;
    float rough = clamp(modelU.roughness * mr.g, 0.04, 1.0);
    float a = rough;
    vec3 F0 = mix(vec3(0.04), albedo, metal);

    vec3 R = reflect(-V, N);
    vec3 irr = texture(envIrradiance, N).rgb;
    vec3 prefilt = textureLod(envPrefilter, R, rough * 4.0).rgb;
    vec3 specF = fSchlick(F0, 1.0 - NoV);
    vec3 indirectDiffuse = irr * albedo * (1.0 - metal) * ubo.envIntensity / PI;
    vec3 indirectSpecular = prefilt * specF * ubo.envIntensity;

    // the diff/spec are accumulated separately: the glass scales the diffuse by (1 - refraction)
    vec3 diffSum = indirectDiffuse;
    vec3 specSum = indirectSpecular;
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= lts.count)
            break;
        Light li = lts.lights[i];
        if (li.onNearFar.x < 0.5)
            continue;
        vec3 Ld = li.posIntensity.xyz - vWorldPos;
        float d2 = dot(Ld, Ld);
        float dist = sqrt(d2);
        vec3 L = Ld / max(dist, 1e-4);
        float NoL = max(dot(N, L), 0.0);
        if (NoL <= 0.0)
            continue;
        vec3 H = normalize(V + L);
        float NoH = max(dot(N, H), 0.0);
        float VoH = max(dot(V, H), 0.0);
        vec3 F = fSchlick(F0, VoH);
        vec3 spec = dGGX(NoH, a) * vSmith(NoV, NoL, a) * F / max(4.0 * NoL * NoH, 1e-4);
        vec3 kd = (1.0 - F) * (1.0 - metal);
        vec3 diff = kd * albedo / PI;
        float shadow = pcssShadow(N, L, NoL, dist, li, i);
        vec3 radiance = li.colorRadius.xyz * li.posIntensity.w / max(d2, 1e-3);
        diffSum += radiance * NoL * diff * shadow;
        specSum += radiance * NoL * spec * shadow;
    }

    // glass (screen-space refraction): the refraction field (0..1) drives both the
    // transmission energy (linear) and the screen-uv offset (max 0.1 uv at 1.0); an
    // off-screen ray falls back to the environment in the refracted direction
    vec3 trans = vec3(0.0);
    float refAmount = modelU.refraction;
    if (refAmount > 0.0) {
        float refEnergy = refAmount;
        diffSum *= 1.0 - refEnergy;
        vec3 refN = normalize((ubo.view * vec4(N, 0.0)).xyz);
        vec2 screenUv = gl_FragCoord.xy / ubo.screenSize;
        vec2 refOfs = screenUv - refN.xy * (refAmount * 0.1);
        if (refOfs.x >= 0.0 && refOfs.x <= 1.0 && refOfs.y >= 0.0 && refOfs.y <= 1.0) {
            // the depth is read at the offset uv, the position reconstructed at the current uv
            // (Godot's approximation); the fade hides the refraction when the refracted point
            // is behind this fragment
            vec3 pV = (ubo.view * vec4(vWorldPos, 1.0)).xyz;
            float d = depthToDist(textureLod(sceneDepth, refOfs, 0.0).r);
            vec2 clip = vec2(screenUv.x * 2.0 - 1.0, 1.0 - screenUv.y * 2.0);
            vec3 refViewPos = clipToView(clip, d);
            float see = smoothstep(0.0, 1.0, pV.z - refViewPos.z);
            vec3 refCol = textureLod(sceneColor, refOfs, rough * 8.0).rgb;
            trans += (1.0 - specF) * see * (1.0 - metal) * refEnergy * refCol;
        } else { // off-screen ray: the environment, sampled in the refracted direction
            vec2 refNdc = vec2(refOfs.x * 2.0 - 1.0, refOfs.y * 2.0 - 1.0);
            float tanH = 0.5 * (ubo.frustum.y - ubo.frustum.x);
            float tanV = 0.5 * (ubo.frustum.z - ubo.frustum.w);
            vec3 refDir = normalize(vec3(refNdc.x * tanH, refNdc.y * tanV, -1.0));
            vec3 refCol = textureLod(envPrefilter, refDir, rough * 4.0).rgb * ubo.envIntensity;
            trans += (1.0 - specF) * (1.0 - metal) * refEnergy * refCol;
        }
    }

    vec3 color = diffSum + specSum + trans;
    color += modelU.emissive * texture(emissiveTex, vUv).rgb; // unlit HDR emission, tonemapped with the rest
    // opaque pass: the alpha is the ssssWeight side channel (the background clears to 0); the
    // transparent pass: the alpha is the blend factor (the glass self-composites over the scene)
    outColor = vec4(color, passU.passId < 0.5 ? modelU.ssssWeight : alpha);
}
