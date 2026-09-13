#include "sim/sim.hpp"

#include "app/geometry.hpp"
#include "app/rigid.hpp"
#include "app/scene.hpp"
#include "math.hpp"
#include "sim/params.hpp"
#include "sim/phys.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {

struct CapInfo {
    V3 A, B, AB;
    float ab2, radius;
};

std::vector<CapInfo> prepareCaps(const std::vector<CapsuleGPU>& caps) {
    std::vector<CapInfo> out(caps.size());
    for (size_t c = 0; c < caps.size(); ++c) {
        const CapsuleGPU& cg = caps[c];
        float m[9];
        quatToMat3(cg.quat, m);
        V3 axis = vScale({m[3], m[4], m[5]}, cg.halfLen[0]);
        V3 center{cg.centerRadius[0], cg.centerRadius[1], cg.centerRadius[2]};
        CapInfo& ci = out[c];
        ci.A = vSub(center, axis);
        ci.B = vAdd(center, axis);
        ci.AB = vSub(ci.B, ci.A);
        ci.ab2 = vDot(ci.AB, ci.AB);
        ci.radius = cg.centerRadius[3];
    }
    return out;
}

struct Body {
    int n = 0;
    std::vector<V3> pos, prev, sub0;
    struct Contact {
        V3 n;
        float lambda;
    };
    std::vector<Contact> contact;
    std::vector<sim::Entry> ents;
    std::vector<int> estart, colorStart, colorVerts;
    int colorCount = 0;
    std::vector<V3> initPos;
    const std::vector<sim::Constraint>* cons = nullptr;

    void build(const sim::SoftBody& sb) {
        n = sb.n;
        pos.resize(n);
        prev.resize(n);
        sub0.resize(n);
        contact.resize(n);
        initPos.resize(n);
        for (int i = 0; i < n; ++i) {
            const size_t o = (size_t)12 * i;
            initPos[i] = {sb.vtx[o], sb.vtx[o + 1], sb.vtx[o + 2]};
            pos[i] = initPos[i];
            prev[i] = initPos[i];
            sub0[i] = initPos[i];
            contact[i] = Contact{{0.0f, 0.0f, 0.0f}, 0.0f};
        }
        ents = sb.entries;
        estart = sb.entryStart;
        colorStart = sb.colorStart;
        colorVerts = sb.colorVerts;
        colorCount = sb.colorCount;
        cons = &sb.cons;
    }

    void reset() {
        pos = initPos;
        prev = initPos;
        sub0 = initPos;
        resetContacts();
    }

    void resetContacts() {
        for (int i = 0; i < n; ++i)
            contact[i] = Contact{{0.0f, 0.0f, 0.0f}, 0.0f};
    }

    void verlet(const PhysParams& ph) {
        for (int i = 0; i < n; ++i) {
            V3 P = pos[i], Pr = prev[i];
            V3 d = vScale(vSub(P, Pr), ph.damping);
            d.y += ph.gravity * ph.dt * ph.dt;
            float dl = vLen(d);
            if (dl > ph.maxStep)
                d = vScale(d, ph.maxStep / dl);
            pos[i] = vAdd(P, d);
        }
    }

    V3 collideVertex(const PhysParams& ph, int i, V3 P, const std::vector<CapInfo>& caps) {
        V3 cN = contact[i].n;
        float cL = contact[i].lambda;
        for (int c = 0; c < ph.nCaps; ++c) {
            const CapInfo& ci = caps[c];
            float t = std::clamp(vDot(vSub(P, ci.A), ci.AB) / std::max(ci.ab2, 1e-6f), 0.0f, 1.0f);
            V3 Q = vAdd(ci.A, vScale(ci.AB, t));
            V3 d = vSub(P, Q);
            float dist = vLen(d);
            float C = dist - (ci.radius + ph.skin);
            if (C < 0.0f) {
                V3 nrm = dist > 1e-5f ? vScale(d, 1.0f / dist) : V3{0, 1, 0};
                float corr = -C / (1.0f + kCollideCompliance);
                P = vAdd(P, vScale(nrm, corr));
                cN = vAdd(cN, vScale(nrm, corr));
                cL += corr;
            }
        }
        if (P.y < 0.0f) {
            float corr = -P.y / (1.0f + kCollideCompliance);
            P.y += corr;
            cN.y += corr;
            cL += corr;
        }
        float nl = vLen(cN);
        if (nl > 1e-8f) {
            V3 fn = vScale(cN, 1.0f / nl);
            V3 rel = vSub(P, sub0[i]);
            V3 vt = vSub(rel, vScale(fn, vDot(rel, fn)));
            float l = vLen(vt);
            float limit = ph.friction * cL;
            if (l > 1e-8f && limit > 0.0f) {
                float corr = std::min(l, limit) / (1.0f + kFrictionCompliance);
                P = vSub(P, vScale(vt, corr / l));
            }
        }
        contact[i] = Contact{cN, cL};
        return P;
    }

