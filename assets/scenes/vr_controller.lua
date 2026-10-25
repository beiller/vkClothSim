-- VR controller (right hand): the stick walks the "vr root" anchor in the VR head's ("vr cam")
-- frame; the trigger grabs/releases: a Jolt point joint for dynamic bodies, an in-place
-- parent (and restore) to the hand anchor for kinematic ones. The grab anchor is a kinematic
-- capsule this script drives from the aim pose - the rigid step pulls kinematic poses from
-- the ECS before each step, so a pose written in _physics is live for that step.
-- Always runs - without a headset the controller is simply inactive and nothing moves.
local M = {}

local ctrlH, ctrlL, rootH, headH
local handAnchor = nil -- kinematic grab anchor (right hand); created on the first physics step
local joint = nil
local kinGrab = nil -- { body, origParent, origPos, origQuat } for a parented kinematic grab
local marker = nil
local markerMat = nil
local laserMatR = nil
local laserMatL = nil
local markerGeom = nil
local laserGeom = nil
local anchorShape = nil
local lasers = {} -- hand -> laser entity, parented to that hand's grip mesh
local kSpeed = 2.0
local kDead = 0.12
local kGrabRange = 3.0
local kMarkerRadius = 0.03
local kLaserLen = 10.0
local kLaserHalf = 0.0005

-- stdlib geometry tables are the Geometry struct itself: stash one in a geometry entity
local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

-- a PBR material as a pure-data entity (shared by ref, edited in place)
local function material(name, baseColor, metallic, roughness, emissive)
    local e = scene.entity(name)
    local m = { baseColor = baseColor, metallic = metallic or 0, roughness = roughness or 0.5 }
    if emissive then
        m.emissive = emissive
    end
    scene.replace(e, "Material", m)
    return e
end

-- grabbable bodies are RigidBody (all rigid bodies are dynamic) or KinematicBody
-- (grabbed by parenting in place - the parent chain drives the kinematic pose)
local function isDynamic(e)
    return scene.component(e, "RigidBody") ~= nil
end

local function isKinematic(e)
    return scene.component(e, "KinematicBody") ~= nil
end

function M._on_ready(self)
    markerMat = material("grab_marker", {1, 0.1, 0.1}, 0.0, 0.35)
    markerGeom = geom(mesh.sphere({ radius = kMarkerRadius, subdiv = 2 }))
    -- lasers: one very thin box per hand along its aim dir (aim = -z), lit by HDR emissive
    laserGeom = geom(mesh.box({kLaserHalf, kLaserHalf, kLaserLen / 2}))
    laserMatR = material("laser R", {1, 0.05, 0.05}, 0, 1, {3, 0.05, 0.05})
    laserMatL = material("laser L", {0.05, 0.8, 1}, 0, 1, {0.05, 2.5, 3})
    anchorShape = scene.entity()
    scene.replace(anchorShape, "CapsuleShape", { halfLen = 0.06, radius = 0.03 })
end

-- place a small sphere on the grabbed capsule, at the joint anchor (Parent system)
local function spawnMarker(capEnt, hitPos)
    if marker then
        scene.destroy(marker)
        marker = nil
    end
    local ct = scene.component(capEnt, "Transform")
    local capPos, capQ = ct.pos, ct.quat
    -- joint anchor (world hitPos) in the capsule's local frame
    local localPos = scene.qrot(scene.qinv(capQ), scene.vsub(hitPos, capPos))
    marker = scene.entity()
    scene.replace(marker, "Transform", { pos = localPos })
    scene.replace(marker, "Renderable", { geom = markerGeom })
    scene.replace(marker, "MaterialRef", { material = markerMat })
    scene.add_child(capEnt, marker)
end

local function release()
    if joint then
        scene.destroy(joint)
        joint = nil
    end
    if kinGrab then
        -- unparent, keeping the body's current world pose:
        -- with an original parent: keepWorld re-expresses it in the parent's frame
        -- without: the local is in the anchor frame, so convert it to world first
        -- (the detached body then freezes in the air where it was let go)
        if kinGrab.origParent then
            scene.add_child(kinGrab.origParent, kinGrab.body, { keepWorld = true })
        else
            local at = scene.component(handAnchor, "Transform")
            local rt = scene.component(rootH, "Transform")
            local bt = scene.component(kinGrab.body, "Transform")
            scene.replace(kinGrab.body, "Transform", {
                pos = scene.vadd(scene.vadd(rt.pos, scene.qrot(rt.quat, at.pos)),
                               scene.qrot(scene.qmul(rt.quat, at.quat), bt.pos)),
                quat = scene.qmul(scene.qmul(rt.quat, at.quat), bt.quat),
            })
            scene.remove(kinGrab.body, "Parent")
        end
        kinGrab = nil
    end
    if marker then
        scene.destroy(marker)
        marker = nil
    end
end

-- stick walks the vr root in the head's flattened XZ frame. Render-rate (called from _animate,
-- NOT _physics): the root is the visual locomotion anchor - stepping it at the physics rate
-- makes the in-hand view (head + hand parented under it) alias against the render and jitter
local function walk(dt)
    local sx, sy = scene.get(ctrlH, "axis_0"), scene.get(ctrlH, "axis_1")
    if sx * sx + sy * sy < kDead * kDead then
        return
    end
    local q = scene.component(headH, "Transform").quat
    local fwd = scene.qrot(q, {0, 0, -1})
    local rgt = scene.qrot(q, {1, 0, 0})
    fwd[2] = 0
    rgt[2] = 0
    local d = scene.vadd(scene.vscale(scene.vnorm(fwd), sy), scene.vscale(scene.vnorm(rgt), sx))
    local p = scene.component(rootH, "Transform").pos
    scene.replace(rootH, "Transform", { pos = {p[1] + d[1] * kSpeed * dt, 0, p[3] + d[3] * kSpeed * dt} })
