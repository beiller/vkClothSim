local uib = require "ui"
local rag = require "ragdoll"

-- hand bones in the skeleton's plain naming; "l" / "r" prefixes make the full names
local kHandBones = { "Hand", "Thumb1", "Thumb2", "Thumb3", "Carpal1", "Carpal2", "Carpal3", "Carpal4",
                     "Index1", "Index2", "Index3", "Mid1", "Mid2", "Mid3",
                     "Ring1", "Ring2", "Ring3", "Pinky1", "Pinky2", "Pinky3" }

-- full-grasp angles per bone suffix, left-hand joint frame (right hand mirrors sign)
local kGrasp = {
    Thumb1 = { { 0, 1, 0 }, 25 }, Thumb2 = { { 0, 1, 0 }, 50 }, Thumb3 = { { 0, 1, 0 }, 50 },
    Index1 = { { 0, 0, 1 }, -50 }, Index2 = { { 0, 0, 1 }, -90 }, Index3 = { { 0, 0, 1 }, -70 },
    Mid1   = { { 0, 0, 1 }, -50 }, Mid2   = { { 0, 0, 1 }, -90 }, Mid3   = { { 0, 0, 1 }, -70 },
    Ring1  = { { 0, 0, 1 }, -50 }, Ring2  = { { 0, 0, 1 }, -90 }, Ring3  = { { 0, 0, 1 }, -70 },
    Pinky1 = { { 0, 0, 1 }, -50 }, Pinky2 = { { 0, 0, 1 }, -90 }, Pinky3 = { { 0, 0, 1 }, -70 }
}

local defaultOpts = {
    tone = {
        freq = 60.0,
        damp = 10.0,
        grip = 1.0,
    },
    damping = 0.1,
    friction = 10.0,
    weightThreshold = 0.25,
    radiusScale = 1.1,
    fitOptions = {
        profile_bins = 64,
        max_segments = 1,
        profile_percentile = 1.0,
        sphere_aspect = 5.0,
        max_clusters = 1
    },
    debugVisuals = true,
    mass = 0.04
}

-- grasp settings; bindable as "<attached entity>.LuaScript.<var>" when attached as a script
lHandGrasp = 0.0
rHandGrasp = 0.0
lHandGraspHz = 10.0
rHandGraspHz = 10.0

local jointsByName = {} -- per-side SixDofJoint entities from createSubRagdoll
local kLimits = {}      -- joint limits, clamped against when driving motor targets

local function handBoneNames()
    local names = {}
    for _, prefix in ipairs({ "l", "r" }) do
        for _, b in ipairs(kHandBones) do
            names[#names + 1] = prefix .. b
        end
    end
    return names
end

-- the hand sub-ragdoll: one RigidBody + weld per bone; parents outside the set resolve against boneEnt
function createHandRig(ch, prefix, boneEnt, materials)
    local data = rag.createSubRagdoll(ch, prefix, boneEnt, handBoneNames(), "hands", defaultOpts, materials)
    jointsByName = data.jointsByName
    kLimits = data.limits
    -- fingers of a side overlap at rest: every bone ignores its fellows

    local excludeBodies = {
        l = {}, r = {}
    }
    for k, v in pairs(data.bodies.l) do excludeBodies.l[k] = v end
    for k, v in pairs(data.bodies.r) do excludeBodies.r[k] = v end
    excludeBodies.l[#excludeBodies.l + 1] = boneEnt['lForearmTwist']
    excludeBodies.r[#excludeBodies.r + 1] = boneEnt['rForearmTwist']
    rag.excludeCollisions(excludeBodies.l)
    rag.excludeCollisions(excludeBodies.r)
    return data
end

local function setHand(side, g, hz)
    local s = side == "l" and 1 or -1
    for name, w in pairs(jointsByName[side] or {}) do
        local t = kGrasp[name:sub(2)]
        if t then
            local a = s * t[2] * g
            local lim = kLimits[name] and kLimits[name][t[1][3] > 0 and "z" or "y"]
            if lim then -- clamp to the joint limits: Jolt mis-clamps targets far out of range
                a = math.max(-math.deg(lim[2]), math.min(-math.deg(lim[1]), a))
            end
            local j = scene.component(w, "SixDofJoint")
            j.motorTargetQuat = math.qaxis(t[1], math.rad(a))
            j.motorFreq = { hz, hz, hz }
        end
    end
end

-- slider callback: re-apply the grasp motors from the current settings
function applyGrasp(self)
    setHand("l", lHandGrasp, lHandGraspHz)
    setHand("r", rHandGrasp, rHandGraspHz)
end

-- attached-script mode: build the rig and draw its controls into the shared window
function _on_ready(self)
    local p = self.params
    createHandRig(p.ch, p.prefix, p.boneEnt, p.materials)
    local url = scene.name(self.entity) .. ".LuaScript."
    local tab = uib.tab(p.win, "hands")
    uib.text(tab, "Grasp")
    uib.bind_slider(tab, "lHandGrasp", url .. "lHandGrasp", -1.0, 1.0, "%.2f", url .. "applyGrasp")
    uib.bind_slider(tab, "rHandGrasp", url .. "rHandGrasp", -1.0, 1.0, "%.2f", url .. "applyGrasp")
    uib.bind_slider(tab, "lHandGraspHz", url .. "lHandGraspHz", 0.5, 30.0, "%.1f", url .. "applyGrasp")
    uib.bind_slider(tab, "rHandGraspHz", url .. "rHandGraspHz", 0.5, 30.0, "%.1f", url .. "applyGrasp")
end
