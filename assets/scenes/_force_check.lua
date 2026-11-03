local M = {}
local tick = 0
local maxImpY = -1e9

local function y(name)
    return scene.get_pose(name).pos[2]
end

function M._on_ready(self)
    local shape = scene.entity()
    scene.replace(shape, "BoxShape", { half = { 0.5, 0.5, 0.5 } })
    local col = scene.entity()
    scene.replace(col, "Collider", { shape = shape })

    local imp = scene.entity("imp")
    scene.replace(imp, "Transform", { pos = { 0, 5, 0 } })
    scene.replace(imp, "RigidBody", { mass = 1, colliders = { col } })

    local frc = scene.entity("frc")
    scene.replace(frc, "Transform", { pos = { 5, 5, 0 } })
    scene.replace(frc, "RigidBody", { mass = 1, colliders = { col } })

    -- one-shot impulse launch on 'imp'
    scene.set("imp.RigidBody.impulse", { 0, 30, 0 })
    -- continuous upward force on 'frc'
    scene.set("frc.RigidBody.force", { 0, 50, 0 })

    print(string.format("FORCE_TEST t=0 imp_y=%.3f frc_y=%.3f", y("imp"), y("frc")))
end

function M._physics(self, dt)
    tick = tick + 1
    if tick % 30 == 0 then
        print(string.format("FORCE_TEST t=%d imp_y=%.3f frc_y=%.3f", tick, y("imp"), y("frc")))
    end
    local iy = y("imp")
    if iy > maxImpY then maxImpY = iy end
    if tick == 240 then
        local ir = scene.get("imp.RigidBody.impulse")
        print(string.format("FORCE_TEST done max_imp_y=%.3f (expect >5) imp_readback={%.3f,%.3f,%.3f} (expect 0,0,0) frc_y=%.3f (rising)",
            maxImpY, ir[1], ir[2], ir[3], y("frc")))
    end
end

return M
