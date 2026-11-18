local rag = require "ragdoll"

local M = {}

-- foot bones in the skeleton's plain naming; "l" / "r" prefixes make the full names
local kFootBones = { "Foot", "Metatarsals", "Toe",
                     "BigToe", "BigToe_2",
                     "SmallToe1", "SmallToe1_2", "SmallToe2", "SmallToe2_2",
                     "SmallToe3", "SmallToe3_2", "SmallToe4", "SmallToe4_2" }

-- toe tone; 50 Hz sat borderline at zeta=1 (workplans/JOLT_PERF.md), kept at the body default
M.kToneFreq = 20.0

local function footBoneNames()
    local names = {}
    for _, prefix in ipairs({ "l", "r" }) do
        for _, b in ipairs(kFootBones) do
            names[#names + 1] = prefix .. b
        end
    end
    return names
end

function M.createFootRig(ch, char_prefix, boneEnt, opts)
    opts = opts or {}
    local tone = opts.tone or { freq = M.kToneFreq, damp = rag.kToneDamp, grip = rag.kGripTorque }
    return rag.createSubRagdoll(ch, char_prefix, boneEnt, footBoneNames(), "feet", { tone = tone })
end

return M