end

-- the grab anchor: a kinematic capsule under the vr root, so its local pose = the raw aim
-- pose (walkPos comes from the parent chain). Created on first use: the controller entities
-- don't exist until the engine spawns them, after scene load
local function ensureAnchor()
    if handAnchor then
        return
    end
    handAnchor = scene.entity("hand anchor")
    scene.replace(handAnchor, "Transform", {})
    scene.add_child(rootH, handAnchor)
    local aCol = scene.entity()
    scene.replace(aCol, "Collider", { shape = anchorShape })
    scene.replace(handAnchor, "KinematicBody", { colliders = { aCol } })
end

-- per step: drive the anchor from the right hand's aim pose (inactive: it stays put)
local function poseAnchor()
    if scene.get(ctrlH, "active") < 0.5 then
        return
    end
    local ap = scene.get(ctrlH, "aim_pos")
    local aq = {scene.get(ctrlH, "aim_qx"), scene.get(ctrlH, "aim_qy"),
                scene.get(ctrlH, "aim_qz"), scene.get(ctrlH, "aim_qw")}
    scene.replace(handAnchor, "Transform", { pos = ap, quat = aq })
end

local function grab()
    local trig = scene.get(ctrlH, "trigger_0")
    local active = scene.get(ctrlH, "active")
    if active < 0.5 then
        release()
        return
    end
    if joint or kinGrab then
        if trig < 0.5 then
            release()
        end
        return
    end
    if trig > 0.5 then
        local rootPos = scene.component(rootH, "Transform").pos
        local aimPos = scene.get(ctrlH, "aim_pos")
        local aimWorld = {rootPos[1] + aimPos[1], rootPos[2] + aimPos[2], rootPos[3] + aimPos[3]}
        local aq = {
            scene.get(ctrlH, "aim_qx"), scene.get(ctrlH, "aim_qy"),
            scene.get(ctrlH, "aim_qz"), scene.get(ctrlH, "aim_qw"),
        }
        local fwd = scene.qrot(aq, {0, 0, -1})
        -- exclude the kinematic anchor: the ray starts at its center and a convex self-hit
        -- would otherwise always win at fraction 0
        local hit = physics.raycast(aimWorld, fwd, kGrabRange, { handAnchor })
        if hit and hit.entity and isDynamic(hit.entity) then
            local j = scene.entity()
            scene.replace(j, "Joint", { bodyA = handAnchor, bodyB = hit.entity, type = 0, grabPos = hit.pos })
            scene.add_child(handAnchor, j)
            joint = j
            spawnMarker(hit.entity, hit.pos)
        elseif hit and hit.entity and isKinematic(hit.entity) then
            -- parent in place: the local pose is frozen relative to the anchor, so the
            -- body follows the hand as the anchor moves (kinematic pose = parent chain)
            local ct = scene.component(hit.entity, "Transform")
            local pp = scene.component(hit.entity, "Parent")
            kinGrab = {
                body = hit.entity,
                origParent = pp and pp.e or nil,
                origPos = ct.pos,
                origQuat = ct.quat,
            }
            scene.add_child(handAnchor, hit.entity, { keepWorld = true })
            spawnMarker(hit.entity, hit.pos)
        end
    end
end

-- pose of the beam (box centered on the aim ray) in the grip's frame: the aim and grip poses
-- share the vr root's frame, so the local transform is grip^-1 * aim
local function laserPoseInGrip(ctrl)
    local grip = scene.component(ctrl, "Transform")
    local gripInv = scene.qinv(grip.quat)
    local aimPos = scene.get(ctrl, "aim_pos")
    local aimQ = {scene.get(ctrl, "aim_qx"), scene.get(ctrl, "aim_qy"), scene.get(ctrl, "aim_qz"),
                  scene.get(ctrl, "aim_qw")}
    local fwd = scene.qrot(aimQ, {0, 0, -1})
    local center = scene.vadd(aimPos, scene.vscale(fwd, kLaserLen / 2))
    return scene.qrot(gripInv, scene.vsub(center, grip.pos)), scene.qmul(gripInv, aimQ)
end

local function updateLaser(hand, ctrl, matEnt)
    if not ctrl or scene.get(ctrl, "active") < 0.5 then
        if lasers[hand] then
            scene.destroy(lasers[hand])
            lasers[hand] = nil
        end
        return
    end
    if not lasers[hand] then
        local e = scene.entity()
        scene.replace(e, "Transform", {})
        scene.replace(e, "Renderable", { geom = laserGeom, noShadow = true })
        scene.replace(e, "MaterialRef", { material = matEnt })
        scene.add_child(ctrl, e)
        lasers[hand] = e
    end
    local pos, quat = laserPoseInGrip(ctrl)
    scene.replace(lasers[hand], "Transform", {pos = pos, quat = quat})
end

-- per render frame: locomotion (the visual anchor steps at render rate) + laser tracking
function M._animate(self, dt)
    if not ctrlH then
        ctrlH = scene.find("controller R")
    end
    if not ctrlL then
        ctrlL = scene.find("controller L")
    end
    if not rootH then
        rootH = scene.find("vr root")
    end
    if not headH then
        headH = scene.find("vr cam")
    end
    if ctrlH and rootH and headH then
        walk(dt)
    end
    updateLaser("R", ctrlH, laserMatR)
    updateLaser("L", ctrlL, laserMatL)
end

function M._physics(self, dt)
    if not ctrlH or not rootH or not headH then
        return
    end
    ensureAnchor()
    poseAnchor()
    grab()
end

return M
