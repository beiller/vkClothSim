-- VR controller (right hand) + non-VR fly camera.
-- In VR: stick drives the "vr root" (locomotion), trigger grabs/releases via a Jolt point joint.
-- In non-VR: LMB-drag look + WASD/QE fly camera on the active camera entity.

local M = {}

local W, A, S, D, Q, E, SHIFT = 87, 65, 83, 68, 81, 69, 340

local ctrlH, rootH, camH, mouseH, kbH
local prevMX, prevMY = 0, 0
local joint = nil
local marker = nil
local markerGeom = nil
local kSpeed = 2.0
local kDead = 0.12
local kSens = 0.0025
local kGrabRange = 3.0
local kMarkerRadius = 0.03

local function camQuat()
    return {scene.get(camH, "qx"), scene.get(camH, "qy"), scene.get(camH, "qz"), scene.get(camH, "qw")}
end

local function camSetQuat(q)
    scene.set(camH, "qx", q[1])
    scene.set(camH, "qy", q[2])
    scene.set(camH, "qz", q[3])
    scene.set(camH, "qw", q[4])
end

function M._on_ready(self)
    camH = scene.find("cam")
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

local function vrMove(dt)
    local sx = scene.get(ctrlH, "axis_0")
    local sy = scene.get(ctrlH, "axis_1")
    if sx * sx + sy * sy < kDead * kDead then
        return
    end
    local rootPos = scene.get(rootH, "pos")
    local q = camQuat()
    local fwd = scene.qrot(q, {0, 0, -1})
    local rgt = scene.qrot(q, {1, 0, 0})
    fwd[2] = 0
    rgt[2] = 0
    fwd = scene.vnorm(fwd)
    rgt = scene.vnorm(rgt)
    local d = scene.vscale(scene.vadd(scene.vscale(fwd, sy), scene.vscale(rgt, sx)), kSpeed * dt)
    scene.set(rootH, "pos", {rootPos[1] + d[1], 0, rootPos[3] + d[3]})
end

local function vrGrab()
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

local function flyCam(dt)
    if not mouseH then mouseH = scene.find("mouse") end
    if not kbH then kbH = scene.find("keyboard") end
    if not mouseH or not kbH then return end
    local mx = scene.get(mouseH, "x")
    local my = scene.get(mouseH, "y")
    local lmb = scene.get(mouseH, "button_0")
    if lmb > 0.5 then
        local q = camQuat()
        local dqy = scene.qaxis({0, 1, 0}, -(mx - prevMX) * kSens)
        local dqx = scene.qaxis({1, 0, 0}, -(my - prevMY) * kSens)
        camSetQuat(scene.qmul(dqy, scene.qmul(q, dqx)))
    end
    prevMX, prevMY = mx, my
    local pos = scene.get(camH, "pos")
    local q = camQuat()
    local fwd = scene.qrot(q, {0, 0, -1})
    local rgt = scene.qrot(q, {1, 0, 0})
    local mv = {0, 0, 0}
    if scene.key_down(kbH, W) then mv = scene.vadd(mv, fwd) end
    if scene.key_down(kbH, S) then mv = scene.vadd(mv, scene.vscale(fwd, -1)) end
    if scene.key_down(kbH, D) then mv = scene.vadd(mv, rgt) end
    if scene.key_down(kbH, A) then mv = scene.vadd(mv, scene.vscale(rgt, -1)) end
    if scene.key_down(kbH, E) then mv[2] = mv[2] + 1 end
    if scene.key_down(kbH, Q) then mv[2] = mv[2] - 1 end
    local len = scene.vlen(mv)
    if len > 0 then
        local speed = scene.key_down(kbH, SHIFT) and 6.0 or 2.0
        local d = scene.vscale(scene.vnorm(mv), speed * dt)
        scene.set(camH, "pos", {pos[1] + d[1], pos[2] + d[2], pos[3] + d[3]})
    end
end

function M._physics(self, dt)
    if scene.is_vr() then
        ctrlH = scene.find("controller R")
        rootH = scene.find("vr root")
        if not ctrlH or not rootH then
            return
        end
        vrMove(dt)
        vrGrab()
    else
        flyCam(dt)
    end
end

return M