    void relaxVertex(const PhysParams& ph, int i) {
        V3 P = pos[i];
        float alpha = constraintAlpha(ph);
        for (int e = estart[i]; e < estart[i + 1]; ++e) {
            const sim::Entry& en = ents[e];
            V3 d = vSub(pos[en.j], P);
            float dist = vLen(d);
            if (dist < 1e-6f)
                continue;
            float rest = en.rest * ph.tension;
            P = vAdd(P, vScale(d, (dist - rest) / dist * (en.k / (2.0f + alpha))));
        }
        pos[i] = P;
    }

    void solveVertex(const PhysParams& ph, int i, const std::vector<CapInfo>& caps) {
        pos[i] = collideVertex(ph, i, pos[i], caps);
        relaxVertex(ph, i);
    }

    void collideAll(const PhysParams& ph, const std::vector<CapInfo>& caps) {
        for (int i = 0; i < n; ++i)
            pos[i] = collideVertex(ph, i, pos[i], caps);
    }

    void finalize(const PhysParams& ph) {
        for (int i = 0; i < n; ++i) {
            V3 P = pos[i];
            V3 v = vScale(vSub(P, sub0[i]), 1.0f / ph.dt);
            float nl = vLen(contact[i].n);
            if (nl > 1e-8f) {
                V3 fn = vScale(contact[i].n, 1.0f / nl);
                float vn = vDot(v, fn);
                if (vn < 0.0f)
                    v = vSub(v, vScale(fn, vn));
            }
            prev[i] = vSub(P, vScale(v, ph.dt));
        }
    }

    void step(const PhysParams& ph, int iters, const std::vector<CapInfo>& caps, int pinned) {
        if (pinned)
            return;
        for (int s = 0; s < sim::kSubsteps; ++s) {
            for (int i = 0; i < n; ++i)
                sub0[i] = pos[i];
            resetContacts();
            verlet(ph);
            for (int it = 0; it < iters; ++it)
                for (int c = 0; c < colorCount; ++c)
                    for (int idx = colorStart[c]; idx < colorStart[c + 1]; ++idx)
                        solveVertex(ph, colorVerts[idx], caps);
            collideAll(ph, caps);
            finalize(ph);
        }
    }
};

struct Metrics {
    float maxV = 0, ke = 0, stretch = 0;
    V3 center{0, 0, 0}, ext{0, 0, 0};
    V3 com{0, 0, 0};
    float maxY = 0;
};

Metrics metrics(const Body& b, std::vector<V3>& prevFramePos, float dt) {
    Metrics m;
    const V3* p = b.pos.data();
    V3 mn{1e9f, 1e9f, 1e9f}, mx{-1e9f, -1e9f, -1e9f};
    V3 com{0, 0, 0};
    float ke2 = 0;
    for (int i = 0; i < b.n; ++i) {
        V3 d = vSub(p[i], prevFramePos[i]);
        float v = vLen(d) / dt;
        m.maxV = std::max(m.maxV, v);
        ke2 += v * v;
        com = vAdd(com, p[i]);
        mn.x = std::min(mn.x, p[i].x);
        mx.x = std::max(mx.x, p[i].x);
        mn.y = std::min(mn.y, p[i].y);
        mx.y = std::max(mx.y, p[i].y);
        mn.z = std::min(mn.z, p[i].z);
        mx.z = std::max(mx.z, p[i].z);
        m.maxY = std::max(m.maxY, p[i].y);
        prevFramePos[i] = p[i];
    }
    m.ke = 0.5f * ke2 / (float)b.n;
    m.center = vScale(vAdd(mn, mx), 0.5f);
    m.ext = vSub(mx, mn);
    m.com = vScale(com, 1.0f / (float)b.n);
    float st = 0;
    const auto& cons = *b.cons;
    for (const auto& c : cons) {
        float dist = vLen(vSub(p[c.b], p[c.a]));
        st += std::fabs(dist - c.rest) / c.rest;
    }
    m.stretch = (float)std::fabs((double)(st / (float)cons.size()));
    return m;
}

} // namespace

