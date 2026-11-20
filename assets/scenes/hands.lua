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
    debugVisuals = false,
    mass = 0.04
}

lHandGrasp = 0.0
rHandGrasp = 0.0
lHandGraspHz = 10.0
rHandGraspHz = 10.0

graspJoints = {}
local kLimits = {}

local function handBoneNames(self)
    local names = {}
    for _, prefix in ipairs({ "l", "r" }) do
        for _, b in ipairs(kHandBones) do
            names[#names + 1] = prefix .. b
        end
    end
    return names
end


function _on_ready(self)
    local handData = rag.createSubRagdoll(
        self.params.characterEntity, 
        self.params.gltfName .. "/", 
        self.params.boneEnt, 
        handBoneNames(), 
        "hands", 
        defaultOpts,
        self.params.meshMaterialsCollisionShapes
    )
    graspJoints = handData.jointsByName
    kLimits = self.params.kLimits

    -- local tab = uib.tab(self.params.win, "scene")
    -- uib.text(tab, "Rig settings")

    -- local onChangeGrasp = "caps.LuaScript.applyGrasp"
    -- uib.bind_slider(tab, "lHandGrasp", "caps.LuaScript.lHandGrasp", -1.0, 1.0, "%.2f", onChangeGrasp)
    -- uib.bind_slider(tab, "rHandGrasp", "caps.LuaScript.rHandGrasp", -1.0, 1.0, "%.2f", onChangeGrasp)
    -- uib.bind_slider(tab, "lHandGraspHz", "caps.LuaScript.lHandGraspHz", 0.5, 30.0, "%.1f", onChangeGrasp)
    -- uib.bind_slider(tab, "rHandGraspHz", "caps.LuaScript.rHandGraspHz", 0.5, 30.0, "%.1f", onChangeGrasp)
end


local function setHand(self, side, g, hz)
    local s = side == "l" and 1 or -1
    for name, w in pairs(graspJoints[side] or {}) do
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

function applyGrasp(self, graspJoints)
    setHand("l", lHandGrasp, lHandGraspHz)
    setHand("r", rHandGrasp, rHandGraspHz)
end

