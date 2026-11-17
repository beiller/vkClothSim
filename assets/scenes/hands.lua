
local rag = require "ragdoll"

local M = {}

-- tunables (module level; pass opts to createHandRig for per-call overrides)
M.kToneFreq = 3.0        -- Hz; springback speed of the joint position motors
M.kToneDamp = 25.0       -- damping ratio; 1 = critical (no oscillation)
M.kGripTorque = 1.0      -- N m max friction torque per rotation axis
M.kBodyDamping = 0.05    -- linear/angular damping of the hand bodies
M.kBodyFriction = 10.0   -- high friction: static grip holds the pose at rest
M.kWeightThreshold = 0.7 -- skin weight above which a vertex joins a bone's cloud
M.kRadiusScale = 0.9     -- shrink the fitted radii so shapes sit inside the skin
-- max_clusters = 1: one collider per bone (a single tapered capsule / sphere). Raise it
-- (e.g. 4) for multi-part fits on curved bones, at the cost of more colliders.
M.kFitOptions = { profile_bins = 64, max_segments = 1, profile_percentile = 1.0,
                  sphere_aspect = 5.0, max_clusters = 1 }
M.kDebugVisuals = false   -- render one capsule per bone, parented to the body

-- hand bones in the skeleton's plain naming; "l" / "r" prefixes make the full names
local kHandBones = { "Thumb1", "Thumb2", "Thumb3", "Carpal1", "Carpal2", "Carpal3", "Carpal4",
                     "Index1", "Index2", "Index3", "Mid1", "Mid2", "Mid3",
                     "Ring1", "Ring2", "Ring3", "Pinky1", "Pinky2", "Pinky3" }

local kLimitsPaths = { "assets/local/genesis8_bone_limits.json", "local/genesis8_bone_limits.json" }

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
    opts = opts or {}
    local tone = { freq = opts.tone and opts.tone.freq or M.kToneFreq,
                   damp = opts.tone and opts.tone.damp or M.kToneDamp,
                   grip = opts.tone and opts.tone.grip or M.kGripTorque }

    local skelEnt = scene.find_components("Skeleton", ch)[1]
    assert(skelEnt, "hands: no skeleton under the character root")
    local skel = scene.component(skelEnt, "Skeleton")

    local kLimits = rag.loadLimits(kLimitsPaths, "hands")
    local kJoints = rag.buildJoints(skel)
    local jointBy = {}
    for _, j in ipairs(kJoints) do
        jointBy[j.name] = j
    end

    local kFits = rag.buildFits(skel, handBoneNames(), {
        tag = "hands",
        weightThreshold = opts.weightThreshold or M.kWeightThreshold,
        radiusScale = opts.radiusScale or M.kRadiusScale,
        fitOptions = opts.fitOptions or M.kFitOptions
    })
    -- one RigidBody per fitted bone, kept in two lists (left / right hand)
    local bodies, bodyBy = { l = {}, r = {} }, {}
    for _, f in ipairs(kFits) do
        local body = rag.makeBoneBody(f, {
            material = opts.material,
            debugVisuals = opts.debugVisuals ~= nil and opts.debugVisuals or M.kDebugVisuals,
            damping = opts.damping or M.kBodyDamping,
            friction = opts.friction or M.kBodyFriction })
        bodyBy[f.name] = body
        bodies[f.name:sub(1, 1)][f.name] = body -- "l..." / "r..."
        -- reparent the character's skinned bone under the rigid body so the skin follows
        rag.attachBone(body, scene.get_entity(char_prefix .. f.name))
    end

    local joints = { l = {}, r = {} }
    for _, f in ipairs(kFits) do
        local j = jointBy[f.name]
        if not j then
            print("hands: no skeleton joint for '" .. f.name .. "', left free")
        else
            local parentBody = bodies.l[j.parent] or bodies.r[j.parent] or boneEnt[j.parent]
            if not parentBody then
                print("hands: '" .. f.name .. "' has no body for parent '" .. j.parent .. "', left free")
            else
                local side = joints[f.name:sub(1, 1)]
                side[#side + 1] = rag.makeWeld(bodyBy[f.name], parentBody, j, kLimits[f.name], tone)
            end
        end
    end

    local nExc = rag.excludeCollisions(bodies.l) + rag.excludeCollisions(bodies.r)
    print("hands: " .. #kFits .. " hand bones in " .. (#joints.l + #joints.r) .. " joints, " .. nExc .. " self-collision exceptions")

    return { bodies = bodies, joints = joints }
end

return M
