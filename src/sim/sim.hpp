// sim.hpp
// The soft-body DATA shared between the CPU (scene setup) and the GPU (the solver).
// Deliberately decoupled from the renderer (Vulkan) and the rigid-body physics (Jolt): it
// operates only on plain vertices + constraints.
//
//   - SoftBody       : a body's initial state (n vertices + two-point distance
//                      constraints). The bodies are stepped ON THE GPU
//                      (shaders/softbody.comp) — this type only seeds that GPU sim.
//   - makeCloth/Ball : build a body's vertices + distance constraints (a grid / a sphere).
//
// The same SoftBody type governs every soft body in the scene (cloth, ball, ...). A body is
// just (n vertices, a set of distance constraints).

#pragma once
#include "math.hpp"
#include <cmath>
#include <vector>

namespace sim {

// The scene gravity (m/s^2, along -Y). The GPU integrator uses this.
inline constexpr float kGravity = -9.81f;

// The Verlet sub-steps per frame (all soft bodies). The GPU integrator (softsim) uses this.
inline constexpr int kSubsteps = 3;

// A two-point distance constraint: keep vertices a..b at rest length `rest`.
// `k` (0..1) scales how hard the constraint is enforced per relaxation (1 = stiff).
struct Constraint {
    int a, b;
    float rest;
    float k;
};

// A triangle (for rendering).
struct Triangle {
    int a, b, c;
};

// A soft body's initial state: n vertices (position + normal + color, 3 floats each) + the
// distance constraints. The (pos, nrm, col) triple is exactly the render vertex (the sim's
// Vtx struct in softbody.comp) — the sim seeds the GPU with it, steps .pos/.nrm, and the
// renderer reads the whole thing to draw. .col is static (set here, never touched).
struct SoftBody {
    int n = 0;
    std::vector<float> pos; // 3n
    std::vector<float> nrm; // 3n
    std::vector<float> col; // 3n
    std::vector<Constraint> cons;

    // Build the body from initial position/normal/color lists (3 floats each) + constraints.
    void init(const float* p0, const float* n0, const float* c0, int n, std::vector<Constraint> cons) {
        this->n = n;
        pos.assign(p0, p0 + (size_t)3 * n);
        nrm.assign(n0, n0 + (size_t)3 * n);
        col.assign(c0, c0 + (size_t)3 * n);
        this->cons = std::move(cons);
    }

