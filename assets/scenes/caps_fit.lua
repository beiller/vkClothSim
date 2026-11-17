local uib = require("ui")
local rag = require "ragdoll"
local hand_rig = require("hands")


-- script variables, bindable via "caps.LuaScript.<var>"
kToneFreq = 3.0 -- Hz; springback speed (higher = stiffer)
kToneDamp = 25.0 -- damping ratio; 1 = critical (no oscillation)
kGripTorque = 1.0 -- N m max friction torque per rotation axis
kBodyDamping = 0.05
kBodyFriction = 10.0

local joints = {} -- every SixDofJoint entity (base ragdoll + hands), retuned by applyTone
local boneEnt = {} -- base ragdoll: boneName -> RigidBody entity (also the hand rig's attachment table)
local bodies = {} -- every RigidBody entity (base ragdoll + hands), retuned by applyTone

-- bones that get a rigid body with tapered-capsule collision
local kBones = {
    "pelvis", "abdomenLower", "abdomenUpper", "chestLower", "chestUpper",
    "lCollar", "rCollar", "neckLower", "neckUpper", "head",
    "lShin", "lThighBend", "lThighTwist", "lFoot", "lMetatarsals",
    "rShin", "rThighBend", "rThighTwist", "rFoot", "rMetatarsals",
    "lShldrBend", "lShldrTwist", "lForearmBend", "lForearmTwist", "lHand",
    "rShldrBend", "rShldrTwist", "rForearmBend", "rForearmTwist", "rHand" 
}

local kWeightThreshold = 0.3 -- skin weight above which a vertex joins a bone's cloud
local kRadiusScale = 0.9     -- shrink the fitted radii so shapes sit inside the skin
local kFitOptions = { profile_bins = 64, max_segments = 8, profile_percentile = 1.0, sphere_aspect = 5.0 }

function create_cloth()
    local world_scale = 0.25
    local clothSpan = 8.0 * world_scale
    local clothDim = 96
    local clothGeom = rag.geom(mesh.plane({ cw = clothDim, ch = clothDim, span = clothSpan }))
    clothH = scene.entity("cloth")
    scene.replace(clothH, "Transform", { pos = {0, 0, 0} })
    scene.replace(clothH, "Renderable", { geom = clothGeom })
    scene.replace(clothH, "MaterialRef", { material = scene.get_entity("mat_cloth") })
    scene.replace(clothH, "SoftBodyData",
                  { cons = mesh.planeCons({ cw = clothDim, ch = clothDim, span = clothSpan }), mass = 0.15 })

    local tE = 0.2
    local shape = scene.entity()
    scene.replace(shape, "SphereShape", { radius = tE * 2.0 })
    local col = scene.entity()
    --scene.replace(col, "Transform", { pos = off, quat = q })
    scene.replace(col, "Collider", { shape = shape })

    -- pin the front edge (plane row gy=0 sits at z=-span/2) where it spawns; set_pins captures current positions
    local pinW = {}
    for i = 1, clothDim * clothDim do
        pinW[i] = 0.0
    end
    for i = 1, clothDim do
        pinW[i] = 1.0
    end

    local tetra = scene.entity()
 
    scene.replace(tetra, "Geometry", {
        pos = { tE, tE, tE,  tE, -tE, -tE,
                -tE, tE, -tE,  -tE, -tE, tE },
        nrm = { 0.5774, 0.5774, 0.5774,  0.5774, -0.5774, -0.5774,
                -0.5774, 0.5774, -0.5774,  -0.5774, -0.5774, 0.5774 },
        idx = { 0, 1, 2,  0, 3, 1,  0, 2, 3,  1, 3, 2 } })

    local tetra_material = scene.entity()
    scene.replace(tetra_material, "Material", {baseColor = {0,0,0}, emissive={10, 9.5, 9}})
    
    local tetra_mesh = scene.entity()
    scene.replace(tetra_mesh, "Transform", {pos = {0,3,0}})
    scene.replace(tetra_mesh, "Renderable", {geom = tetra})
    scene.replace(tetra_mesh, "MaterialRef", {material = tetra_material})
    scene.replace(tetra_mesh, "KinematicBody", { colliders = { col } })
    scene.replace(clothH, "Parent", {e = tetra_mesh})

    soft.set_pins(clothH, pinW)
end

function _on_ready(self)
    local ch = scene.entity("char")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
-- entity-form gltf load: skinned model (the table API supports skinning too; kept on the entity path for full parity)
    graphics.gltf(ch, "local/hashimoto.glb")
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = rag.geom(mesh.box({ 55, 1, 55 })) })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })
    -- static ground collider (kinematic = immovable)
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 55, 1, 55 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })

    -- per-bone collision fits + joint table from the loaded skeleton (rest pose); scoped to ch so
    -- other skeletons in the scene can't be picked up. The joint table is captured before any
    -- bodies exist: attachBone reparents the gltf bones under the new rigid bodies, so the
    -- skeleton-ancestor walk only works while the original hierarchy is intact
    local skelEnt = scene.find_components("Skeleton", ch)[1]
    assert(skelEnt, "caps_fit: no skeleton under 'char'")
    local skel = scene.component(skelEnt, "Skeleton")
    local kLimits = rag.loadLimits({ "assets/local/genesis8_bone_limits.json", "local/genesis8_bone_limits.json" }, "caps_fit")
    local kFits = rag.buildFits(skel, kBones, { tag = "caps_fit", weightThreshold = kWeightThreshold,
                                               radiusScale = kRadiusScale, fitOptions = kFitOptions })
    local kJoints = rag.buildJoints(skel)

    -- "alive at rest": static friction holds the pose, a small zero-target position motor springs
    -- joints back to rest (muscle tone), linear damping settles the sway
    local n, nj, nlim = 0, 0, 0
    for _, f in ipairs(kFits) do
        for _, cap in ipairs(f.caps) do
            n = n + #cap.seg
        end
        boneEnt[f.name] = rag.makeBoneBody(f, { damping = kBodyDamping, friction = kBodyFriction })
        bodies[#bodies + 1] = boneEnt[f.name]
        -- reparent the character's skinned bone under the rigid body so the skin follows
        rag.attachBone(boneEnt[f.name], scene.get_entity("hashimoto/" .. f.name))
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
            if kLimits[name] then
                nlim = nlim + 1
            end
            joints[#joints + 1] = rag.makeWeld(bone, parent, j, kLimits[name], tone)
            nj = nj + 1
        end
    end

    local handData = hand_rig.createHandRig(ch, "hashimoto/", boneEnt)
    -- flatten the hand rig into the shared lists so applyTone covers base + hands alike
    for _, side in ipairs({ "l", "r" }) do
        for _, w in ipairs(handData.joints[side]) do
            joints[#joints + 1] = w
        end
        for _, b in pairs(handData.bodies[side]) do
            bodies[#bodies + 1] = b
        end
    end

    -- the pelvis has no shaped parent (its joint ancestor "hip" is a graft, not a body): weld it at the hip
    if kLimits["pelvis"] then
        nlim = nlim + 1
    end
    joints[#joints + 1] = rag.makeWeld(boneEnt["pelvis"], boneEnt["abdomenLower"], jointBy["hip"], kLimits["pelvis"], tone)
    nj = nj + 1

    -- hack to fix things that have "hip" weights (some grafts)
    scene.add_child(boneEnt["pelvis"], scene.get_entity("hashimoto/hip"), { keepWorld = true })

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

    print("caps_fit: " .. n .. " capsule segments in " .. #kFits .. " bone bodies, " .. nj .. " joints (" .. nlim .. " limited)")

    -- the ui: one window, a text line, sliders, and the scene editor
    local win = uib.make_window("ragdoll demo", 320, 200, { 0, 1.5, 2.8 })
    local tab = uib.tab(win, "scene")
    uib.text(tab, "Rig settings")
    local onChange = "caps.LuaScript.applyTone" -- url for function to call when slider changed
    uib.bind_slider(tab, "kToneFreq", "caps.LuaScript.kToneFreq", 0, 100.0, "%.2f", onChange)
    uib.bind_slider(tab, "kToneDamp", "caps.LuaScript.kToneDamp", 0, 10000.0, "%.1f", onChange)
    uib.bind_slider(tab, "kGripTorque", "caps.LuaScript.kGripTorque", 0, 10000.0, "%.2f", onChange)
    uib.bind_slider(tab, "kBodyDamping", "caps.LuaScript.kBodyDamping", 0, 100.0, "%.2f", onChange)
    uib.bind_slider(tab, "kBodyFriction", "caps.LuaScript.kBodyFriction", 0, 100.0, "%.2f", onChange)
    uib.editor(uib.tab(win, "editor"))
    self.metricsText = uib.text(uib.make_window("metrics", 160, 180, { 0.45, 1.72, 2.8 }), "")

    soft.set_floor(0.001)
    create_cloth()
end


-- refresh the metrics window from the engine's performance string every frame
function _animate(self, dt)
    local t = scene.component(self.metricsText, "UIText")
    if t then
        t.label = engine.performance_metrics()
    end
end

-- slider handler, called only when a slider moves (or every frame — the diff no-ops if nothing changed)
function applyTone(self)
    for _, w in ipairs(joints) do
        local j = scene.component(w, "SixDofJoint")
        j.motorFreq = { kToneFreq, kToneFreq, kToneFreq }
        j.motorDamp = { kToneDamp, kToneDamp, kToneDamp }
        j.frictionAng = { kGripTorque, kGripTorque, kGripTorque }
    end
    for _, b in ipairs(bodies) do
        local rb = scene.component(b, "RigidBody")
        rb.damping = kBodyDamping
        rb.friction = kBodyFriction
    end
end

function _physics(self, dt)

end
