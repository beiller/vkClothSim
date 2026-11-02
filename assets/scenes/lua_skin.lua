-- Probe: build a skinned character in pure Lua (mesh + skeleton + bones via
-- scene.replace "Skeleton"), then animate the bones in _animate.
-- A 5-bone chain of long thin boxes (0.5 each, along +X, at y = 0.5),
-- each box weighted 100% to its own bone, driven by a subtle traveling wave.
local M = {}

local kN = 5
local kLen = 0.5
local kSkeleton = true
local bones = {}
local t = 0

-- a box spanning [xOff, xOff + kLen] along X, thin in Y/Z
local function section(xOff)
    local b = mesh.box({ kLen * 0.5, 0.05, 0.05 })
    for i = 1, #b.pos, 3 do
        b.pos[i] = b.pos[i] + xOff + kLen * 0.5
    end
    return b
end

function M._on_ready()
    -- support: light, all in Lua
    local light = scene.entity("light")
    scene.replace(light, "Transform", { pos = { 1.25, 2.5, 2 } })
    scene.replace(light, "PointLight", { intensity = 30 })

    -- bone chain: root at (0, 0.5, 0), each bone 0.5 further along +X
    local root = scene.entity("root")
    scene.replace(root, "Transform", { pos = { 0, 0.5, 0 } })
    local parent = root
    for i = 1, kN do
        local b = scene.entity("bone_" .. i)
        scene.replace(b, "Transform", { pos = { (i > 1) and kLen or 0, 0, 0 } })
        scene.add_child(parent, b)
        bones[i] = b
        parent = b
    end

    -- one box per bone, weighted 100% to its own bone
    local pos, nrm, uv, idx, joints, weights = {}, {}, {}, {}, {}, {}
    for i = 1, kN do
        local b = section((i - 1) * kLen)
        local off = #pos // 3
        for j = 1, #b.pos do pos[#pos + 1] = b.pos[j] end
        for j = 1, #b.nrm do nrm[#nrm + 1] = b.nrm[j] end
        for j = 1, #b.uv do uv[#uv + 1] = b.uv[j] end
        for j = 1, #b.idx do idx[#idx + 1] = b.idx[j] + off end
        for _ = 1, #b.pos / 3 do
            joints[#joints + 1], joints[#joints + 2], joints[#joints + 3], joints[#joints + 4] = i - 1, 0, 0, 0
            weights[#weights + 1], weights[#weights + 2], weights[#weights + 3], weights[#weights + 4] = 1, 0, 0, 0
        end
    end

    local lmat = scene.entity("mat_limb")
    scene.replace(lmat, "Material", { baseColor = { 0.9, 0.35, 0.2 }, metallic = 0.1, roughness = 0.5 })
    local limb = scene.entity("limb")
    scene.replace(limb, "Transform", { pos = { 0, 0, 0 } })
    scene.replace(limb, "Geometry", { pos = pos, nrm = nrm, uv = uv, idx = idx, joints = joints, weights = weights })
    scene.replace(limb, "Renderable", { geom = limb })
    scene.replace(limb, "MaterialRef", { material = lmat })
    scene.add_child(root, limb)

    if kSkeleton then
        -- the skeleton: invBinds snapshot the bones' current pose at write time
        local skel = scene.entity("skel")
        scene.replace(skel, "Skeleton", {
            name = "lua_probe",
            boneNames = { "b1", "b2", "b3", "b4", "b5" },
            joints = { bones[1], bones[2], bones[3], bones[4], bones[5] },
            placement = root,
        })
        scene.link_mesh(skel, limb)

        local comp = scene.component(skel, "Skeleton")
        print("lua skin: readback boneNames = " .. tostring(comp and #comp.boneNames or -1)
            .. " joints = " .. tostring(comp and #comp.joints or -1) .. " (expect 5/5)")
        local bad = pcall(scene.replace, scene.entity(), "Skeleton", {
            boneNames = { "a", "b" },
            joints = { bones[1] }, -- wrong count: must be rejected
        })
        print("lua skin: mismatch rejected = " .. tostring(not bad))
    end
end

function M._animate(_, dt)
    t = t + dt
    for i = 1, kN do
        -- subtle traveling wave along the chain
        local yaw = math.sin(t * 0.8 - i * 0.5) * 0.12
        local tilt = math.sin(t * 0.6 + i * 0.4) * 0.06
        scene.set_pose(bones[i], { (i > 1) and kLen or 0, 0, 0 },
                      math.qmul(math.qaxis({ 0, 1, 0 }, yaw), math.qaxis({ 0, 0, 1 }, tilt)))
    end
end

return M
