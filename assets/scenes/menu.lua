-- the startup menu: one button per scene; a click loads that scene (a full reset)
local M = {}

function M._on_ready(self)
    local win = scene.ui_window("choose a scene", 290, 120)
    for _, s in ipairs(scene.scenes()) do
        if s.path ~= "assets/scenes/menu.json" then
            scene.ui_button(win, s.label, function()
                scene.request_scene(s.path)
            end)
        end
    end
    local err = scene.last_error()
    if err ~= "" then
        scene.ui_text(win, err)
    end
end

return M
