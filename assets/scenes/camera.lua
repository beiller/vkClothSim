-- fly camera: WASD/QE + shift moves it, LMB drag rotates it. Attached to the camera object in
-- the scene json (self.entity is the camera) - always runs; in VR this camera is a separate view
-- object (the headset view is the "vr cam") and simply isn't the current camera.
local M = {}

local W, A, S, D, Q, E, SHIFT = 87, 65, 83, 68, 81, 69, 340

local camH, mouseH, kbH
local prevMX, prevMY = 0, 0
local kSpeed = 2.0
local kSens = 0.0025

local function camQuat()
    return {scene.get(camH, "qx"), scene.get(camH, "qy"), scene.get(camH, "qz"), scene.get(camH, "qw")}
end

function M._on_ready(self)
    camH = self.entity
end

function M._physics(self, dt)
    if not kbH then
        kbH = scene.find("keyboard")
    end
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
        local speed = (scene.key_down(kbH, SHIFT) and 6.0 or kSpeed) * dt
        local d = scene.vscale(scene.vnorm(mv), speed)
        local pos = scene.get(camH, "pos")
        scene.set(camH, "pos", {pos[1] + d[1], pos[2] + d[2], pos[3] + d[3]})
    end
    if not mouseH then
        mouseH = scene.find("mouse")
    end
    if mouseH then
        local mx = scene.get(mouseH, "x")
        local my = scene.get(mouseH, "y")
        if scene.get(mouseH, "button_0") > 0.5 then
            local dqy = scene.qaxis({0, 1, 0}, -(mx - prevMX) * kSens)
            local dqx = scene.qaxis({1, 0, 0}, -(my - prevMY) * kSens)
            local nq = scene.qmul(dqy, scene.qmul(q, dqx))
            scene.set(camH, "qx", nq[1])
            scene.set(camH, "qy", nq[2])
            scene.set(camH, "qz", nq[3])
            scene.set(camH, "qw", nq[4])
        end
        prevMX, prevMY = mx, my
    end
end

return M
