-- VR controller (right hand): the stick walks the "vr root" anchor in the VR head's ("vr cam")
-- frame; the trigger grabs/releases a capsule via a Jolt point joint. Always runs - without a
-- headset the controller is simply inactive and nothing moves.
local M = {}

local ctrlH, ctrlL, rootH, headH
local joint = nil
local marker = nil
local markerGeom = nil
local laserGeom = nil
local lasers = {} -- hand -> laser entity, parented to that hand's grip mesh
local kSpeed = 2.0
local kDead = 0.12
local kGrabRange = 3.0
local kMarkerRadius = 0.03
local kLaserLen = 10.0
local kLaserHalf = 0.002

function M._on_ready(self)
    render.material("grab_marker", {1, 0.1, 0.1}, 0.0, 0.35)
    markerGeom = render.geometry(mesh.sphere({ radius = kMarkerRadius, subdiv = 2 }))
    -- lasers: one very thin box per hand along its aim dir (aim = -z), lit by HDR emissive
    laserGeom = render.geometry(mesh.box({kLaserHalf, kLaserHalf, kLaserLen / 2}))
    render.material("laser R", {1, 0.05, 0.05}, 0, 1, nil, {3, 0.05, 0.05})
    render.material("laser L", {0.05, 0.8, 1}, 0, 1, nil, {0.05, 2.5, 3})
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
    render.mesh(marker, markerGeom, "grab_marker", localPos, {0, 0, 0, 1})
    scene.add_child(capEnt, marker)
end

local function release()
    if joint then
        physics.destroy_joint(joint)
        joint = nil
    end
    if marker then
        scene.destroy(marker)
        marker = nil
    end
end

-- stick walks the vr root in the head's flattened XZ frame
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

local function grab()
    local trig = scene.get(ctrlH, "trigger_0")
    local active = scene.get(ctrlH, "active")
    if active < 0.5 then
        release()
        return
    end
    if joint then
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
        -- exclude the controller's own kinematic anchor: the ray starts at its center and a
        -- convex self-hit would otherwise always win at fraction 0
        local hit = physics.raycast(aimWorld, fwd, kGrabRange, { ctrlH })
        if hit and hit.entity and physics.is_dynamic(hit.entity) then
            joint = physics.create_joint(ctrlH, hit.entity, "point", hit.pos)
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

local function updateLaser(hand, ctrl, matName)
    if not ctrl or scene.get(ctrl, "active") < 0.5 then
        if lasers[hand] then
            scene.destroy(lasers[hand])
            lasers[hand] = nil
        end
        return
    end
    if not lasers[hand] then
        local e = scene.entity()
        render.mesh(e, laserGeom, matName, nil, nil, { noShadow = true })
        scene.add_child(ctrl, e)
        lasers[hand] = e
    end
    local pos, quat = laserPoseInGrip(ctrl)
    scene.replace(lasers[hand], "Transform", {pos = pos, quat = quat})
end

-- per render frame: the beam tracks the aim pose at render rate
function M._animate(self)
    if not ctrlH then
        ctrlH = scene.find("controller R")
    end
    if not ctrlL then
        ctrlL = scene.find("controller L")
    end
    updateLaser("R", ctrlH, "laser R")
    updateLaser("L", ctrlL, "laser L")
end

function M._physics(self, dt)
    if not rootH then
        rootH = scene.find("vr root")
    end
    if not headH then
        headH = scene.find("vr cam")
    end
    if not ctrlH or not rootH or not headH then
        return
    end
    walk(dt)
    grab()
end

return M
