-- char diag: isolate the floating/drag issues.
-- mode A (default): no input, does the capsule rest at 0.8 or float?
-- mode B (DIAG=1): ghost sphere parked far away (no possible self-collision) + walk -Z for 240 ticks
local tick = 0
local DIAG = tonumber(os.getenv("DIAG") or "0")

function _on_ready(self)
    if DIAG == 1 or DIAG == 2 then
        -- park the ghost's sphere far away: self-collision becomes impossible
        scene.replace(scene.get_entity("char_ghost"), "Transform", { pos = { 0, 50, 0 } })
    end
end

function _physics(self, dt)
    tick = tick + 1
    local dir = { 0, 0 }
    if (DIAG == 1 or DIAG == 2) and tick <= 240 then
        dir = { 0, -1 }
    end
    local gp = scene.get_pose("char_ghost")
    local rp = scene.get_pose("char_rb")
    -- mode 1: re-sync + step (the controller design); mode 2: ghost walks free (no re-sync)
    local pos
    if DIAG == 2 then
        pos = { gp.pos[1], gp.pos[2], gp.pos[3] }
    else
        pos = { rp.pos[1], gp.pos[2], rp.pos[3] }
    end
    local len = math.sqrt(dir[1] * dir[1] + dir[2] * dir[2])
    if len > 0 then
        local step = 4.0 * dt
        pos[1] = pos[1] + dir[1] / len * step
        pos[3] = pos[3] + dir[2] / len * step
    end
    scene.replace(scene.get_entity("char_ghost"), "Transform", { pos = pos, quat = gp.quat })
    if tick % 40 == 0 or (DIAG > 0 and tick >= 235 and tick <= 285 and tick % 5 == 0) then
        print(string.format("DIAG mode=%s t=%d rb=(%.3f,%.3f,%.3f) ghost=(%.3f,%.3f,%.3f)", tostring(DIAG), tick, rp.pos[1], rp.pos[2], rp.pos[3], gp.pos[1], gp.pos[2], gp.pos[3]))
    end
    if tick == 300 then
        t.done()
    end
end
