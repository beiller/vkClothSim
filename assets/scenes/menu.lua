-- the startup menu: one button per scene; a click loads that scene (a full reset)
local M = {}

function M._on_ready(self)
    local win = ui.window("choose a scene", 290, 120)
    for _, s in ipairs(scene.scenes()) do
        -- filter by filename (the path prefix varies with cwd / --scenes-dir)
        if s.path:match("[^/]+$") ~= "menu.json" then
            ui.button(win, s.label, function()
                scene.request_scene(s.path)
            end)
        end
    end
    local err = scene.last_error()
    if err ~= "" then
        ui.text(win, err)
    end
end

return M
