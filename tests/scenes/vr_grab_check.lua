-- VR grab is layer-2 only: a layer-1 body on the aim ray is ignored, the UI window behind it is grabbed
local uib = require("ui")

local canvas, blocker, anchor
local tick = 0

function _on_ready(self)
    -- fake headset entities so vr_controller runs headless: aim from the origin toward -Z at y=2
    scene.replace(scene.entity("vr root"), "Transform", {})
    scene.replace(scene.entity("vr cam"), "Transform", {})
    local cr = scene.entity("controller R")
    scene.replace(cr, "Transform", {})
    scene.replace(cr, "ControllerInput", { active = 1, trigger_0 = 1, aim_pos = { 0, 2, -2 } })

    -- layer-1 box (default layer) between the controller and the window
    local shape = scene.entity()
    scene.replace(shape, "BoxShape", { half = { 0.5, 0.5, 0.5 } })
    local col = scene.entity()
    scene.replace(col, "Collider", { shape = shape })
    blocker = scene.entity("blocker")
    scene.replace(blocker, "Transform", { pos = { 0, 2, -2.6 } })
    scene.replace(blocker, "KinematicBody", { colliders = { col } })

    local win = uib.make_window("t", 200, 200, { 0, 2, -3 }) -- layer-2 canvas slab behind the blocker
    canvas = scene.component(win, "UIWindow").canvas

    local s = scene.entity()
    scene.attach_script(s, "scenes/vr_controller")
end

function _physics(self, dt)
    tick = tick + 1
    if tick == 3 then -- grab lands on the first ticks after the anchor exists
        anchor = scene.get_entity("hand anchor")
        local cp = scene.component(canvas, "Parent")
        t.check("window (layer 2) is grabbed", cp and cp.e == anchor)
        t.check("blocker (layer 1) is ignored", scene.component(blocker, "Parent") == nil)
        scene.set("controller R.ControllerInput.trigger_0", 0) -- release
    end
    if tick == 10 then
        t.check("release unparents the window", scene.component(canvas, "Parent") == nil)
        local y = scene.world_pose(canvas).pos[2]
        t.check("window pose preserved after release (y~2)", math.abs(y - 2) < 0.01, tostring(y))
        t.done()
    end
end
