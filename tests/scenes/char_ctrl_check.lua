-- char_ctrl headless check: walk, wall (no pass-through / no push-back / ghost re-sync), jump +
-- grounded state machine, step-up. dt = 1/120 per frame (kRigidHz).
local cc = require("scenes/char_ctrl")

local tick = 0
local yMax = -1e9

function _on_ready(self)
    cc.setup({ rb = scene.get_entity("char_rb"), ghost = scene.get_entity("char_ghost") })
end

local function p()
    return scene.get_pose("char_rb").pos
end

function _physics(self, dt)
    tick = tick + 1
    local dir, jump = { 0, 0 }, false
    if tick <= 120 then
        dir = { 0, -1 } -- walk forward (-Z)
    elseif tick <= 340 then
        dir = { 1, 0 } -- into the wall at x=4.5
    elseif tick <= 460 then
        dir = { -1, 0 } -- back off
    elseif tick <= 470 then
        jump = true -- stationary jump (fires once: grounded flips)
    elseif tick <= 760 then
        if tick > 620 then
            dir = { 1, 0 } -- step-up over the 0.25 riser
        end
    end
    local r = cc.update(dt, dir, jump)
    local pos = p()
    if pos[2] > yMax then
        yMax = pos[2]
    end
    if tick % 60 == 0 then
        local gp = scene.get_pose("char_ghost").pos
        print(string.format("DBG t=%d rb=(%.2f,%.2f,%.2f) ghost=(%.2f,%.2f,%.2f) grounded=%s", tick, pos[1], pos[2], pos[3],
            gp[1], gp[2], gp[3], tostring(r.grounded)))
    end

    if tick == 120 then
        t.check("walk: moved -Z", pos[3] < -3.0)
        t.eq("walk: on ground", pos[2], 0.8, 0.15)
        t.check("walk: grounded", r.grounded)
    end
    if tick == 340 then
        t.check("wall: no pass-through", pos[1] < 4.3)
        t.check("wall: held at wall (no push-back)", pos[1] > 3.8)
        t.eq("wall: settled on ground", pos[2], 0.8, 0.15)
        local gp = scene.get_pose("char_ghost").pos
        t.check("resync: ghost xz tracks capsule", math.abs(gp[1] - pos[1]) < 0.1 and math.abs(gp[3] - pos[3]) < 0.1)
    end
    if tick == 460 then
        t.check("back off: moved away cleanly", pos[1] < 3.0 and pos[1] > -0.5)
        t.eq("back off: on ground", pos[2], 0.8, 0.15)
    end
    if tick == 500 then
        t.check("jump: airborne mid-ascent", not r.grounded)
    end
    if tick == 620 then
        t.check("jump: rose > 1.5", yMax > 1.5)
        t.eq("jump: landed on ground", pos[2], 0.8, 0.15)
        t.check("jump: grounded again", r.grounded)
    end
    if tick == 720 then
        t.check("step-up: on top of riser", pos[2] > 0.95)
        t.check("step-up: reached the step", pos[1] > 2.5)
    end
    if tick == 760 then
        t.check("step-up: crossed the step", pos[1] > 3.9)
    end
    if tick == 780 then
        print(string.format("CHAR_CTRL final x=%.2f y=%.2f z=%.2f yMax=%.2f", p()[1], p()[2], p()[3], yMax))
        t.done()
    end
end
