-- ragdoll.lua -- shared building blocks for per-bone rigid-body ragdolls.
--
-- A "ragdoll" here means: one RigidBody per skeleton bone, collision fitted at load time
-- from the bone's rest-pose vertex cloud (physics.fit_capsule), and SixDofJoints welding
-- each bone to its skeleton parent ("alive at rest": zero limits hold the pose, a small
-- zero-target position motor springs joints back to rest = muscle tone).
--
-- This module only provides building blocks; it creates nothing on require. Scene scripts
-- compose them: buildFits + makeBoneBody for the bodies, buildJoints + makeWeld for the
-- articulation. Sub-ragdolls (hands.lua) attach to a base ragdoll by resolving each bone's
-- skeleton parent against the base ragdoll's body table.

local json = require "json"

local M = {}

M.RAD = math.pi / 180

-- wrap a geometry table in a Geometry entity (Renderable.geom takes an entity)
function M.geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

-- Daz Genesis 8 per-bone rotation limits (degrees -> radians); missing file = joints stay fully stiff.
-- paths: candidate file paths to try in order; tag: prefix for the warning print.
-- returns { boneName = { x = {min,max}, y = ..., z = ... } }
function M.loadLimits(paths, tag)
    for _, p in ipairs(paths) do
        local f = io.open(p)
        if f then
            local data = json.decode(f:read("*a"))
            f:close()
            local out = {}
            for name, ax in pairs(data) do
                -- Daz is left-handed: positive angles run opposite to the scene's right-handed axes, so flip all three
                out[name] = { x = { -ax.x[2] * M.RAD, -ax.x[1] * M.RAD },
                              y = { -ax.y[2] * M.RAD, -ax.y[1] * M.RAD },
                              z = { -ax.z[2] * M.RAD, -ax.z[1] * M.RAD } }
            end
            return out
        end
    end
    print(tag .. ": bone limits file not found, joints stay fully stiff")
    return {}
end

-- group every skinned vertex into the cloud of each bone it weights onto (single pass)
-- returns clouds[jointIndex] = { pos = flat xyz, nrm = flat xyz (or nil), sumX/sumY/sumZ },
-- keyed by the RAW glTF joint index from JOINTS_0 -- i.e. 0-based, so bone i of
-- skel.boneNames (1-based) is clouds[i - 1]

