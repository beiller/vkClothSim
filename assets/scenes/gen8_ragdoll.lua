local uib = require "ui"
local rag = require "ragdoll"


-- script variables, bindable via "caps.LuaScript.<var>"
kToneFreq = 5.0 -- Hz; springback speed (higher = stiffer; >~50 destabilizes at zeta=1)
kToneDamp = 50.0 -- damping ratio; 1 = critical (no oscillation)
kGripTorque = 10.0 -- N m max friction torque per rotation axis
kBodyDamping = 0.1
kBodyFriction = 10.0

local joints = {} -- base-ragdoll SixDofJoints, retuned by applyTone
local boneEnt = {} -- base ragdoll: boneName -> RigidBody entity; handed to the rig modules for parent resolution
local kLimits = {}

local meshMaterialsCollisionShapes = {
    "Torso",
    "Face",
    "Lips",
    "Teeth",
    "Ears",
    "Legs",
    "EyeSocket",
    "Mouth",
    "Arms",
    "Pupils",
    "EyeMoisture",
    "Fingernails",
    "Cornea",
    "Irises",
    "Sclera",
    "Toenails"
}

-- bones that get a rigid body with tapered-capsule collision
local kBones = {
    "pelvis", "abdomenLower", "abdomenUpper", "chestLower", "chestUpper",
    "lCollar", "rCollar", "neckLower", "neckUpper", "head",
    "lShin", "lThighBend", "lThighTwist",
    "rShin", "rThighBend", "rThighTwist",
    "lShldrBend", "lShldrTwist", "lForearmBend", "lForearmTwist",
    "rShldrBend", "rShldrTwist", "rForearmBend", "rForearmTwist"
}

local kWeightThreshold = 0.3 -- skin weight above which a vertex joins a bone's cloud
local kRadiusScale = 0.9     -- shrink the fitted radii so shapes sit inside the skin
local kFitOptions = { profile_bins = 64, max_segments = 8, profile_percentile = 1.0, sphere_aspect = 5.0 }


function _on_ready(self)
    local ch = self.entity
    local entity_name = scene.name(self.entity)
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
    -- entity-form gltf load: skinned model (the table API supports skinning too; kept on the entity path for full parity)
    local gltfName = "hanako"
    graphics.gltf(ch, "local/" .. gltfName .. ".glb")

    -- per-bone collision fits + joint table from the loaded skeleton (rest pose); scoped to ch so
    -- other skeletons in the scene can't be picked up. The joint table is captured before any
    -- skeleton-ancestor walk only works while the original hierarchy is intact
    local skelEnt = scene.find_components("Skeleton", ch)[1]
    assert(skelEnt, "caps_fit: no skeleton under 'char'")
    local skel = scene.component(skelEnt, "Skeleton")
    kLimits = rag.loadLimits({ "assets/local/genesis8_bone_limits.json", "local/genesis8_bone_limits.json" }, "caps_fit")
    local kFits = rag.buildFits(skel, kBones, meshMaterialsCollisionShapes, { tag = "caps_fit", weightThreshold = kWeightThreshold,
                                               radiusScale = kRadiusScale, fitOptions = kFitOptions })
    local kJoints = rag.buildJoints(skel)

    -- "alive at rest": static friction holds the pose, a small zero-target position motor springs
    -- joints back to rest (muscle tone), linear damping settles the sway
    for _, f in ipairs(kFits) do
        boneEnt[f.name] = rag.makeBoneBody(f, { damping = kBodyDamping, friction = kBodyFriction })
        -- reparent the character's skinned bone under the rigid body so the skin follows
        rag.attachBone(boneEnt[f.name], scene.get_entity(gltfName .. "/" .. f.name))
    end

    local jointBy = {}
    for _, j in ipairs(kJoints) do
        jointBy[j.name] = j
    end
    local tone = { freq = kToneFreq, damp = kToneDamp, grip = kGripTorque }
    -- perfectly stiff 6DOF weld: each shaped bone is joined to its shaped parent at the bone's
    -- joint world position (kJoints); zero limits hold the rest-pose relative pose
    for name, bone in pairs(boneEnt) do
        local j = jointBy[name]
        local parent = j and boneEnt[j.parent]
        if j and parent then
            joints[#joints + 1] = rag.makeWeld(bone, parent, j, kLimits[name], tone)
        end
    end

    local win = uib.make_window("ragdoll demo", 320, 200, { -1, 1.5, -1 })

    joints[#joints + 1] = rag.makeWeld(boneEnt["pelvis"], boneEnt["abdomenLower"], jointBy["hip"], kLimits["pelvis"], tone)

    -- hack to fix things that have "hip" weights (some grafts)
    scene.add_child(boneEnt["pelvis"], scene.get_entity(gltfName .. "/hip"), { keepWorld = true })

    --- add collision exceptions
    rag.excludeCollisions({ 
        boneEnt["rThighTwist"],
        boneEnt["lThighTwist"],
        boneEnt["pelvis"],
    })

    rag.excludeCollisions({
        boneEnt["chestUpper"],
        boneEnt["chestLower"],
        boneEnt["abdomenLower"],
        boneEnt["abdomenUpper"],
        boneEnt["pelvis"],
    })

    print("base: " .. #boneEnt .. " bones in " .. #joints .. " joints")
        
    -- rig modules: each builds its own sub-ragdoll, collision exclusions, and controls in the shared window
    scene.attach_script(scene.entity("hand_rig"), "scenes/hands",
                        { ch = ch, prefix = gltfName .. "/", boneEnt = boneEnt, win = win, materials = meshMaterialsCollisionShapes })
    scene.attach_script(scene.entity("foot_rig"), "scenes/feet",
                        { ch = ch, prefix = gltfName .. "/", boneEnt = boneEnt, win = win, materials = meshMaterialsCollisionShapes })
    
    -- the ui: one window, a text line, sliders, and the scene editor
    local tab = uib.tab(win, "scene")
    uib.text(tab, "Rig settings")
    local onChange = entity_name .. ".LuaScript.applyTone" -- url for function to call when slider changed
    uib.bind_slider(tab, "kToneFreq", entity_name .. ".LuaScript.kToneFreq", 0, 100.0, "%.2f", onChange)
    uib.bind_slider(tab, "kToneDamp", entity_name .. ".LuaScript.kToneDamp", 0, 5.0, "%.1f", onChange)
    uib.bind_slider(tab, "kGripTorque", entity_name .. ".LuaScript.kGripTorque", 0, 10000.0, "%.2f", onChange)
    uib.bind_slider(tab, "kBodyDamping", entity_name .. ".LuaScript.kBodyDamping", 0, 100.0, "%.2f", onChange)
    uib.bind_slider(tab, "kBodyFriction", entity_name .. ".LuaScript.kBodyFriction", 0, 100.0, "%.2f", onChange)
    uib.editor(uib.tab(win, "editor"))
    self.metricsText = uib.text(uib.make_window("metrics", 160, 180, { 0.45, 1.72, 2.8 }), "")
end


-- slider handler, called only when a slider moves (or every frame — the diff no-ops if nothing changed)
function applyTone(self)
    for _, w in ipairs(joints) do
        local j = scene.component(w, "SixDofJoint")
        j.motorFreq = { kToneFreq, kToneFreq, kToneFreq }
        j.motorDamp = { kToneDamp, kToneDamp, kToneDamp }
        j.frictionAng = { kGripTorque, kGripTorque, kGripTorque }
    end
    for _, b in ipairs(boneEnt) do
        local rb = scene.component(b, "RigidBody")
        rb.damping = kBodyDamping
        rb.friction = kBodyFriction
    end
end
