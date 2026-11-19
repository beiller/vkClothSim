local rag = require "ragdoll"

local M = {}

-- hand bones in the skeleton's plain naming; "l" / "r" prefixes make the full names
local kHandBones = { "Hand", "Thumb1", "Thumb2", "Thumb3", "Carpal1", "Carpal2", "Carpal3", "Carpal4",
                     "Index1", "Index2", "Index3", "Mid1", "Mid2", "Mid3",
                     "Ring1", "Ring2", "Ring3", "Pinky1", "Pinky2", "Pinky3" }

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

-- full-grasp angles per bone suffix, left-hand joint frame (right hand mirrors sign)
M.kGrasp = {
    Thumb1 = { { 0, 1, 0 }, 25 }, Thumb2 = { { 0, 1, 0 }, 50 }, Thumb3 = { { 0, 1, 0 }, 50 },
    Index1 = { { 0, 0, 1 }, -50 }, Index2 = { { 0, 0, 1 }, -90 }, Index3 = { { 0, 0, 1 }, -70 },
    Mid1   = { { 0, 0, 1 }, -50 }, Mid2   = { { 0, 0, 1 }, -90 }, Mid3   = { { 0, 0, 1 }, -70 },
    Ring1  = { { 0, 0, 1 }, -50 }, Ring2  = { { 0, 0, 1 }, -90 }, Ring3  = { { 0, 0, 1 }, -70 },
    Pinky1 = { { 0, 0, 1 }, -50 }, Pinky2 = { { 0, 0, 1 }, -90 }, Pinky3 = { { 0, 0, 1 }, -70 } }

local function handBoneNames()
    local names = {}
    for _, prefix in ipairs({ "l", "r" }) do
        for _, b in ipairs(kHandBones) do
            names[#names + 1] = prefix .. b
        end
    end
    return names
end

function M.createHandRig(ch, char_prefix, boneEnt, opts)
    opts = opts or defaultOpts
    return rag.createSubRagdoll(ch, char_prefix, boneEnt, handBoneNames(), "hands", opts)
end

return M