int main(int argc, char** argv) {
    SimParams ui;
    const int FRAMES = (argc > 1) ? (int)std::strtol(argv[1], nullptr, 10) : 240;
    const float FRICTION = (argc > 2) ? std::strtof(argv[2], nullptr) : ui.friction;
    const bool NOCOLS = (argc > 3) && std::strtol(argv[3], nullptr, 10) != 0;
    const bool BEND = (argc > 5) && std::strtol(argv[5], nullptr, 10) != 0;
    const bool SINGLE = (argc > 6) && std::strtol(argv[6], nullptr, 10) != 0;
    const float ZOFF = (argc > 7) ? std::strtof(argv[7], nullptr) : 0.0f;
    const float STIFF = (argc > 8) ? std::strtof(argv[8], nullptr) : ui.stiffness;
    const int CW = Scene::kCW, CH = Scene::kCH, holdFrames = Scene::kHoldFrames;
    const float kDefaultSpan = SINGLE ? 3.0f : Scene::kClothSpan;
    const float kClothSpan = (argc > 4) ? std::strtof(argv[4], nullptr) : kDefaultSpan;
    const float kClothY0 = SINGLE ? Scene::kSingleY0 : Scene::kClothY0;
    const int kBallLat = Scene::kBallLat, kBallLon = Scene::kBallLon;
    const float kBallRadius = Scene::kBallRadius, kBallY0 = Scene::kBallY0;

    RigidScene rigid;
    if (!SINGLE)
        rigid.init();

    SoftMesh clothMesh = makeCloth(CW, CH, kClothSpan, kClothY0);
    if (ZOFF != 0.0f)
        for (size_t i = 0; i < clothMesh.mesh.vtx.size(); i += 12)
            clothMesh.mesh.vtx[i + 2] += ZOFF;
    if (BEND) {
        const float sp = kClothSpan / (CW - 1);
        for (int gy = 0; gy < CH; ++gy)
            for (int gx = 0; gx < CW; ++gx) {
                int i = gy * CW + gx;
                if (gx < CW - 2)
                    clothMesh.cons.push_back({i, i + 2, 2.0f * sp, 0.5f});
                if (gy < CH - 2)
                    clothMesh.cons.push_back({i, i + 2 * CW, 2.0f * sp, 0.5f});
            }
    }
    sim::SoftBody cloth;
    cloth.init(clothMesh.mesh, clothMesh.cons);
    SoftMesh ballMesh = makeBall(kBallLat, kBallLon, kBallRadius, kBallY0);
    sim::SoftBody ball;
    ball.init(ballMesh.mesh, ballMesh.cons);

    Body cb, bb;
    cb.build(cloth);
    bb.build(ball);

    PhysParams ph;
    ph.dt = kFrameDt / sim::kSubsteps;
    ph.damping = ui.damping;
    ph.gravity = sim::kGravity * ui.mass;
    ph.friction = FRICTION;
    ph.skin = kSkin;
    ph.tension = ui.tension;
    ph.stiff = STIFF;
    ph.maxStep = kSoftMaxStep;

    std::vector<V3> prevCloth((size_t)CW * CH), prevBall(ball.n);
    for (int i = 0; i < cb.n; ++i)
        prevCloth[i] = cb.pos[i];
    for (int i = 0; i < bb.n; ++i)
        prevBall[i] = bb.pos[i];

    Metrics clothFinal, ballFinal;
    float clothPeakV = 0, clothPeakKe = 0, clothPeakStretch = 0;
    float ballPeakV = 0, ballPeakKe = 0;
    int capCount = 0;
    auto trackPeak = [](float& peakValue, float value) {
        if (!std::isfinite(value))
            peakValue = std::numeric_limits<float>::quiet_NaN();
        else if (value > peakValue)
            peakValue = value;
    };
    for (int f = 0; f < FRAMES; ++f) {
        std::vector<CapsuleGPU> caps;
        if (SINGLE) {
            caps.push_back(CapsuleGPU{
                {0.0f, 1.5f, 0.0f, kCapsuleRadius}, {0.0f, 0.0f, 0.0f, 1.0f}, {kCapsuleHalfLen, 0.0f, 0.0f, 0.0f}});
        } else {
            rigid.step(1);
            if (!NOCOLS)
                caps = rigid.capsuleGPU();
        }
        auto ccaps = prepareCaps(caps);
        ph.nCaps = (int)caps.size();
        cb.step(ph, ui.passes, ccaps, f < holdFrames);
        bb.step(ph, ui.passes, ccaps, 0);
        Metrics cm = metrics(cb, prevCloth, kFrameDt);
        Metrics bm = metrics(bb, prevBall, kFrameDt);
        clothFinal = cm;
        ballFinal = bm;
        capCount = (int)ccaps.size();
        trackPeak(clothPeakV, cm.maxV);
        trackPeak(clothPeakKe, cm.ke);
        trackPeak(clothPeakStretch, cm.stretch);
        trackPeak(ballPeakV, bm.maxV);
        trackPeak(ballPeakKe, bm.ke);
    }
    std::printf("fr=%.2f stiff=%.2f span=%.1f single=%d frames=%d caps=%d cloth final maxV=%.3f ke=%.4f stretch=%.4f "
                "maxY=%.2f com=%.2f,%.2f,%.2f peakV=%.3f peakKe=%.4f peakStretch=%.4f ball final maxV=%.3f ke=%.4f "
                "peakV=%.3f peakKe=%.4f\n",
                FRICTION, STIFF, kClothSpan, SINGLE ? 1 : 0, FRAMES, capCount, clothFinal.maxV, clothFinal.ke,
                clothFinal.stretch, clothFinal.maxY, clothFinal.com.x, clothFinal.com.y, clothFinal.com.z, clothPeakV,
                clothPeakKe, clothPeakStretch, ballFinal.maxV, ballFinal.ke, ballPeakV, ballPeakKe);
    return 0;
}
