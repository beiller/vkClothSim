
local json = require "json"
local uib = require("ui")



-- script variables, bindable via "caps.LuaScript.<var>"
kToneFreq = 3.0 -- Hz; springback speed (higher = stiffer)
kToneDamp = 25.0 -- damping ratio; 1 = critical (no oscillation)
kGripTorque = 1.0 -- N m max friction torque per rotation axis
kBodyDamping = 0.05
kBodyFriction = 10.0

local joints = {}
local boneEnt = {}

local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

-- Daz Genesis 8 per-bone rotation limits (degrees -> radians); missing file = joints stay fully stiff
local RAD = math.pi / 180
local function loadLimits()
    for _, p in ipairs({ "assets/local/genesis8_bone_limits.json", "local/genesis8_bone_limits.json" }) do
        local f = io.open(p)
        if f then
            local data = json.decode(f:read("*a"))
            f:close()
            local out = {}
            for name, ax in pairs(data) do
                -- Daz is left-handed: positive angles run opposite to the scene's right-handed axes, so flip all three
                out[name] = { x = { -ax.x[2] * RAD, -ax.x[1] * RAD },
                             y = { -ax.y[2] * RAD, -ax.y[1] * RAD },
                             z = { -ax.z[2] * RAD, -ax.z[1] * RAD } }
            end
            return out
        end
    end
    print("caps_fit: genesis8_bone_limits.json not found, joints stay fully stiff")
    return {}
end

-- Per-bone collision fits + joint table, computed at runtime from the loaded
-- skeleton: physics.fit_capsule on each bone's rest-pose vertex cloud. This
-- replaces the pre-baked table that solvers/update_caps_fit.sh used to generate.

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
local kRadiusScale = 0.8     -- shrink the fitted radii so shapes sit inside the skin
local kFitOptions = { profile_bins = 64, max_segments = 8, profile_percentile = 1.0, sphere_aspect = 5.0 }

-- group every skinned vertex into the cloud of each bone it weights onto (single pass)
-- returns clouds[jointIndex] = { pos = flat xyz, nrm = flat xyz (or nil), sumX/Y/Z }
local function boneClouds(skel)
    local clouds = {}
    for _, me in ipairs(skel.meshes) do
        local g = scene.component(me, "Geometry")
        print(me)
        if not g.pos then goto cont end
        -- read each array once: the component view re-converts on every field access
        local posArr, nrmArr, jArr, wArr = g.pos, g.nrm, g.joints, g.weights
        local nv = #posArr / 3
        if #jArr ~= 4 * nv or #wArr ~= 4 * nv then goto cont end
        for v = 1, nv do
            local base, pbase = (v - 1) * 4, (v - 1) * 3
            local seen = {}
            for slot = 0, 3 do
                local ji = jArr[base + slot + 1]
                if wArr[base + slot + 1] > kWeightThreshold and not seen[ji] then
                    seen[ji] = true
                    local c = clouds[ji]
                    if not c then
                        c = { pos = {}, nrm = {}, sumX = 0, sumY = 0, sumZ = 0 }
                        clouds[ji] = c
                    end
                    table.insert(c.pos, posArr[pbase + 1])
                    table.insert(c.pos, posArr[pbase + 2])
                    table.insert(c.pos, posArr[pbase + 3])
                    if nrmArr then
                        table.insert(c.nrm, nrmArr[pbase + 1])
                        table.insert(c.nrm, nrmArr[pbase + 2])
                        table.insert(c.nrm, nrmArr[pbase + 3])
                    end
                    c.sumX = c.sumX + posArr[pbase + 1]
                    c.sumY = c.sumY + posArr[pbase + 2]
                    c.sumZ = c.sumZ + posArr[pbase + 3]
                end
            end
        end
        ::cont::
    end
    return clouds
end

