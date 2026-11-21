local uib = require "ui"
local rag = require "ragdoll"

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
    debugVisuals = true,
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

-- the foot overlaps the shin above it: exclude each side's shin against its foot + metatarsals
local function excludeShin(boneEnt, side, bodies)
    local shin = boneEnt[side .. "Shin"]
    if not shin then return end
    rag.excludeCollisions({ shin, bodies[side][side .. "Foot"], bodies[side][side .. "Metatarsals"] })
end

-- the foot sub-ragdoll: one RigidBody + weld per bone; parents outside the set resolve against boneEnt
local function createFootRig(ch, prefix, boneEnt, materials)
    local data = rag.createSubRagdoll(ch, prefix, boneEnt, footBoneNames(), "feet", defaultOpts, materials)
    excludeShin(boneEnt, "l", data.bodies)
    excludeShin(boneEnt, "r", data.bodies)
    -- toes of a side overlap at rest: every bone ignores its fellows
    rag.excludeCollisions(data.bodies.l)
    rag.excludeCollisions(data.bodies.r)
    return data
end

-- attached-script mode: build the rig and reserve a tab in the shared window for foot controls
function _on_ready(self)
    local p = self.params
    createFootRig(p.ch, p.prefix, p.boneEnt, p.materials)
    local tab = uib.tab(p.win, "feet")
    uib.text(tab, "Toes")
end

