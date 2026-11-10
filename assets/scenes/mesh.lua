-- standard geometry library (engine loads this for every scene, global `mesh`)
--
-- geometry table: { pos = {x,y,z,...}, nrm = {x,y,z,...}, uv = {u,v,...}, idx = {0,1,2,...} }
--   (nrm/uv optional). The table is the Geometry component's data: vertex indices are
--   0-based, as in Vulkan, so `scene.replace(g, "Geometry", mesh.box({...}))` works as-is.
-- constraints: flat array { a, b, rest, k, ... } (0-based vertex indices)


local function norm3(v)
    local l = math.sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3])
    return { v[1] / l, v[2] / l, v[3] / l }
end

-- surface-of-revolution capsule (radius x halfLen cylinder, hemispherical caps)
function capsule(p)
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
            local v = i * S + j
            pos[3 * v + 1], pos[3 * v + 2], pos[3 * v + 3] = cr * cp, y, cr * sp
            if pole then -- degenerate ring: all segments share the vertical normal
                nrm[3 * v + 1], nrm[3 * v + 2], nrm[3 * v + 3] = 0, ny, 0
            else
                nrm[3 * v + 1], nrm[3 * v + 2], nrm[3 * v + 3] = cp, ny, sp
            end
            uv[2 * v + 1], uv[2 * v + 2] = j / S, i / M
        end
    end
    for i = 0, M - 1 do
        for j = 0, S - 1 do
            local a = i * S + j
            local b = i * S + ((j + 1) % S)
            local c = (i + 1) * S + j
            local d = (i + 1) * S + ((j + 1) % S)
            local n = #idx
            idx[n + 1], idx[n + 2], idx[n + 3] = a, b, d
            idx[n + 4], idx[n + 5], idx[n + 6] = a, d, c
        end
    end
    return { pos = pos, nrm = nrm, uv = uv, idx = idx }
end

-- surface-of-revolution tapered capsule: the radius ramps linearly from bottomRadius
-- (at y = -halfHeight) to topRadius (at y = +halfHeight), hemispherical cap at each end
function taperedCapsule(p)
    local h, rb, rt = p.halfHeight, p.bottomRadius, p.topRadius
    local S, M = p.S or 20, p.M or 32
    local pos, nrm, uv, idx = {}, {}, {}, {}
    local top, bot, twoPi = h + rt, -h - rb, 2 * math.pi
    for i = 0, M do
        local y = top - i / M * (top - bot)
        local dy, rc
        if y >= h then
            dy, rc = y - h, rt
        elseif y <= -h then
            dy, rc = y + h, rb
        else
            dy, rc = 0.0, h > 0 and (rt + (rb - rt) * (0.5 * (1 - y / h))) or 0.5 * (rt + rb)
        end
        local cr = dy == 0.0 and rc or math.sqrt(math.max(0, rc * rc - dy * dy))
        for j = 0, S - 1 do
            local phi = j / S * twoPi
            local cp, sp = math.cos(phi), math.sin(phi)
            local v = i * S + j
            pos[3 * v + 1], pos[3 * v + 2], pos[3 * v + 3] = cr * cp, y, cr * sp
            local nx, ny, nz
            if cr <= 1e-5 then -- degenerate ring: all segments share the vertical normal
                nx, nz = 0, 0
                ny = dy > 0 and 1 or (dy < 0 and -1 or (y >= 0 and 1 or -1))
            else
                local nl = math.sqrt(cr * cr + dy * dy)
                nx, ny, nz = cr * cp / nl, dy / nl, cr * sp / nl
            end
            nrm[3 * v + 1], nrm[3 * v + 2], nrm[3 * v + 3] = nx, ny, nz
            uv[2 * v + 1], uv[2 * v + 2] = j / S, i / M
        end
    end
    for i = 0, M - 1 do
        for j = 0, S - 1 do
            local a = i * S + j
            local b = i * S + ((j + 1) % S)
            local c = (i + 1) * S + j
            local d = (i + 1) * S + ((j + 1) % S)
            local n = #idx
            idx[n + 1], idx[n + 2], idx[n + 3] = a, b, d
            idx[n + 4], idx[n + 5], idx[n + 6] = a, d, c
        end
    end
    return { pos = pos, nrm = nrm, uv = uv, idx = idx }
end