    int numVertices() const { return n; }
    float* posPtr(int i) { return &pos[3 * (size_t)i]; }
    const float* posPtr(int i) const { return &pos[3 * (size_t)i]; }
    const float* nrmPtr(int i) const { return &nrm[3 * (size_t)i]; }
    const float* colPtr(int i) const { return &col[3 * (size_t)i]; }
};

// Build a CW x CH grid of vertices (pos/nrm/col, 3 floats each) + the distance constraints
// (the grid edges + diagonals) at their tension = 1.0 rest lengths. The sheet faces +Y (the
// normals are up); the color is the cloth's albedo.
inline void makeCloth(std::vector<float>& verts, std::vector<float>& nrm, std::vector<float>& col,
                      std::vector<Constraint>& cons, int CW, int CH, float span, float y0) {
    const int CN = CW * CH;
    verts.assign((size_t)3 * CN, 0.0f);
    nrm.assign((size_t)3 * CN, 0.0f);
    col.assign((size_t)3 * CN, 0.0f);
    for (int gy = 0; gy < CH; ++gy)
        for (int gx = 0; gx < CW; ++gx) {
            int i = gy * CW + gx;
            verts[3 * i + 0] = -span / 2 + span * (float)gx / (float)(CW - 1);
            verts[3 * i + 1] = y0;
            verts[3 * i + 2] = -span / 2 + span * (float)gy / (float)(CH - 1);
            nrm[3 * i + 1] = 1.0f;  // flat sheet, faces up
            col[3 * i + 0] = 0.25f; // the cloth's albedo (blue)
            col[3 * i + 1] = 0.45f;
            col[3 * i + 2] = 0.78f;
        }
    const float spacing = span / (float)(CW - 1);
    const float diag = spacing * kSqrt2;
    cons.clear();
    for (int i = 0; i < CN; ++i) {
        int gx = i % CW, gy = i / CW;
        if (gx < CW - 1)
            cons.push_back({i, i + 1, spacing, 1.0f});
        if (gy < CH - 1)
            cons.push_back({i, i + CW, spacing, 1.0f});
        if (gx < CW - 1 && gy < CH - 1)
            cons.push_back({i, i + CW + 1, diag, 1.0f});
        if (gx > 0 && gy < CH - 1)
            cons.push_back({i, i + CW - 1, diag, 1.0f});
    }
}

// Build a UV-sphere: the vertex list (pos/nrm/col, 3 floats/vert each), the distance
// constraints (the sphere's edges, soft k=0.5), and the triangles (for the render). The
// normals are the rest-state sphere surface directions (the vertex position / radius). The
// two poles are single vertices; the interior is (lat-1) rings of `lon` vertices each.
inline void makeBall(std::vector<float>& verts, std::vector<float>& nrm, std::vector<float>& col,
                     std::vector<Constraint>& cons, std::vector<Triangle>& tris, int lat, int lon, float radius,
                     float y0) {
    auto vid = [lat, lon](int r, int c) {
        if (r == 0)
            return 0;
        if (r == lat)
            return 1 + (lat - 1) * lon;
        return 1 + (r - 1) * lon + (c % lon);
    };
    auto vpos = [lat, lon, radius](int r, int c, float& x, float& y, float& z) {
        if (r == 0) {
            x = 0;
            y = radius;
            z = 0;
            return;
        }
        if (r == lat) {
            x = 0;
            y = -radius;
            z = 0;
            return;
        }
        float phi = kPi * (float)r / (float)lat, theta = 2.0f * kPi * (float)c / (float)lon;
        x = radius * std::sin(phi) * std::cos(theta);
        y = radius * std::cos(phi);
        z = radius * std::sin(phi) * std::sin(theta);
    };
    const int BN = 2 + (lat - 1) * lon;
    verts.assign((size_t)3 * BN, 0.0f);
    nrm.assign((size_t)3 * BN, 0.0f);
    col.assign((size_t)3 * BN, 0.0f);
    for (int r = 0; r <= lat; ++r)
        for (int c = 0; c < lon; ++c) {
            int i = vid(r, c);
            float x, y, z;
            vpos(r, c, x, y, z); // the un-offset point, at distance `radius` from the center
            verts[3 * i + 0] = x;
            verts[3 * i + 1] = y0 + y;
            verts[3 * i + 2] = z;
            nrm[3 * i + 0] = x / radius; // rest-state surface normal
            nrm[3 * i + 1] = y / radius;
            nrm[3 * i + 2] = z / radius;
            col[3 * i + 0] = 0.85f; // the ball's albedo (orange)
            col[3 * i + 1] = 0.35f;
            col[3 * i + 2] = 0.25f;
        }
    auto bdist = [&](int r1, int c1, int r2, int c2) {
        float ax, ay, az, bx, by, bz;
        vpos(r1, c1, ax, ay, az);
        vpos(r2, c2, bx, by, bz);
        float dx = ax - bx, dy = ay - by, dz = az - bz;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    cons.clear();
    tris.clear();
    for (int r = 0; r < lat; ++r)
        for (int c = 0; c < lon; ++c) {
            int a = vid(r, c), b = vid(r, (c + 1) % lon), c2 = vid(r + 1, c), d = vid(r + 1, (c + 1) % lon);
            cons.push_back({a, b, bdist(r, c, r, (c + 1) % lon), 0.5f});
            cons.push_back({a, c2, bdist(r, c, r + 1, c), 0.5f});
            cons.push_back({a, d, bdist(r, c, r + 1, (c + 1) % lon), 0.5f});
            tris.push_back({a, b, d});
            tris.push_back({a, d, c2});
        }
}

} // namespace sim
