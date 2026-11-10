-- fractal tree builder: spawns capsule branches in a parent chain and attaches a branch
-- script to each; also spawns the ground (mesh is the engine-loaded geometry stdlib global;
-- lights and UI live in tree.json). Branches are visual: the branch script drives their
-- Transforms, no physics bodies.

local kMaxDepth = 5
-- depth -> capsule size: thick trunk tapering to thin twigs
local halfLen = { [0] = 0.7, [1] = 1.0, [2] = 0.7, [3] = 0.5, [4] = 0.35, [5] = 0.25 }
local radius = { [0] = 0.5, [1] = 0.4, [2] = 0.3, [3] = 0.22, [4] = 0.15, [5] = 0.1 }
-- brown trunk/branches -> green foliage at the tips
local depthColor = {
    [0] = { 0.30, 0.21, 0.13 },
    [1] = { 0.34, 0.24, 0.15 },
    [2] = { 0.32, 0.22, 0.14 },
    [3] = { 0.20, 0.38, 0.18 },
    [4] = { 0.30, 0.48, 0.22 },
    [5] = { 0.45, 0.62, 0.30 },
}
-- one material + one shared geometry per depth (branches of a depth reuse both)
local capGeom = {}

local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

local function childCount(depth)
    if depth == 0 then
        return 1 -- trunk
    elseif depth == 1 then
        return 6 -- main branches
    elseif depth == 2 then
        return 3
    elseif depth == 3 then
        return 2
    elseif depth == 4 then
        return 1
    else
        return 0
    end
end

-- random unit direction
local function randUnit()
    local v = { math.random() * 2 - 1, math.random() * 2 - 1, math.random() * 2 - 1 }
    local l = math.vlen(v)
    if l < 1e-3 then
        return { 0, 1, 0 }
    end
    return math.vscale(v, 1 / l)
end

-- bend this generation away from its parent's direction (main branches bend most)
local function spreadDir(pdir, depth)
    local ang = (1.05 - 0.12 * depth) * (0.75 + 0.5 * math.random())
    local axis = math.vcross(pdir, randUnit())
    if math.vlen(axis) < 0.1 then
        axis = { 0, 0, 1 }
    end
    axis = math.vnorm(axis)
    return math.vnorm(math.qrot(math.qaxis(axis, ang), pdir))
end

-- segment length: capsule overhangs its joint so branch pieces connect
local function edgeLen(depth)
    return 1.5 * (halfLen[depth] + radius[depth])
end

local function addBranch(parent, parentWQ, parentWPos, parentDir, depth)
    local dir = depth <= 1 and { 0, 1, 0 } or spreadDir(parentDir, depth)
    local wpos = depth == 0 and { 0, 1, 0 } or math.vadd(parentWPos, math.vscale(dir, edgeLen(depth)))
    local nodeWQ = math.qfromto({ 0, 1, 0 }, math.vnorm(dir))
    local localPos = depth == 0 and wpos or math.qrot(math.qinv(parentWQ), math.vsub(wpos, parentWPos))
    local localQuat = depth == 0 and nodeWQ or math.qmul(math.qinv(parentWQ), nodeWQ)
    local amp = 0.02 + 0.02 * depth -- tips sway more
    local phase = 0.35 * depth      -- outward lag -> bend travels to the tips
    local isClock = depth == 0
    local e = scene.entity()
    scene.replace(e, "Transform", { pos = localPos, quat = localQuat })
    scene.replace(e, "Renderable", { geom = capGeom[depth] })
    scene.replace(e, "MaterialRef", { material = scene.get_entity("mat_d" .. depth) })
    if parent then
        scene.add_child(parent, e)
    end
    scene.attach_script(e, "scenes/branch", { pos = localPos, quat = localQuat, amp = amp, phase = phase, isClock = isClock })
    if depth >= kMaxDepth then
        return
    end
    for i = 1, childCount(depth) do
        addBranch(e, nodeWQ, wpos, dir, depth + 1)
    end
end

function _on_ready(self)
    -- static ground (visual only)
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 28, 1, 28 })) })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })

    for d = 0, kMaxDepth do
        local m = scene.entity("mat_d" .. d)
        scene.replace(m, "Material", { baseColor = depthColor[d], metallic = 0.1, roughness = 0.6 })
        capGeom[d] = geom(mesh.capsule({ halfLen = halfLen[d], radius = radius[d] }))
    end
    math.randomseed(20260929)
    addBranch(nil, { 0, 0, 0, 1 }, { 0, 0, 0 }, { 0, 1, 0 }, 0)
end

