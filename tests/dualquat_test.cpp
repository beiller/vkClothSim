// Verifies the ported Blender dual-quaternion math (src/sim/dualquat.cpp).
//
// Checks:
//  1. No-scale round-trip (EXACT): mat4ToDquat(I, T·R) then mulV3m3Dq must equal (T·R)·p for a
//     range of rotations/translations. This is the path hashimoto actually uses.
//  2. Bind-pose identity: blending identity DQs with any weights yields the identity transform.
//  3. Scale presence: a scaled matrix sets scale_weight=1; an unscaled one sets it to 0.
//  4. Blender fixed-value cases (BLI_math_rotation_test.cc, DualQuaternionFromMatrix): the full
//     scale-extraction path (orthogonalize + invert + mat4_to_quat) against Blender's ground truth.
//  5. Rotation direction: a rotation DQ sends x->y under +90 deg Z (pins the transpose/inverse bug
//     that the self-consistent round-trip in [1] cannot catch).
//
// Exit 0 on pass, 1 on failure. Run: ./build/dualquat_test

#include "sim/dualquat.hpp"

#include <cmath>
#include <cstdio>

static int g_fail = 0;
static int g_checks = 0;

static void check(bool ok, const char* what, float maxerr = 0.0f) {
    ++g_checks;
    if (!ok) {
        ++g_fail;
        std::printf("  FAIL: %s (err=%.6g)\n", what, maxerr);
    }
}

static float vdist3(const float a[3], const float b[3]) {
    float d = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float e = a[i] - b[i];
        d += e * e;
    }
    return std::sqrt(d);
}

// apply a 4x4 (m[col][row]) to a point; reference transform (independent of the DQ math)
static void applyMat4(const float M[4][4], float p[3]) {
    const float x = p[0], y = p[1];
    p[0] = x * M[0][0] + y * M[1][0] + M[2][0] * p[2] + M[3][0];
    p[1] = x * M[0][1] + y * M[1][1] + M[2][1] * p[2] + M[3][1];
    p[2] = x * M[0][2] + y * M[1][2] + M[2][2] * p[2] + M[3][2];
}

// build M = T·R (m[col][row]) from a quaternion {w,x,y,z} + translation
static void rotTransMat4(float M[4][4], float w, float x, float y, float z, float tx, float ty, float tz) {
    const float l = std::sqrt(w * w + x * x + y * y + z * z);
    w /= l;
    x /= l;
    y /= l;
    z /= l;
    const double Q0 = std::sqrt(2) * w, Q1 = std::sqrt(2) * x, Q2 = std::sqrt(2) * y, Q3 = std::sqrt(2) * z;
    const double qda = Q0 * Q1, qdb = Q0 * Q2, qdc = Q0 * Q3;
    const double qaa = Q1 * Q1, qab = Q1 * Q2, qac = Q1 * Q3;
    const double qbb = Q2 * Q2, qbc = Q2 * Q3, qcc = Q3 * Q3;
    M[0][0] = (float)(1 - qbb - qcc);
    M[0][1] = (float)(qdc + qab);
    M[0][2] = (float)(-qdb + qac);
    M[0][3] = 0.0f;
    M[1][0] = (float)(-qdc + qab);
    M[1][1] = (float)(1 - qaa - qcc);
    M[1][2] = (float)(qda + qbc);
    M[1][3] = 0.0f;
    M[2][0] = (float)(qdb + qac);
    M[2][1] = (float)(-qda + qbc);
    M[2][2] = (float)(1 - qaa - qbb);
    M[2][3] = 0.0f;
    M[3][0] = tx;
    M[3][1] = ty;
    M[3][2] = tz;
    M[3][3] = 1.0f;
}

static void identityMat4(float M[4][4]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            M[c][r] = (c == r) ? 1.0f : 0.0f;
}

