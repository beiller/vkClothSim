#pragma once

// ported from Blender's BLI math (math_rotation_c.cc, math_matrix_c.cc)
struct DualQuat {
    float quat[4] = {1, 0, 0, 0};
    float trans[4] = {0, 0, 0, 0};
    float scale[4][4] = {};
    float scale_weight = 0.0f;
    float pad[3] = {};
};
static_assert(sizeof(DualQuat) == 112, "DualQuat must match the GLSL DQ stride");

namespace dq {
void mat4ToDquat(DualQuat* out, const float basemat[4][4], const float mat[4][4]);
void dquatToMat4(float R[4][4], const DualQuat* dq);
void addWeightedDqDq(DualQuat* sum, const DualQuat* dq, float weight);
void addWeightedDqDqPivot(DualQuat* sum, const DualQuat* dq, const float pivot[3], float weight,
                          bool computeScaleMatrix);
void normalizeDq(DualQuat* dq, float totweight);
void mulV3m3Dq(float r[3], float R[3][3], DualQuat* dq);
}
