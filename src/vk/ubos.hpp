#pragma once
// GPU-side uniform / push-constant layouts. Padding is hand-computed for the
// std140 (UBO) and std430 (push constant) rules; the static_asserts pin each
// size to its shader counterpart — do not reorder fields without updating GLSL.

inline constexpr int kMaxLights = 8;

struct ModelUbo {
    float model[16];    // 0
    float baseColor[3]; // 64
    float metallic;     // 76
    float roughness;    // 80
    float baseAlpha;    // 84
    float alphaCutoff;  // 88
    float alphaMode;    // 92  0 opaque, 1 mask, 2 blend
    float emissive[3];   // 96
    float pad0;          // 108 (std140: the ssssWeight vec3 16-aligns to 112)
    float ssssWeight[3]; // 112
    float refraction;    // 124
    float ior;           // 128
    float normalScale;   // 132
    float ssssScale;     // 136
    float ssssSplit;     // 140 pre/post-scatter split exponent, 0..1
    float pad[4];        // 144 (the shader's vec3 pad sits at 144; block size 160)
};
static_assert(sizeof(ModelUbo) == 160, "ModelUbo must match the shader std140 layout");

struct ViewUbo {
    float viewProj[16]; // 0
    float view[16];     // 64
    float camPos[3];    // 128 (std140: a vec3 is 12 bytes; the next scalar lands at 140)
    float envIntensity; // 140
    float shadowTexels; // 144
    float pad[3];       // 148 (pad so the 16-aligned frustum lands at 160)
    float frustum[4];   // 160 tanL tanR tanU tanD
    float zNear;        // 176
    float zFar;         // 180
    float screenSize[2];// 184
    float sssCurvScale; // 192 (was pad2[0]) curvature LUT input scale
    float pad2[3];      // 196
};
static_assert(sizeof(ViewUbo) == 208, "ViewUbo must match the shader std140 layout");

// the SSS compute's push constants (std430: scalars are 4-byte aligned)
struct SssPc {
    float screenSize[2]; // 0
    float zNear;         // 8
    float zFar;          // 12
    float unitSizeH;     // 16  2/(tanR-tanL): world radius -> uv step, horizontal axis
    float unitSizeV;     // 20  2/(tanU-tanD): vertical axis (per-axis: circular blur)
    float depthScale;    // 24
    float vertical;      // 28  1 = vertical pass (dir 0,1), 0 = horizontal
    float depthReject;   // 32  tap rejection across depth steps (1 / world-space radius fraction)
};
static_assert(sizeof(SssPc) == 36, "SssPc must match the shader std430 layout");

struct LightGPU {
    float pos[3];
    float intensity;
    float color[3];
    float radius;
    float on;
    float shadowNear;
    float shadowFar;
    float shadowNormalBias;
    float biasBase;
    float biasSlope;
    float searchScale;
    float maxRadius;
};
static_assert(sizeof(LightGPU) == 64, "LightGPU must match the shader std430 layout");

struct LightsUbo {
    int count;
    float pad[3];
    LightGPU lights[kMaxLights];
};
static_assert(sizeof(LightsUbo) == 16 + kMaxLights * 64, "LightsUbo must match the shader std430 layout");