-- fit a tapered-capsule chain to each kBones' rest-pose vertex cloud
-- returns { name, com, caps = { { origin, axis, seg = { t1..t4 } } } }
local function buildFits(skel)
    local idxByName = {}
    for i, n in ipairs(skel.boneNames) do idxByName[n] = i end
    local clouds = boneClouds(skel)
    local fits = {}
    for _, name in ipairs(kBones) do
        local c = idxByName[name] and clouds[idxByName[name]]
        if not c or #c.pos < 9 then
            print("caps_fit: no vertex cloud for bone '" .. name .. "'")
        else
            local fit, err = physics.fit_capsule(c.pos, #c.nrm == #c.pos and c.nrm or nil, kFitOptions)
            if not fit then
                print("caps_fit: fit failed for '" .. name .. "': " .. tostring(err))
            else
                local caps = {}
                for _, p in ipairs(fit.parts) do
                    local seg = {}
                    for _, s in ipairs(p.tapers) do
                        seg[#seg + 1] = { s[1], s[2], s[3] * kRadiusScale, s[4] * kRadiusScale }
                    end
                    caps[#caps + 1] = { origin = p.origin, axis = p.axis, seg = seg }
                end
                local nV = #c.pos / 3
                fits[#fits + 1] = { name = name, com = { c.sumX / nV, c.sumY / nV, c.sumZ / nV }, caps = caps }
            end
        end
    end
    return fits
end

-- every joint: its nearest-joint ancestor + rest-pose world transform
local function buildJoints(skel)
    local jointSet, nameByJoint = {}, {}
    for i, j in ipairs(skel.joints) do
        jointSet[j] = true
        nameByJoint[j] = skel.boneNames[i]
    end
    local function jointParent(e)
        local p = scene.component(e, "Parent")
        while p and p.e do
            if jointSet[p.e] then return p.e end
            p = scene.component(p.e, "Parent")
        end
        return nil
    end
    local out = {}
    for i, j in ipairs(skel.joints) do
        local wp = scene.world_pose(j)
        local parent = jointParent(j)
        out[#out + 1] = { name = skel.boneNames[i],
                          parent = parent and nameByJoint[parent] or "",
                          pos = wp.pos, quat = wp.quat }
    end
    return out
end

function _on_ready(self)
    local ch = scene.entity("char")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
-- entity-form gltf load: skinned model (the table API supports skinning too; kept on the entity path for full parity)
    graphics.gltf(ch, "local/hashimoto.glb")
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 55, 1, 55 })) })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })
    -- static ground collider (kinematic = immovable)
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 55, 1, 55 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })
    local mat = scene.get_entity("mat_caps")
    local n, nj, nlim = 0, 0, 0
    local kLimits = loadLimits()
    -- per-bone collision fits + joint table from the loaded skeleton (rest pose);
    -- scoped to ch so other skeletons in the scene can't be picked up
    local skelEnt = scene.find_components("Skeleton", ch)[1]
    assert(skelEnt, "caps_fit: no skeleton under 'char'")
    local kFits = buildFits(scene.component(skelEnt, "Skeleton"))
    local kJoints = buildJoints(scene.component(skelEnt, "Skeleton"))
    -- "alive at rest": static friction holds the pose, a small zero-target position motor
    -- springs joints back to rest (muscle tone), linear damping settles the sway

    for _, f in ipairs(kFits) do
        -- one RigidBody per bone; its collision is the compound of the bone's tapered capsule segments
        local bone = scene.entity(f.name)
        boneEnt[f.name] = bone
        scene.replace(bone, "Transform", { pos = f.com })
        local cols = {}
        for c, cap in ipairs(f.caps) do
            local q = math.qfromto({ 0, 1, 0 }, cap.axis)
            for i, s in ipairs(cap.seg) do
                n = n + 1
                -- cap.origin is world space, the body origin is f.com: convert to bone-local
                local off = { cap.origin[1] - f.com[1] + cap.axis[1] * (s[1] + s[2]) * 0.5,
                             cap.origin[2] - f.com[2] + cap.axis[2] * (s[1] + s[2]) * 0.5,
                             cap.origin[3] - f.com[3] + cap.axis[3] * (s[1] + s[2]) * 0.5 }

                -- debug visual segments; follows the falling bone
                -- local e = scene.entity(f.name .. "_" .. c .. "_" .. i)
                -- scene.replace(e, "Transform", { pos = off, quat = q })
                -- scene.replace(e, "Parent", { e = bone })
                -- scene.replace(e, "Renderable", { geom = geom(mesh.taperedCapsule({
                --    halfHeight = (s[2] - s[1]) * 0.5, bottomRadius = s[3], topRadius = s[4] })) })
                -- scene.replace(e, "MaterialRef", { material = mat })

                -- collision segment; local offset from the bone origin
                local shape = scene.entity()
                scene.replace(shape, "TaperedCapsule", { halfHeight = (s[2] - s[1]) * 0.5,
                                                       bottomRadius = s[3], topRadius = s[4] })
                local col = scene.entity()
                scene.replace(col, "Transform", { pos = off, quat = q })
                scene.replace(col, "Collider", { shape = shape })
                cols[#cols + 1] = col
            end
        end
        scene.replace(bone, "RigidBody", { colliders = cols, forceSleep = false, damping = kBodyDamping, friction = kBodyFriction })
        -- reparent the bone the rigid body
        local parent = bone
        local child = scene.get_entity("hashimoto/" .. f.name)
        print(f.name)
        scene.add_child(parent, child, { keepWorld = true })
    end
    -- perfectly stiff 6DOF weld: each shaped bone is joined to its shaped parent at the
    -- bone's joint world position (kJoints); zero limits hold the rest-pose relative pose
    local jointBy = {}
    for _, j in ipairs(kJoints) do
        jointBy[j.name] = j
    end
    for name, bone in pairs(boneEnt) do
        local j = jointBy[name]
        local parent = j and boneEnt[j.parent]
        if j and parent then
            local bq = scene.world_pose(bone).quat
            local pq = scene.world_pose(parent).quat
            local relQuat = math.qmul(math.qinv(pq), bq)
            local lim = kLimits[j.name]
            if lim then
                nlim = nlim + 1
            end
            local w = scene.entity()
            scene.replace(w, "SixDofJoint", {
                bodyA = bone,
                bodyB = parent,
                relQuat = relQuat,
                axisQuat = j.quat,
                grabPos = j.pos,
                excludeCollision = 1,
                limOn = 1,
                pyramid = 1,
                limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 },
                limAngMin = lim and { lim.x[1], lim.y[1], lim.z[1] } or { 0, 0, 0 },
                limAngMax = lim and { lim.x[2], lim.y[2], lim.z[2] } or { 0, 0, 0 },
                motorState = { 0, 0, 0, 2, 2, 2 }, -- position motor on the rotation axes only
                motorFreq = { kToneFreq, kToneFreq, kToneFreq },
                motorDamp = { kToneDamp, kToneDamp, kToneDamp },
                frictionAng = { kGripTorque, kGripTorque, kGripTorque } })
            joints[#joints + 1] = w
            nj = nj + 1
            -- joint debug axis: parented to bone at the joint point, showing the LIMIT frame
            -- (axisQuat = j.quat at creation; the frame follows body A as it moves)
            -- local bp = scene.world_pose(bone).pos
            -- local ax = mesh.createAxis({ len = 0.15 })
            -- scene.replace(ax, "Transform", { pos = math.qrot(math.qinv(bq), { j.pos[1] - bp[1], j.pos[2] - bp[2], j.pos[3] - bp[3] }),
            --                                quat = math.qmul(math.qinv(bq), j.quat) })
            -- scene.replace(ax, "Parent", { e = bone })
        end
    end

    local w = scene.entity()
    local bq = scene.world_pose(boneEnt["pelvis"]).quat
    local pq = scene.world_pose(boneEnt["abdomenLower"]).quat
    local relQuat = math.qmul(math.qinv(pq), bq)
    local lim = kLimits["pelvis"]
    if lim then
        nlim = nlim + 1
    end
    scene.replace(w, "SixDofJoint", {
        bodyA = boneEnt["pelvis"],
        bodyB = boneEnt["abdomenLower"],
        relQuat = relQuat,
        axisQuat = jointBy["hip"].quat,
        grabPos = jointBy["hip"].pos,
        excludeCollision = 1,
        limOn = 1,
        pyramid = 1,
        limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 },
        limAngMin = lim and { lim.x[1], lim.y[1], lim.z[1] } or { 0, 0, 0 },
        limAngMax = lim and { lim.x[2], lim.y[2], lim.z[2] } or { 0, 0, 0 },
        motorState = { 0, 0, 0, 2, 2, 2 }, -- position motor on the rotation axes only
        motorFreq = { kToneFreq, kToneFreq, kToneFreq },
        motorDamp = { kToneDamp, kToneDamp, kToneDamp },
        frictionAng = { kGripTorque, kGripTorque, kGripTorque } })
    joints[#joints + 1] = w
    -- local bp = scene.world_pose(boneEnt["pelvis"]).pos
    -- local ax = mesh.createAxis({ len = 0.15 })
    -- scene.replace(ax, "Transform", { pos = math.qrot(math.qinv(bq), { jointBy["hip"].pos[1] - bp[1], jointBy["hip"].pos[2] - bp[2], jointBy["hip"].pos[3] - bp[3] }),
    --                                quat = math.qmul(math.qinv(bq), jointBy["hip"].quat) })
    -- scene.replace(ax, "Parent", { e = boneEnt["pelvis"] })

    -- hack to fix things that have "hip" weights (some grafts)
    scene.add_child(boneEnt["pelvis"], scene.get_entity("hashimoto/hip"), { keepWorld = true })


    --- add collision exceptions
    physics.collision_exception(boneEnt["rThighTwist"], boneEnt["pelvis"])          -- ignore
    physics.collision_exception(boneEnt["lThighTwist"], boneEnt["pelvis"])          -- ignore

    physics.collision_exception(boneEnt["pelvis"], boneEnt["abdomenUpper"])          -- ignore
    physics.collision_exception(boneEnt["abdomenLower"], boneEnt["chestLower"])          -- ignore
    physics.collision_exception(boneEnt["abdomenUpper"], boneEnt["chestUpper"])          -- ignore


    print("caps_fit: " .. n .. " capsule segments in " .. #kFits .. " bone bodies, " .. nj .. " joints (" .. nlim .. " limited)")


    -- the ui: one window, a text line, sliders, and the scene editor
    local win = uib.make_window("ragdoll demo", 320, 200, { 0, 1.5, 2.8 })
    local tab = uib.tab(win, "scene")
    uib.text(tab, "Rig settings")
    local onChange = "caps.LuaScript.applyTone" -- url for function to call when slider changed
    uib.bind_slider(tab, "kToneFreq", "caps.LuaScript.kToneFreq", 0, 100.0, "%.2f", onChange)
    uib.bind_slider(tab, "kToneDamp", "caps.LuaScript.kToneDamp", 0, 500.0, "%.1f", onChange)
    uib.bind_slider(tab, "kGripTorque", "caps.LuaScript.kGripTorque", 0, 100.0, "%.2f", onChange)
    uib.bind_slider(tab, "kBodyDamping", "caps.LuaScript.kBodyDamping", 0, 100.0, "%.2f", onChange)
    uib.bind_slider(tab, "kBodyFriction", "caps.LuaScript.kBodyFriction", 0, 100.0, "%.2f", onChange)
    uib.editor(uib.tab(win, "editor"))
    self.metricsText = uib.text(uib.make_window("metrics", 160, 180, { 0.45, 1.72, 2.8 }), "")
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
    for _, b in pairs(boneEnt) do
        local rb = scene.component(b, "RigidBody")
        rb.damping = kBodyDamping
        rb.friction = kBodyFriction
    end
end

function _physics(self, dt)

end