local function ends_with(s, suffix)
  return suffix == "" or s:sub(-#suffix) == suffix
end

local function valid_material(meshMaterialsCollisionShapes, materialName)
    for _, v in ipairs(meshMaterialsCollisionShapes) do
        if ends_with(materialName, "/" .. v) then return true end
    end
    return false
end

function M.boneClouds(skel, weightThreshold, meshMaterialsCollisionShapes)
    local clouds = {}
    for _, me in ipairs(skel.meshes) do
        local g = scene.component(me, "Geometry")
        if meshMaterialsCollisionShapes ~= nil then
            if not valid_material(meshMaterialsCollisionShapes, scene.name(scene.component(me, "MaterialRef").material)) then 
                goto cont 
            end
        end
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
                if wArr[base + slot + 1] > weightThreshold and not seen[ji] then
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

-- fit a tapered-capsule compound to each named bone's rest-pose vertex cloud.
-- opts: { tag = "ragdoll", weightThreshold = 0.3, radiusScale = 1.0, fitOptions = {}, fitter = fn }
--       (fitOptions are passed straight to physics.fit_capsule; max_clusters = 1 gives
--        one collider per bone)
--       fitter: optional (name, cloud) -> fitTable|nil; when it returns a table that result
--       is used as-is (no radius scaling -- the fitter applies its own), otherwise the
--       default physics.fit_capsule path runs. A nil return therefore falls back cleanly.
-- returns an array of { name, com = vec3, caps = { { origin, axis, seg = { {s0,s1,r0,r1}, ... } } } }
function M.buildFits(skel, boneNames, meshMaterialsCollisionShapes, opts)
    opts = opts or {}
    local tag = opts.tag or "ragdoll"
    local radiusScale = opts.radiusScale or 1.0
    local fitOptions = opts.fitOptions or {}
    local idxByName = {}
    for i, n in ipairs(skel.boneNames) do idxByName[n] = i end
    local clouds = M.boneClouds(skel, opts.weightThreshold or 0.3, meshMaterialsCollisionShapes)
    local fits = {}
    for _, name in ipairs(boneNames) do
        -- clouds is keyed by the raw (0-based) glTF joint index; idxByName is 1-based
        local c = idxByName[name] and clouds[idxByName[name] - 1]
        if not c or #c.pos < 9 then
            print(tag .. ": no vertex cloud for bone '" .. name .. "'")
        else
            local fit = opts.fitter and opts.fitter(name, c)
            if type(fit) ~= "table" then
                local capFit, err = physics.fit_capsule(c.pos, #c.nrm == #c.pos and c.nrm or nil, fitOptions)
                if not capFit then
                    print(tag .. ": fit failed for '" .. name .. "': " .. tostring(err))
                else
                    local caps = {}
                    for _, p in ipairs(capFit.parts) do
                        local seg = {}
                        for _, s in ipairs(p.tapers) do
                            seg[#seg + 1] = { s[1], s[2], s[3] * radiusScale, s[4] * radiusScale }
                        end
                        caps[#caps + 1] = { origin = p.origin, axis = p.axis, seg = seg }
                    end
                    local nV = #c.pos / 3
                    fit = { name = name, com = { c.sumX / nV, c.sumY / nV, c.sumZ / nV }, caps = caps }
                end
            end
            if fit then
                fits[#fits + 1] = fit
            end
        end
    end
    return fits
end

-- every joint: its nearest-joint ancestor + rest-pose world transform.
-- returns an array of { name, parent (bone name or ""), pos, quat }
function M.buildJoints(skel)
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

-- create the RigidBody entity for one fitted bone: Transform at the cloud's center of mass,
-- one Collider per fitted taper segment (local offset from the body origin), plus optional
-- debug visuals that follow the falling bone.
-- opts: { material = entity|nil, debugVisuals = bool, damping = 0, friction = 1 }
function M.makeBoneBody(fit, opts)
    opts = opts or {}
    local body = scene.entity(fit.name)
    scene.replace(body, "Transform", { pos = fit.com })
    local cols = {}
    for c, cap in ipairs(fit.caps) do
        local q = math.qfromto({ 0, 1, 0 }, cap.axis)
        for i, s in ipairs(cap.seg) do
            -- cap.origin is world space, the body origin is fit.com: convert to bone-local,
            -- then shift half the segment length along the axis (TaperedCapsule is centered on its own origin)
            local mid = (s[1] + s[2]) * 0.5
            local off = { cap.origin[1] - fit.com[1] + cap.axis[1] * mid,
                          cap.origin[2] - fit.com[2] + cap.axis[2] * mid,
                          cap.origin[3] - fit.com[3] + cap.axis[3] * mid }

            if opts.debugVisuals then
                local e = scene.entity(fit.name .. "_" .. c .. "_" .. i)
                scene.replace(e, "Transform", { pos = off, quat = q })
                scene.replace(e, "Parent", { e = body })
                scene.replace(e, "Renderable", { geom = M.geom(mesh.taperedCapsule({
                    halfHeight = (s[2] - s[1]) * 0.5, bottomRadius = s[3], topRadius = s[4] })) })
                if opts.material then
                    scene.replace(e, "MaterialRef", { material = opts.material })
                end
            end

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
    scene.replace(body, "RigidBody", { 
        colliders = cols, 
        forceSleep = false,
        damping = opts.damping or 0, 
        friction = opts.friction or 1,
        mass = opts.mass or 0
    })
    return body
end

-- reparent the character's skinned bone under the rigid body (world pose preserved), so the
-- skin follows the falling body; no-op when the gltf bone is missing
function M.attachBone(body, charBone)
    if charBone then
        scene.add_child(body, charBone, { keepWorld = true })
    end
end

-- weld child to parent at the joint point: zero translation limits hold the rest-pose
-- relative pose (relQuat from the current world quats), angular limits clamp motion in the
-- rest-pose joint frame (axisQuat), and a position motor on the rotation axes springs back
-- to rest (muscle tone).
-- jinfo: { pos, quat } from buildJoints; lim: { x = {min,max}, ... }|nil; tone: { freq, damp, grip }
function M.makeWeld(child, parent, jinfo, lim, tone)
    local bq = scene.world_pose(child).quat
    local pq = scene.world_pose(parent).quat
    local w = scene.entity()
    scene.replace(w, "SixDofJoint", {
        bodyA = child,
        bodyB = parent,
        relQuat = math.qmul(math.qinv(pq), bq),
        axisQuat = jinfo.quat,
        grabPos = jinfo.pos,
        excludeCollision = 1,
        limOn = 1,
        pyramid = 1,
        limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 },
        limAngMin = lim and { lim.x[1], lim.y[1], lim.z[1] } or { 0, 0, 0 },
        limAngMax = lim and { lim.x[2], lim.y[2], lim.z[2] } or { 0, 0, 0 },
        motorState = { 0, 0, 0, 2, 2, 2 }, -- position motor on the rotation axes only
        motorFreq = { tone.freq, tone.freq, tone.freq },
        motorDamp = { tone.damp, tone.damp, tone.damp },
        frictionAng = { tone.grip, tone.grip, tone.grip } })
    return w
end

function M.excludeCollisions(list)
    local es = {}
    for _, e in pairs(list) do
        es[#es + 1] = e
    end
    local n = 0
    for i = 1, #es do
        for j = 1, #es do
            if es[i] ~= es[j] then
                physics.collision_exception(es[i], es[j])
                n = n + 1
            end
        end
    end
    return n
end

-- sub-ragdoll defaults (hands/feet); pass opts to createSubRagdoll for per-call overrides
M.kToneFreq = 50.0        -- Hz; springback speed of the joint position motors (>~50 destabilizes at zeta=1)
M.kToneDamp = 50.0       -- damping ratio; 1 = critical (no oscillation)
M.kGripTorque = 10.0      -- N m max friction torque per rotation axis
M.kBodyDamping = 0.05    -- linear/angular damping of the sub-ragdoll bodies
M.kBodyFriction = 10.0   -- high friction: static grip holds the pose at rest
M.kWeightThreshold = 0.5 -- skin weight above which a vertex joins a bone's cloud
M.kRadiusScale = 1.05     -- shrink the fitted radii so shapes sit inside the skin
-- max_clusters = 1: one collider per bone (a single tapered capsule / sphere)
M.kFitOptions = { profile_bins = 64, max_segments = 1, profile_percentile = 1.0,
                  sphere_aspect = 5.0, max_clusters = 1 }
M.kDebugVisuals = false

local kLimitsPaths = { "assets/local/genesis8_bone_limits.json", "local/genesis8_bone_limits.json" }

-- sub-ragdoll for a named bone set (hands/feet): one RigidBody + weld per bone; parents outside
-- the set resolve against boneEnt. Returns { bodies = { l, r }, joints = { l, r }, jointsByName = { l, r } }
function M.createSubRagdoll(ch, char_prefix, boneEnt, boneNames, tag, opts, meshMaterialsCollisionShapes)
    opts = opts or {}
    local tone = { freq = opts.tone and opts.tone.freq or M.kToneFreq,
                   damp = opts.tone and opts.tone.damp or M.kToneDamp,
                   grip = opts.tone and opts.tone.grip or M.kGripTorque }

    local skelEnt = scene.find_components("Skeleton", ch)[1]
    assert(skelEnt, tag .. ": no skeleton under the character root")
    local skel = scene.component(skelEnt, "Skeleton")

    -- captured before any bodies exist: attachBone reparents the gltf bones under the new rigid
    -- bodies, so the skeleton-ancestor walk only works while the original hierarchy is intact
    local kLimits = M.loadLimits(kLimitsPaths, tag)
    local kJoints = M.buildJoints(skel)
    local jointBy = {}
    for _, j in ipairs(kJoints) do
        jointBy[j.name] = j
    end

    local kFits = M.buildFits(skel, boneNames, meshMaterialsCollisionShapes, {
        tag = tag,
        weightThreshold = opts.weightThreshold or M.kWeightThreshold,
        radiusScale = opts.radiusScale or M.kRadiusScale,
        fitOptions = opts.fitOptions or M.kFitOptions
    })
    -- one RigidBody per fitted bone, kept in two lists (left / right)
    local bodies, bodyBy = { l = {}, r = {} }, {}
    for _, f in ipairs(kFits) do
        local body = M.makeBoneBody(f, {
            material = opts.material,
            debugVisuals = opts.debugVisuals ~= nil and opts.debugVisuals or M.kDebugVisuals,
            damping = opts.damping or M.kBodyDamping,
            friction = opts.friction or M.kBodyFriction,
            mass = opts.mass or 0
         })
        bodyBy[f.name] = body
        bodies[f.name:sub(1, 1)][f.name] = body -- "l..." / "r..."
        -- reparent the character's skinned bone under the rigid body so the skin follows
        M.attachBone(body, scene.get_entity(char_prefix .. f.name))
    end

    local joints = { l = {}, r = {} }
    local jointsByName = { l = {}, r = {} }
    local toneHz = {} -- weld -> motorFreq; lets the scene keep a sub-rig's own tone when global sliders move
    for _, f in ipairs(kFits) do
        local j = jointBy[f.name]
        if not j then
            print(tag .. ": no skeleton joint for '" .. f.name .. "', left free")
        else
            local parentBody = bodies.l[j.parent] or bodies.r[j.parent] or boneEnt[j.parent]
            if not parentBody then
                print(tag .. ": '" .. f.name .. "' has no body for parent '" .. j.parent .. "', left free")
            else
                local sname = f.name:sub(1, 1)
                local w = M.makeWeld(bodyBy[f.name], parentBody, j, kLimits[f.name], tone)
                joints[sname][#joints[sname] + 1] = w
                jointsByName[sname][f.name] = w
                toneHz[w] = tone.freq
            end
        end
    end

    print(tag .. ": " .. #kFits .. " bones in " .. (#joints.l + #joints.r) .. " joints")

    return { bodies = bodies, joints = joints, jointsByName = jointsByName, toneHz = toneHz }
end

return M