static void testRoundTrip() {
    std::printf("[1] no-scale round-trip\n");
    const float quats[5][4] = {
        {1, 0, 0, 0},                       // identity
        {0.7071f, 0, 0, 0.7071f},           // 90 deg about Z
        {0.7071f, 0.7071f, 0, 0},           // 90 deg about X
        {0.9659f, 0.0691f, 0.1383f, 0.2074f}, // 30 deg about (1,2,3)
        {0.0f, 0.7071f, 0.7071f, 0.0f},     // 180 deg about (1,1,0)
    };
    const float trans[3][3] = {{0, 0, 0}, {1, -2, 3}, {0.5f, 0.25f, -1}};
    const float pts[5][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {-2, 3, -1}};
    const float ID[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    for (auto& q : quats)
        for (auto& t : trans) {
            float M[4][4];
            rotTransMat4(M, q[0], q[1], q[2], q[3], t[0], t[1], t[2]);
            DualQuat dq;
            dq::mat4ToDquat(&dq, ID, M);
            check(dq.scale_weight == 0.0f, "no-scale matrix should have scale_weight==0");

            float R[3][3];
            for (auto& p0 : pts) {
                float ref[3] = {p0[0], p0[1], p0[2]};
                float got[3] = {p0[0], p0[1], p0[2]};
                applyMat4(M, ref);
                dq::mulV3m3Dq(got, R, &dq);
                const float err = vdist3(ref, got);
                check(err < 1e-4f, "round-trip point", err);
            }
        }
}

static void testBindPoseIdentity() {
    std::printf("[2] bind-pose identity blend\n");
    // 4 identity DQs with arbitrary (uneven) weights -> blended DQ must be the identity transform
    float wts[4] = {0.25f, 0.5f, 0.1f, 0.15f};
    float pivot[3] = {1.0f, 2.0f, 3.0f};
    DualQuat sum{}; // all-zero accumulator
    for (int i = 0; i < 4; ++i) {
        DualQuat id; // default DualQuat = identity quat, zero trans/scale
        id.scale_weight = 0.0f;
        dq::addWeightedDqDqPivot(&sum, &id, pivot, wts[i], true);
    }
    float W = 0.0f;
    for (auto w : wts)
        W += w;
    dq::normalizeDq(&sum, W);

    const float pts[5][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {-2, 3, -1}};
    for (auto& p0 : pts) {
        float got[3] = {p0[0], p0[1], p0[2]};
        float R[3][3];
        dq::mulV3m3Dq(got, R, &sum);
        const float err = vdist3(got, p0);
        check(err < 1e-4f, "identity blend leaves point fixed", err);
    }
}

static void testScalePresence() {
    std::printf("[3] scale presence\n");
    const float ID[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    // scaled matrix: rotation (identity) with per-axis scale (2,3,4)
    float M[4][4];
    identityMat4(M);
    M[0][0] = 2.0f;
    M[1][1] = 3.0f;
    M[2][2] = 4.0f;
    M[3][0] = 0.5f;
    M[3][1] = -1.0f;
    M[3][2] = 2.0f;
    DualQuat dq;
    dq::mat4ToDquat(&dq, ID, M);
    check(dq.scale_weight == 1.0f, "scaled matrix should have scale_weight==1");
    // the model-space scale (identity basemat) should recover the per-axis scale on the diagonal
    check(std::fabs(dq.scale[0][0] - 2.0f) < 1e-3f, "scale x", std::fabs(dq.scale[0][0] - 2.0f));
    check(std::fabs(dq.scale[1][1] - 3.0f) < 1e-3f, "scale y", std::fabs(dq.scale[1][1] - 3.0f));
    check(std::fabs(dq.scale[2][2] - 4.0f) < 1e-3f, "scale z", std::fabs(dq.scale[2][2] - 4.0f));
}

static float m4dist(const float a[4][4], const float b[4][4]) {
    float d = 0.0f;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float e = a[c][r] - b[c][r];
            d = std::fmax(d, std::fabs(e));
        }
    return d;
}

static void testBlenderFixed() {
    std::printf("[4] blender fixed-value cases\n");
    struct Case {
        float mat[4][4];
        float basemat[4][4];
        float expScale[4][4];
        float quat[4];
        float trans[4];
    };
    // float4x4 4-arg ctor = columns; mat = transpose(inner) -> the values below are m[col][row].
    const Case cases[2] = {
        {
            {{-2.14123, -1.28264, 3.27343, 0.0f}, {-0.478481f, 2.87361f, 0.812993f, 0.0f},
             {-1.38296f, 0.0230992f, -0.895575f, 0.0f}, {-2.26029f, 12.8871f, -13.5216f, 1.0f}},
            {{0.0988318f, 0.16104f, 0.981987f, 0.0f}, {0.91328f, -0.406549f, -0.0252451f, 0.0f},
             {0.39516f, 0.899324f, -0.187255f, 0.0f}, {7.73971f, 22.8871f, -3.52155f, 1.0f}},
            {{4.08974f, -0.445021f, 0.146173f, 0.0f}, {0.306437f, 2.97151f, 0.473002f, 0.0f},
             {-0.0853435f, -0.250095f, 1.62645f, 0.0f}, {-31.2277f, -42.5586f, -9.75092f, 1.0f}},
            {0.502368f, 0.0543716f, -0.854483f, -0.120535f},
            {22.674f, -0.878616f, 11.2762f, 14.167f},
        },
        {
            {{-0.0806635f, -1.04583f, -0.123402f, 0.0f}, {-1.60529f, -0.150756f, 2.32698f, 0.0f},
             {2.44763f, -0.385074f, 1.66357f, 0.0f}, {26.823f, -22.2225f, 5.397f, 1.0f}},
            {{0.0603774f, -0.271734f, -0.960477f, 0.0f}, {0.904674f, 0.421514f, -0.0623834f, 0.0f},
             {0.421806f, -0.865151f, 0.27128f, 0.0f}, {36.823f, -12.2225f, 15.397f, 1.0f}},
            {{0.248852f, 0.971507f, -0.331274f, 0.0f}, {2.66363f, -0.382422f, 0.8794f, 0.0f},
             {-0.726295f, 1.09917f, 2.67787f, 0.0f}, {71.3985f, -69.5943f, -2.88715f, 1.0f}},
            {0.149898f, -0.319339f, -0.0441496f, -0.934668f},
            {-2.20019f, 39.6236f, 49.052f, -16.2077f},
        },
    };

    for (int c = 0; c < 2; ++c) {
        DualQuat dq;
        dq::mat4ToDquat(&dq, cases[c].basemat, cases[c].mat);
        check(dq.scale_weight == 1.0f, "scale_weight==1");
        for (int i = 0; i < 4; ++i)
            check(std::fabs(dq.quat[i] - cases[c].quat[i]) < 1e-4f, "quat",
                  std::fabs(dq.quat[i] - cases[c].quat[i]));
        for (int i = 0; i < 4; ++i)
            check(std::fabs(dq.trans[i] - cases[c].trans[i]) < 1e-4f, "trans",
                  std::fabs(dq.trans[i] - cases[c].trans[i]));
        check(m4dist(dq.scale, cases[c].expScale) < 1e-4f, "scale mat",
              m4dist(dq.scale, cases[c].expScale));
    }
}

// [5] rotation direction: the DQ of a rotation must rotate in the CORRECT direction, never the
// transpose/inverse. Regression for the GPU kernel bug where the GLSL mat3 literal (built from the
// CPU's row-ordered values) silently became M^T, so the skinned mesh turned the opposite way to
// the joint. The round-trip test above can't catch a direction flip that is self-consistent, so
// this pins the absolute orientation: 90 deg about Z must send x->y (not x->-y).
static void testRotationDirection() {
    std::printf("[5] rotation direction\n");
    const float ID[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    struct Case {
        float q[4];      // {w,x,y,z}
        const char* from;
        float p[3];
        float e[3];
    };
    const Case cases[4] = {
        {{0.7071f, 0, 0, 0.7071f}, "Z90 x->y", {1, 0, 0}, {0, 1, 0}},
        {{0.7071f, 0, 0, 0.7071f}, "Z90 y->-x", {0, 1, 0}, {-1, 0, 0}},
        {{0.7071f, 0.7071f, 0, 0}, "X90 y->z", {0, 1, 0}, {0, 0, 1}},
        {{0.7071f, 0, 0.7071f, 0}, "Y90 z->x", {0, 0, 1}, {1, 0, 0}},
    };
    for (auto& c : cases) {
        float M[4][4];
        rotTransMat4(M, c.q[0], c.q[1], c.q[2], c.q[3], 0, 0, 0);
        DualQuat dq;
        dq::mat4ToDquat(&dq, ID, M);
        float p[3] = {c.p[0], c.p[1], c.p[2]};
        float R[3][3];
        dq::mulV3m3Dq(p, R, &dq);
        check(vdist3(p, c.e) < 1e-4f, c.from, vdist3(p, c.e));
    }
}

int main() {
    testRoundTrip();
    testBindPoseIdentity();
    testScalePresence();
    testBlenderFixed();
    testRotationDirection();

    std::printf("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0)
        std::printf("PASS\n");
    return g_fail == 0 ? 0 : 1;
}
