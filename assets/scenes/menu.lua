-- the startup menu: one button per scene; a click loads that scene (a full reset)

local uib = require("ui")

local targets = {}   -- button entity -> scene path

function on_menu(btn)
    scene.request_scene(targets[btn])
end

function _on_ready(self)
    local win = uib.make_window("choose a scene", 290, 120, { 0, 1.5, 2.8 })
    for _, s in ipairs(scene.scenes()) do
        -- filter by filename (the path prefix varies with cwd / --scenes-dir)
        if s.path:match("[^/]+$") ~= "menu.json" then
            local b = scene.entity()
            scene.replace(b, "Parent", { e = win })
            scene.emplace(b, "UIWidget", { order = ui.seq() })
            scene.emplace(b, "UIButton", { label = s.label, target_url = "cam.LuaScript.on_menu" })
            targets[b] = s.path
        end
    end
    local err = scene.last_error()
    if err ~= "" then
        local t = scene.entity()
        scene.replace(t, "Parent", { e = win })
        scene.emplace(t, "UIWidget", { order = ui.seq() })
        scene.emplace(t, "UIText", { label = err })
    end
    self.metricsText = uib.text(uib.make_window("metrics", 160, 180, { 0.45, 1.72, 2.8 }), "")
end

-- refresh the metrics window from the engine's performance string every frame
function _animate(self, dt)
    local t = scene.component(self.metricsText, "UIText")
    if t then
        t.label = engine.performance_metrics()
    end
end
