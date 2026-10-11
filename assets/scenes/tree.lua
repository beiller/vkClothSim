-- fractal tree: kinematic capsule branches in a parent chain, each swaying in a
-- shared sin-wave breeze (static data, lights and UI live in tree.json)

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
for d = 0, kMaxDepth do
    scene.material("mat_d" .. d, depthColor[d], 0.1, 0.6)
end

math.randomseed(20260929)
local windTime = 0.0
local windAxis = { 0, 0, 1 } -- local-frame bend axis
local windFreq = 1.0

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

local function randUnit()
    local v = { math.random() * 2 - 1, math.random() * 2 - 1, math.random() * 2 - 1 }
    local l = scene.vlen(v)
    if l < 1e-3 then
        return { 0, 1, 0 }
    end
    return scene.vscale(v, 1 / l)
end

-- bend this generation away from its parent's direction (main branches bend most)
local function spreadDir(pdir, depth)
    local ang = (1.05 - 0.12 * depth) * (0.75 + 0.5 * math.random())
    local axis = scene.vcross(pdir, randUnit())
    if scene.vlen(axis) < 0.1 then
        axis = { 0, 0, 1 }
    end
    axis = scene.vnorm(axis)
    return scene.vnorm(scene.qrot(scene.qaxis(axis, ang), pdir))
end

-- segment length: capsule overhangs its joint so branch pieces connect
local function edgeLen(depth)
    return 1.5 * (halfLen[depth] + radius[depth])
end

local function addBranch(parent, parentWQ, parentWPos, parentDir, depth)
    local dir = depth <= 1 and { 0, 1, 0 } or spreadDir(parentDir, depth)
    local wpos = depth == 0 and { 0, 1, 0 } or scene.vadd(parentWPos, scene.vscale(dir, edgeLen(depth)))
    local nodeWQ = scene.qfromto({ 0, 1, 0 }, scene.vnorm(dir))
    local localPos = depth == 0 and wpos or scene.qrot(scene.qinv(parentWQ), scene.vsub(wpos, parentWPos))
    local localQuat = depth == 0 and nodeWQ or scene.qmul(scene.qinv(parentWQ), nodeWQ)

    local amp = 0.02 + 0.02 * depth -- tips sway more
    local phase = 0.35 * depth      -- outward lag -> bend travels to the tips
    local isClock = depth == 0
    local e = scene.capsule(halfLen[depth], radius[depth], "mat_d" .. depth, localPos, localQuat, nil,
                            { body = "kinematic", parent = parent })
    scene.anim(e, function(dt)
        if isClock then
            windTime = windTime + dt
        end
        local sway = scene.qaxis(windAxis, amp * math.sin(windFreq * windTime + phase))
        return scene.qrot(sway, localPos), scene.qmul(sway, localQuat)
    end)

    if depth >= kMaxDepth then
        return
    end
    for i = 1, childCount(depth) do
        addBranch(e, nodeWQ, wpos, dir, depth + 1)
    end
end

addBranch(nil, { 0, 0, 0, 1 }, { 0, 0, 0 }, { 0, 1, 0 }, 0)
