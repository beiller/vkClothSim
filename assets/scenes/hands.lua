local rag = require "ragdoll"

local M = {}

-- hand bones in the skeleton's plain naming; "l" / "r" prefixes make the full names
local kHandBones = { "Hand", "Thumb1", "Thumb2", "Thumb3", "Carpal1", "Carpal2", "Carpal3", "Carpal4",
                     "Index1", "Index2", "Index3", "Mid1", "Mid2", "Mid3",
                     "Ring1", "Ring2", "Ring3", "Pinky1", "Pinky2", "Pinky3" }

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
    return rag.createSubRagdoll(ch, char_prefix, boneEnt, handBoneNames(), "hands", opts)
end

return M
