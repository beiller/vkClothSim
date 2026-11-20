local rag = require "ragdoll"

local M = {}

-- foot bones in the skeleton's plain naming; "l" / "r" prefixes make the full names
local kFootBones = { "Foot", "Metatarsals", "Toe",
                     "BigToe", "BigToe_2",
                     "SmallToe1", "SmallToe1_2", "SmallToe2", "SmallToe2_2",
                     "SmallToe3", "SmallToe3_2", "SmallToe4", "SmallToe4_2" }

local defaultOpts = {
    tone = {
        freq = 60.0,
        damp = 10.0,
        grip = 1.0,
    },
    damping = 0.1,
    friction = 10.0,
    weightThreshold = 0.5,
    radiusScale = 0.9,
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

local function footBoneNames()
    local names = {}
    for _, prefix in ipairs({ "l", "r" }) do
        for _, b in ipairs(kFootBones) do
            names[#names + 1] = prefix .. b
        end
    end
    return names
end

function M.createFootRig(ch, char_prefix, boneEnt, win, opts)
    opts = opts or defaultOpts
    return rag.createSubRagdoll(ch, char_prefix, boneEnt, footBoneNames(), "feet", opts)
end

return M
