#pragma once
// Dual-quaternion skinning math, ported from Blender's BLI math (blenlib/intern/math_rotation_c.cc,
// math_matrix_c.cc). The six DQ functions are ported verbatim (same operations, same order); the
// linear-algebra helpers compute the same math in the same column-major m[col][row] mat4 layout.
// quat is stored {w,x,y,z} (Blender convention), matching the GPU DQ struct byte-for-byte.
//
// DualQuat is 112 B: 100 B of Blender layout (quat 16 + trans 16 + scale 64 + scale_weight 4)
// plus 12 B pad so sizeof matches the GLSL std140 stride of the on-GPU DQ struct.

struct DualQuat {
    float quat[4] = {1, 0, 0, 0}; // {w,x,y,z}
    float trans[4] = {0, 0, 0, 0}; // full 4-vector dual part
    float scale[4][4] = {}; // model-space scale, m[col][row]
    float scale_weight = 0.0f;
    float pad[3] = {}; // pad to 112 B (matches the GLSL DQ stride)
};
static_assert(sizeof(DualQuat) == 112, "DualQuat must match the GLSL DQ stride");

namespace dq {
// decompose mat (relative to basemat) into a DQ: DQ carries (rotation, translation), and the
// non-uniform scale is split out into `scale` (blended "preserve-volume" style) with weight 1.
void mat4ToDquat(DualQuat* out, const float basemat[4][4], const float mat[4][4]);
// reconstruct a mat4 from a DQ (rotation + translation only; scale is not folded in here).
void dquatToMat4(float R[4][4], const DualQuat* dq);
// accumulate `weight * dq` into `sum` (Blender's dual-part sign-flip for a stable running sum).
void addWeightedDqDq(DualQuat* sum, const DualQuat* dq, float weight);
// same, but the scale translation is applied about `pivot` first (the per-joint bind position).
void addWeightedDqDqPivot(DualQuat* sum, const DualQuat* dq, const float pivot[3], float weight,
                          bool computeScaleMatrix);
// normalize the accumulated DQ by the total weight (real part unit, dual part scaled, scale ÷ W).
void normalizeDq(DualQuat* dq, float totweight);
// apply the (possibly scaled) DQ to a point: p' = M(q)·(S·p) + t(q,d), all ÷ |q|².
void mulV3m3Dq(float r[3], float R[3][3], DualQuat* dq);
} // namespace dq
