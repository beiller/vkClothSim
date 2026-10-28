-- verification scene: camera yawed +90 around Y; two canvas windows left/right of the ray
local M = {}

function M._on_ready(self)
    local wl = scene.ui_window("LEFT", 290, 120)
    local wr = scene.ui_window("RIGHT", 290, 120)
    scene.ui_button(wl, "LEFT", function() end)
    scene.ui_button(wr, "RIGHT", function() end)
    local cs = scene.find_components("UICanvas")
    scene.set_pose(cs[1], {-1.2, 1.5, 4.5}, {0, 0.7071, 0, 0.7071})
    scene.set_pose(cs[2], {-1.2, 1.5, 3.5}, {0, 0.7071, 0, 0.7071})
end

return M
