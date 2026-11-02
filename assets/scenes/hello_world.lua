-- hello world: the scene json is nearly blank (one camera object + one object with this
-- script attached); the light, textured spinning cube and UI are all created here in lua.
local M = {}

function M._on_ready(self)
    -- ui state holder: a Material on a dummy entity gives the slider a reflected float to bind
    local state = scene.entity("ui_state")
    scene.emplace(state, "Material", { metallic = 1.0 })

    -- one point light so the pbr material has something to reflect
    local light = scene.entity("light")
    scene.emplace(light, "Transform", { pos = { 0, 3, 4 } })
    scene.emplace(light, "PointLight", { color = { 1, 1, 1 }, intensity = 60, radius = 0.5 })

    -- the textured cube: geometry entity -> renderable ref, texture entity -> material ref
    local tex = graphics.load_texture("cloth_albedo.png", "cube_tex")
    local mat = scene.entity("mat_cube")
    scene.emplace(mat, "Material", { baseColor = { 1, 1, 1 }, metallic = 0.1, roughness = 0.4, tex = tex })

    self.cube = scene.entity("cube")
    scene.emplace(self.cube, "Transform", { pos = { 0, 0, 0 } })
    local g = scene.entity()
    scene.emplace(g, "Geometry", mesh.box({ 0.5, 0.5, 0.5 }))
    scene.emplace(self.cube, "Renderable", { geom = g })
    scene.emplace(self.cube, "MaterialRef", { material = mat })

    -- the ui: one window, a text line, a spin-speed slider, and the scene editor
    local win = ui.window("hello world", 320, 200)
    local tab = ui.tab(win, "scene")
    ui.text(tab, "a spinning cube - everything created in lua")
    ui.slider_f(tab, "spin speed (rad/s)", "ui_state.Material.metallic", 0, 6.28, "%.2f")
    ui.editor(ui.tab(win, "editor"))
end

function M._animate(self, dt)
    local speed = scene.get("ui_state.Material.metallic")
    local pose = scene.get_pose(self.cube)
    scene.set_pose(self.cube, pose.pos, math.qmul(math.qaxis({ 0, 1, 0 }, speed * dt), pose.quat))
end

return M
