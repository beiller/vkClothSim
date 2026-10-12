-- standard geometry library (engine loads this for every scene, global `mesh`)
--
-- geometry table: { pos = {x,y,z,...}, nrm = {x,y,z,...}, uv = {u,v,...}, idx = {0,1,2,...} }
--   (nrm/uv optional, vertex indices are 1-based Lua)
-- constraints: flat array { a, b, rest, k, ... } (1-based vertex indices)

local m = {}

local function norm3(v)
    local l = math.sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3])
    return { v[1] / l, v[2] / l, v[3] / l }
end

-- surface-of-revolution capsule (radius x halfLen cylinder, hemispherical caps)
function m.capsule(p)
    local r, h = p.radius, p.halfLen
    local S, M = p.S or 20, p.M or 32
    local pos, nrm, uv, idx = {}, {}, {}, {}
    local top, bot, twoPi = h + r, -h - r, 2 * math.pi
    for i = 0, M do
        local y = top - i / M * (top - bot)
        local dy = 0.0
        if y >= h then
            dy = y - h
        elseif y <= -h then
            dy = y + h
        end
        local cr, ny, pole
        if dy == 0.0 then
            cr, ny, pole = r, 0.0, false
        else
            cr = math.sqrt(math.max(0, r * r - dy * dy))
            pole = cr <= 1e-5
            ny = pole and ((dy > 0) and 1 or -1) or dy / cr
        end
        for j = 0, S - 1 do
            local phi = j / S * twoPi
            local cp, sp = math.cos(phi), math.sin(phi)
            local v = i * S + j + 1
            pos[3 * v - 2], pos[3 * v - 1], pos[3 * v] = cr * cp, y, cr * sp
            if pole then -- degenerate ring: all segments share the vertical normal
                nrm[3 * v - 2], nrm[3 * v - 1], nrm[3 * v] = 0, ny, 0
            else
                nrm[3 * v - 2], nrm[3 * v - 1], nrm[3 * v] = cp, ny, sp
            end
            uv[2 * v - 1], uv[2 * v] = j / S, i / M
        end
    end
    for i = 0, M - 1 do
        for j = 0, S - 1 do
            local a = i * S + j + 1
            local b = i * S + ((j + 1) % S) + 1
            local c = (i + 1) * S + j + 1
            local d = (i + 1) * S + ((j + 1) % S) + 1
            local n = #idx
            idx[n + 1], idx[n + 2], idx[n + 3] = a, b, d
            idx[n + 4], idx[n + 5], idx[n + 6] = a, d, c
        end
    end
    return { pos = pos, nrm = nrm, uv = uv, idx = idx }
end

-- icosphere (subdivided icosahedron), centered at p.center
local function icoSphere(subdiv)
    local t = (1 + math.sqrt(5)) * 0.5
    local verts = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
                    { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } }
    for i = 1, #verts do
        verts[i] = norm3(verts[i])
    end
    local tris = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2,
                   10, 7, 6, 7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11,
                   6, 2, 10, 8, 6, 7, 9, 8, 1 }
    for _ = 1, subdiv do
        local mid = {}
        local function midOf(a, b)
            local key = (a < b) and (a * 1000000 + b) or (b * 1000000 + a)
            local hit = mid[key]
            if hit then
                return hit
            end
            local va, vb = verts[a + 1], verts[b + 1]
            local idx = #verts
            verts[idx + 1] = norm3({ (va[1] + vb[1]) * 0.5, (va[2] + vb[2]) * 0.5, (va[3] + vb[3]) * 0.5 })
            mid[key] = idx
            return idx
        end
        local next = {}
        local n = 0
        for f = 1, #tris, 3 do
            local a, b, c = tris[f], tris[f + 1], tris[f + 2]
            local ab, bc, ca = midOf(a, b), midOf(b, c), midOf(c, a)
            for _, v in ipairs({ a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca }) do
                n = n + 1
                next[n] = v
            end
        end
        tris = next
    end
    return verts, tris
end

function m.sphere(p)
    local subdiv = p.subdiv or 4
    local r = p.radius or 1.0
    local c = p.center or { 0, 0, 0 }
    local verts, tris = icoSphere(subdiv)
    local n = #verts
    local pos, nrm, uv, idx = {}, {}, {}, {}
    for i = 1, n do
        local d = verts[i]
        pos[3 * i - 2], pos[3 * i - 1], pos[3 * i] = c[1] + d[1] * r, c[2] + d[2] * r, c[3] + d[3] * r
        nrm[3 * i - 2], nrm[3 * i - 1], nrm[3 * i] = d[1], d[2], d[3]
        uv[2 * i - 1] = 0.5 + math.atan(d[3], d[1]) / (2 * math.pi)
        uv[2 * i] = 0.5 + math.asin(d[2]) / math.pi
    end
    for i = 1, #tris do
        idx[i] = tris[i] + 1
    end
    return { pos = pos, nrm = nrm, uv = uv, idx = idx }
end