-- surface-of-revolution cone: base of radius `radius` at y = -halfLen, apex at y = +halfLen
function cone(p)
    local r, h = p.radius, p.halfLen
    local S, M = p.S or 20, p.M or 16
    local pos, nrm, uv, idx = {}, {}, {}, {}
    local twoPi = 2 * math.pi
    local nl = math.sqrt(4 * h * h + r * r)
    for i = 0, M do
        local y = h - i / M * 2 * h
        local cr = r * i / M
        for j = 0, S - 1 do
            local phi = j / S * twoPi
            local cp, sp = math.cos(phi), math.sin(phi)
            local v = i * S + j
            pos[3 * v + 1], pos[3 * v + 2], pos[3 * v + 3] = cr * cp, y, cr * sp
            nrm[3 * v + 1], nrm[3 * v + 2], nrm[3 * v + 3] = 2 * h * cp / nl, r / nl, 2 * h * sp / nl
            uv[2 * v + 1], uv[2 * v + 2] = j / S, i / M
        end
    end
    for i = 0, M - 1 do
        for j = 0, S - 1 do
            local a = i * S + j
            local b = i * S + ((j + 1) % S)
            local c = (i + 1) * S + j
            local d = (i + 1) * S + ((j + 1) % S)
            local n = #idx
            idx[n + 1], idx[n + 2], idx[n + 3] = a, b, d
            idx[n + 4], idx[n + 5], idx[n + 6] = a, d, c
        end
    end
    -- flat base cap at y = -halfLen, facing -Y
    local center = #pos // 3
    pos[#pos + 1], pos[#pos + 2], pos[#pos + 3] = 0, -h, 0
    nrm[#nrm + 1], nrm[#nrm + 2], nrm[#nrm + 3] = 0, -1, 0
    uv[#uv + 1], uv[#uv + 2] = 0.5, 0.5
    for j = 0, S - 1 do
        local phi = j / S * twoPi
        pos[#pos + 1], pos[#pos + 2], pos[#pos + 3] = r * math.cos(phi), -h, r * math.sin(phi)
        nrm[#nrm + 1], nrm[#nrm + 2], nrm[#nrm + 3] = 0, -1, 0
        uv[#uv + 1], uv[#uv + 2] = j / S, 0
    end
    for j = 0, S - 1 do
        local a, b = center + 1 + j, center + 1 + ((j + 1) % S)
        local n = #idx
        idx[n + 1], idx[n + 2], idx[n + 3] = center, a, b
    end
    return { pos = pos, nrm = nrm, uv = uv, idx = idx }
end

-- merge geometry table b into a (b's vertices appended after a's, indices offset)
local function mergeGeo(a, b)
    local nv = #a.pos // 3
    for i = 1, #b.pos do a.pos[#a.pos + 1] = b.pos[i] end
    for i = 1, #b.nrm do a.nrm[#a.nrm + 1] = b.nrm[i] end
    for i = 1, #b.uv do a.uv[#a.uv + 1] = b.uv[i] end
    for i = 1, #b.idx do a.idx[#a.idx + 1] = b.idx[i] + nv end
    return a
end

-- arrow along +Y from y = 0 to y = len: capsule shaft + cone head
function arrow(p)
    local len = p.len or 1.0
    local r = p.radius or len * 0.06
    local headLen = p.headLen or len * 0.25
    local hr = p.headRadius or math.max(r * 2.5, len * 0.1)
    local shaft = capsule({ radius = r, halfLen = (len - headLen) * 0.5, S = p.S or 16, M = p.M or 8 })
    for i = 2, #shaft.pos, 3 do
        shaft.pos[i] = shaft.pos[i] + (len - headLen) * 0.5
    end
    local head = cone({ radius = hr, halfLen = headLen * 0.5, S = p.S or 16, M = p.M or 8 })
    for i = 2, #head.pos, 3 do
        head.pos[i] = head.pos[i] + (len - headLen * 0.5)
    end
    return mergeGeo(shaft, head)
end

-- icosphere (subdivided icosahedron), centered at the origin
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

function sphere(p)
    local subdiv = p.subdiv or 4
    local r = p.radius or 1.0
    local verts, tris = icoSphere(subdiv)
    local n = #verts
    local pos, nrm, uv, idx = {}, {}, {}, {}
    for i = 1, n do
        local d = verts[i]
        pos[3 * i - 2], pos[3 * i - 1], pos[3 * i] = d[1] * r, d[2] * r, d[3] * r
        nrm[3 * i - 2], nrm[3 * i - 1], nrm[3 * i] = d[1], d[2], d[3]
        uv[2 * i - 1] = 0.5 + math.atan(d[3], d[1]) / (2 * math.pi)
        uv[2 * i] = 0.5 + math.asin(d[2]) / math.pi
    end
    for i = 1, #tris do
        idx[i] = tris[i]
    end
    return { pos = pos, nrm = nrm, uv = uv, idx = idx }
end

-- axis-aligned box centered at the origin; p = {hx, hy, hz} half-extents
function box(p)
    local hx, hy, hz = p[1], p[2], p[3]
    local pos, nrm, uv, idx = {}, {}, {}, {}
    -- each face: outward normal + 4 corners, wound CCW when viewed from outside
    local faces = {
        { { 1, 0, 0 }, { { hx, -hy, -hz }, { hx, hy, -hz }, { hx, hy, hz }, { hx, -hy, hz } } },
        { { -1, 0, 0 }, { { -hx, -hy, hz }, { -hx, hy, hz }, { -hx, hy, -hz }, { -hx, -hy, -hz } } },
        { { 0, 1, 0 }, { { -hx, hy, -hz }, { -hx, hy, hz }, { hx, hy, hz }, { hx, hy, -hz } } },
        { { 0, -1, 0 }, { { -hx, -hy, hz }, { -hx, -hy, -hz }, { hx, -hy, -hz }, { hx, -hy, hz } } },
        { { 0, 0, 1 }, { { -hx, -hy, hz }, { hx, -hy, hz }, { hx, hy, hz }, { -hx, hy, hz } } },
        { { 0, 0, -1 }, { { hx, -hy, -hz }, { -hx, -hy, -hz }, { -hx, hy, -hz }, { hx, hy, -hz } } },
    }
    local uvq = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }
    local v0 = -1
    for _, f in ipairs(faces) do
        local n, corners = f[1], f[2]
        for i = 1, 4 do
            local c = corners[i]
            v0 = v0 + 1
            pos[3 * v0 + 1], pos[3 * v0 + 2], pos[3 * v0 + 3] = c[1], c[2], c[3]
            nrm[3 * v0 + 1], nrm[3 * v0 + 2], nrm[3 * v0 + 3] = n[1], n[2], n[3]
            uv[2 * v0 + 1], uv[2 * v0 + 2] = uvq[i][1], uvq[i][2]
        end
        local base = #idx
        local a, b, c, d = v0 - 3, v0 - 2, v0 - 1, v0
        idx[base + 1], idx[base + 2], idx[base + 3] = a, b, c
        idx[base + 4], idx[base + 5], idx[base + 6] = a, c, d
    end
    return { pos = pos, nrm = nrm, uv = uv, idx = idx }
end

-- cw x ch vertex grid in the xz plane at the origin, spanning +/- span/2
function plane(p)
    local cw, ch = p.cw, p.ch
    local span = p.span
    local pos, nrm, uv, idx = {}, {}, {}, {}
    for gy = 0, ch - 1 do
        for gx = 0, cw - 1 do
            local v = gy * cw + gx
            pos[3 * v + 1] = -span / 2 + span * gx / (cw - 1)
            pos[3 * v + 2] = 0
            pos[3 * v + 3] = -span / 2 + span * gy / (ch - 1)
            nrm[3 * v + 1], nrm[3 * v + 2], nrm[3 * v + 3] = 0, 1, 0
            uv[2 * v + 1], uv[2 * v + 2] = gx / (cw - 1), gy / (ch - 1)
        end
    end
    for gy = 0, ch - 2 do
        for gx = 0, cw - 2 do
            local a = gy * cw + gx
            local n = #idx
            idx[n + 1], idx[n + 2], idx[n + 3] = a, a + cw, a + 1
            idx[n + 4], idx[n + 5], idx[n + 6] = a + 1, a + cw, a + cw + 1
        end
    end
    return { pos = pos, nrm = nrm, uv = uv, idx = idx }
end

-- structural + shear springs for the plane grid
function planeCons(p)
    local cw, ch = p.cw, p.ch
    local spacing = p.span / (cw - 1)
    local diag = spacing * math.sqrt(2)
    local cons = {}
    local n = 0
    local function add(a, b, rest, k)
        n = n + 1
        local e = 4 * n - 3
        cons[e], cons[e + 1], cons[e + 2], cons[e + 3] = a, b, rest, k
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
function ballCons(p)
    local pos, idx = p.pos, p.idx
    local n = #pos // 3
    local function vAt(i)
        return pos[3 * i + 1], pos[3 * i + 2], pos[3 * i + 3]
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
    for i = 0, n - 1 do
        local x, y, z = vAt(i)
        cx, cy, cz = cx + x, cy + y, cz + z
    end
    cx, cy, cz = cx / n, cy / n, cz / n
    local dir = {}
    for i = 0, n - 1 do
        local x, y, z = vAt(i)
        dir[i] = norm3({ x - cx, y - cy, z - cz })
    end
    for i = 0, n - 1 do
        local partner, bestDot = -1, 1e30
        for j = 0, n - 1 do
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

-- visual axis triad: three emissive arrows radiating from the origin (X red, Y green, Z blue);
-- opts = { pos = {x,y,z}, quat = {x,y,z,w}, len }; returns the root entity (parent it to follow a body)
function createAxis(opts)
    local len = opts and opts.len or 0.4
    local root = scene.entity()
    scene.replace(root, "Transform", { pos = (opts and opts.pos) or { 0, 0, 0 },
                                     quat = (opts and opts.quat) or { 0, 0, 0, 1 } })
    local axes = {
        { dir = { 1, 0, 0 }, col = { 1.0, 0.2, 0.2 } },
        { dir = { 0, 1, 0 }, col = { 0.2, 1.0, 0.3 } },
        { dir = { 0, 0, 1 }, col = { 0.3, 0.45, 1.0 } },
    }
    for _, a in ipairs(axes) do
        local mat = scene.entity()
        scene.replace(mat, "Material", { baseColor = a.col, emissive = a.col, metallic = 0, roughness = 0.5 })
        local g = scene.entity()
        scene.replace(g, "Geometry", arrow({ len = len }))
        local e = scene.entity()
        local q = (a.dir[2] > 0.999) and { 0, 0, 0, 1 } or math.qfromto({ 0, 1, 0 }, a.dir)
        scene.replace(e, "Transform", { quat = q })
        scene.replace(e, "Renderable", { geom = g, noShadow = true })
        scene.replace(e, "MaterialRef", { material = mat })
        scene.add_child(root, e)
    end
    return root
end

