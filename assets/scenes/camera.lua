-- fly camera: WASD/QE + shift moves it, LMB drag rotates it. Attached to the camera object in
-- the scene json (self.entity is the camera) - always runs; in VR this camera is a separate view
-- object (the headset view is the "vr cam") and simply isn't the current camera.
local M = {}

local W, A, S, D, Q, E, SHIFT = 87, 65, 83, 68, 81, 69, 340

local camH, mouseH, kbH
local prevMX, prevMY = 0, 0
local kSpeed = 2.0
local kSens = 0.0025

function M._on_ready(self)
    camH = self.entity
end

function M._physics(self, dt)
    if not kbH then
        kbH = scene.get_entity("keyboard")
    end
    local t = scene.component(camH, "Transform")
    local pos, q = t.pos, t.quat
    local fwd = math.qrot(q, {0, 0, -1})
    local rgt = math.qrot(q, {1, 0, 0})
    local mv = {0, 0, 0}
    if input.key_down(kbH, W) then mv = math.vadd(mv, fwd) end
    if input.key_down(kbH, S) then mv = math.vadd(mv, math.vscale(fwd, -1)) end
    if input.key_down(kbH, D) then mv = math.vadd(mv, rgt) end
    if input.key_down(kbH, A) then mv = math.vadd(mv, math.vscale(rgt, -1)) end
    if input.key_down(kbH, E) then mv[2] = mv[2] + 1 end
    if input.key_down(kbH, Q) then mv[2] = mv[2] - 1 end
    if math.vlen(mv) > 0 then
        local speed = (input.key_down(kbH, SHIFT) and 6.0 or kSpeed) * dt
        local d = math.vscale(math.vnorm(mv), speed)
        pos = {pos[1] + d[1], pos[2] + d[2], pos[3] + d[3]}
    end
    if not mouseH then
        mouseH = scene.get_entity("mouse")
    end
    if mouseH then
        local mx = scene.get(mouseH, "MouseInput.x")
        local my = scene.get(mouseH, "MouseInput.y")
        if scene.get(mouseH, "MouseInput.button_0") > 0.5 then
            local dqy = math.qaxis({0, 1, 0}, -(mx - prevMX) * kSens)
            local dqx = math.qaxis({1, 0, 0}, -(my - prevMY) * kSens)
            q = math.qmul(dqy, math.qmul(q, dqx))
        end
        prevMX, prevMY = mx, my
    end
    scene.replace(camH, "Transform", { pos = pos, quat = q })
end

return M
