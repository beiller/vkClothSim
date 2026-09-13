#pragma once
#include "math.hpp"
#include "mesh.hpp"
#include <cmath>
#include <cstdint>
#include <vector>

namespace sim {

inline constexpr float kGravity = -9.81f;
inline constexpr int kSubsteps = 3;

inline void prefixSum(std::vector<int>& v) {
    for (size_t i = 1; i < v.size(); ++i)
        v[i] += v[i - 1];
}

struct Constraint {
    int a, b;
    float rest;
    float k;
};

struct Entry {
    int j;
    float rest;
    float k;
    int pad;
};

struct SoftBody {
    int n = 0;
    std::vector<float> pos0;
    std::vector<float> nrm0;
    std::vector<Constraint> cons;
    std::vector<Entry> entries;
    std::vector<int> entryStart;
    int colorCount = 0;
    std::vector<int> colorStart;
    std::vector<int> colorVerts;
    std::vector<int> triStart;
    std::vector<int> triList;

    void init(const Mesh& mesh, const std::vector<Constraint>& c) {
        n = mesh.vertexCount();
        pos0 = mesh.pos;
        nrm0 = mesh.nrm;
        cons = c;
        buildEntries();
        buildColoring();
        buildTriangles(mesh);
    }

private:
    void buildEntries() {
        entryStart.assign(n + 1, 0);
        for (const auto& c : cons) {
            ++entryStart[c.a + 1];
            ++entryStart[c.b + 1];
        }
        prefixSum(entryStart);
        entries.resize((size_t)entryStart[n]);
        std::vector<int> cur(n);
        for (int i = 0; i < n; ++i)
            cur[i] = entryStart[i];
        for (const auto& c : cons) {
            entries[cur[c.a]++] = {c.b, c.rest, c.k, 0};
            entries[cur[c.b]++] = {c.a, c.rest, c.k, 0};
        }
    }

    void buildColoring() {
        std::vector<std::vector<int>> adj(n);
        for (int i = 0; i < n; ++i)
            for (int e = entryStart[i]; e < entryStart[i + 1]; ++e)
                adj[i].push_back(entries[e].j);
        std::vector<int> vertexColor(n, -1);
        std::vector<std::vector<int>> groups;
        for (int i = 0; i < n; ++i) {
            bool used[64] = {};
            for (int nb : adj[i])
                if (vertexColor[nb] >= 0 && vertexColor[nb] < 64)
                    used[vertexColor[nb]] = true;
            size_t c = 0;
            while (c < 64 && used[c])
                ++c;
            if (c == groups.size())
                groups.resize(c + 1);
            vertexColor[i] = (int)c;
            groups[c].push_back(i);
        }
        colorCount = (int)groups.size();
        colorStart.assign(colorCount + 1, 0);
        for (int c = 0; c < colorCount; ++c)
            colorStart[c + 1] = colorStart[c] + (int)groups[c].size();
        colorVerts.resize(n);
        for (int c = 0; c < colorCount; ++c)
            for (size_t j = 0; j < groups[c].size(); ++j)
                colorVerts[colorStart[c] + (int)j] = groups[c][j];
    }

    void buildTriangles(const Mesh& mesh) {
        const auto& idx = mesh.indices;
        triStart.assign(n + 1, 0);
        for (uint32_t v : idx)
            ++triStart[(size_t)v + 1];
        prefixSum(triStart);
        triList.resize(idx.size());
        std::vector<int> cur(n);
        for (int i = 0; i < n; ++i)
            cur[i] = triStart[i];
        for (size_t f = 0; f < idx.size(); f += 3)
            for (int k = 0; k < 3; ++k) {
                const int v = (int)idx[f + (size_t)k];
                triList[cur[v]++] = (int)(f / 3);
            }
    }
};

} // namespace sim
