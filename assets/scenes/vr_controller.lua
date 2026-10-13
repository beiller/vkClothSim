-- VR controller (right hand): the stick walks the "vr root" anchor in the VR head's ("vr cam")
-- frame; the trigger grabs/releases a capsule via a Jolt point joint. Always runs - without a
-- headset the controller is simply inactive and nothing moves.
local M = {}

local ctrlH, rootH, headH
local joint = nil
local marker = nil
local markerGeom = nil
local kSpeed = 2.0
local kDead = 0.12
local kGrabRange = 3.0
local kMarkerRadius = 0.03

function M._on_ready(self)
    render.material("grab_marker", {1, 0.1, 0.1}, 0.0, 0.35)
    markerGeom = render.geometry(mesh.sphere({ radius = kMarkerRadius, subdiv = 2 }))
end

-- place a small sphere on the grabbed capsule, at the joint anchor (Parent system)
local function spawnMarker(capEnt, hitPos)
    if marker then
        scene.destroy(marker)
        marker = nil
    end
    local capPos = scene.get(capEnt, "pos")
    local capQ = {scene.get(capEnt, "qx"), scene.get(capEnt, "qy"), scene.get(capEnt, "qz"), scene.get(capEnt, "qw")}
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
    local q = {scene.get(headH, "qx"), scene.get(headH, "qy"), scene.get(headH, "qz"), scene.get(headH, "qw")}
    local fwd = scene.qrot(q, {0, 0, -1})
    local rgt = scene.qrot(q, {1, 0, 0})
    fwd[2] = 0
    rgt[2] = 0
    local d = scene.vadd(scene.vscale(scene.vnorm(fwd), sy), scene.vscale(scene.vnorm(rgt), sx))
    local p = scene.get(rootH, "pos")
    scene.set(rootH, "pos", {p[1] + d[1] * kSpeed * dt, 0, p[3] + d[3] * kSpeed * dt})
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
        local rootPos = scene.get(rootH, "pos")
        local aimPos = scene.get(ctrlH, "aim_pos")
        local aimWorld = {rootPos[1] + aimPos[1], rootPos[2] + aimPos[2], rootPos[3] + aimPos[3]}
        local aq = {
            scene.get(ctrlH, "aim_qx"), scene.get(ctrlH, "aim_qy"),
            scene.get(ctrlH, "aim_qz"), scene.get(ctrlH, "aim_qw"),
        }
        local fwd = scene.qrot(aq, {0, 0, -1})
        local hit = physics.raycast(aimWorld, fwd, kGrabRange)
        if hit and hit.entity and physics.is_dynamic(hit.entity) then
            joint = physics.create_joint(ctrlH, hit.entity, "point", hit.pos)
            spawnMarker(hit.entity, hit.pos)
        end
    end
end

function M._physics(self, dt)
    if not ctrlH then
        ctrlH = scene.find("controller R")
    end
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