-- flat quad at y=0, spanning +/- size
function m.ground(p)
    local s = p.size or 40.0
    local corners = { { -s, -s }, { s, -s }, { s, s }, { -s, s } }
    local pos, nrm, uv, idx = {}, {}, {}, { 1, 2, 3, 1, 3, 4 }
    for i = 1, 4 do
        local x, z = corners[i][1], corners[i][2]
        pos[3 * i - 2], pos[3 * i - 1], pos[3 * i] = x, 0, z
        nrm[3 * i - 2], nrm[3 * i - 1], nrm[3 * i] = 0, 1, 0
        uv[2 * i - 1] = (x + s) / (2 * s)
        uv[2 * i] = (z + s) / (2 * s)
    end
    return { pos = pos, nrm = nrm, uv = uv, idx = idx }
end

-- cw x ch vertex grid in the xz plane at y=y0, spanning +/- span/2
function m.cloth(p)
    local cw, ch = p.cw, p.ch
    local span = p.span
    local y0 = p.y0 or 0
    local pos, nrm, uv, idx = {}, {}, {}, {}
    for gy = 0, ch - 1 do
        for gx = 0, cw - 1 do
            local v = gy * cw + gx + 1
            pos[3 * v - 2] = -span / 2 + span * gx / (cw - 1)
            pos[3 * v - 1] = y0
            pos[3 * v] = -span / 2 + span * gy / (ch - 1)
            nrm[3 * v - 2], nrm[3 * v - 1], nrm[3 * v] = 0, 1, 0
            uv[2 * v - 1], uv[2 * v] = gx / (cw - 1), gy / (ch - 1)
        end
    end
    for gy = 0, ch - 2 do
        for gx = 0, cw - 2 do
            local a = gy * cw + gx + 1
            local n = #idx
            idx[n + 1], idx[n + 2], idx[n + 3] = a, a + cw, a + 1
            idx[n + 4], idx[n + 5], idx[n + 6] = a + 1, a + cw, a + cw + 1
        end
    end
    return { pos = pos, nrm = nrm, uv = uv, idx = idx }
end

-- structural + shear springs for the cloth grid
function m.clothCons(p)
    local cw, ch = p.cw, p.ch
    local spacing = p.span / (cw - 1)
    local diag = spacing * math.sqrt(2)
    local cons = {}
    local n = 0
    local function add(a, b, rest, k)
        n = n + 1
        local e = 4 * n - 3
        cons[e], cons[e + 1], cons[e + 2], cons[e + 3] = a + 1, b + 1, rest, k
    end
    for i = 0, cw * ch - 1 do
        local gx, gy = i % cw, math.floor(i / cw)
        if gx < cw - 1 then
            add(i, i + 1, spacing, 1.0)
        end
        if gy < ch - 1 then
            add(i, i + cw, spacing, 1.0)
        end
        if gx < cw - 1 and gy < ch - 1 then
            add(i, i + cw + 1, diag, 1.0)
        end
        if gx > 0 and gy < ch - 1 then
            add(i, i + cw - 1, diag, 1.0)
        end
    end
    return cons
end

-- edge springs + antipodal ties that hold a sphere's shape against gravity
function m.ballCons(p)
    local pos, idx = p.pos, p.idx
    local n = #pos / 3
    local function vAt(i)
        return pos[3 * i - 2], pos[3 * i - 1], pos[3 * i]
    end
    local function dist(a, b)
        local ax, ay, az = vAt(a)
        local bx, by, bz = vAt(b)
        return math.sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz))
    end
    local cons = {}
    local nc = 0
    local function add(a, b, rest, k)
        nc = nc + 1
        local e = 4 * nc - 3
        cons[e], cons[e + 1], cons[e + 2], cons[e + 3] = a, b, rest, k
    end
    local seen = {}
    for f = 1, #idx, 3 do
        local a, b, c = idx[f], idx[f + 1], idx[f + 2]
        for _, e in ipairs({ { a, b }, { b, c }, { c, a } }) do
            local x, y = e[1], e[2]
            local lo, hi = (x < y) and x or y, (x < y) and y or x
            local key = lo * 1000000 + hi
            if not seen[key] then
                seen[key] = true
                add(lo, hi, dist(lo, hi), 0.5)
            end
        end
    end
    local cx, cy, cz = 0, 0, 0
    for i = 1, n do
        local x, y, z = vAt(i)
        cx, cy, cz = cx + x, cy + y, cz + z
    end
    cx, cy, cz = cx / n, cy / n, cz / n
    local dir = {}
    for i = 1, n do
        local x, y, z = vAt(i)
        dir[i] = norm3({ x - cx, y - cy, z - cz })
    end
    for i = 1, n do
        local partner, bestDot = -1, 1e30
        for j = 1, n do
            if j ~= i then
                local di, dj = dir[i], dir[j]
                local d = di[1] * dj[1] + di[2] * dj[2] + di[3] * dj[3]
                if d < bestDot then
                    bestDot, partner = d, j
                end
            end
        end
        if partner > i then
            add(i, partner, dist(i, partner), 0.5)
        end
    end
    return cons
end

return m
