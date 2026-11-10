-- hello world: the scene json is blank (one object with this script attached); the camera,
-- light, textured spinning cube and UI are all created here in lua.

-- script variables, bindable via "hello.LuaScript.<var>"
spin_speed = 1.0

-- executed once, when the script is ready
function _on_ready(self)
    -- the fly camera: active=true is what makes it THE camera
    local camera = scene.entity("cam")
    scene.emplace(camera, "Transform", { pos = { 0, 1.2, 4 } })
    scene.emplace(camera, "Camera", { fovDeg = 50, active = true })
    scene.attach_script(camera, "scenes/camera") -- attaches the "fly camera movement" script which gives "look" and "wasd + qe" kb movement

    -- attach the VR controller script (enables movement and pointing with a VR controller setup)
    local vr = scene.entity("vr")
    scene.attach_script(vr, "scenes/vr_controller")

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
    ui.slider_f(tab, "spin speed (rad/s)", "hello.LuaScript.spin_speed", 0, 10.0, "%.2f")
    ui.slider_f(tab, "cube metallic", "mat_cube.Material.metallic", 0, 1.0, "%.2f")
    ui.slider_f(tab, "cube refraction", "mat_cube.Material.refraction", 0, 1.0, "%.2f")
    ui.slider_f(tab, "cube ior", "mat_cube.Material.ior", 1.0, 3.0, "%.2f")
    ui.slider_f(tab, "cube roughness", "mat_cube.Material.roughness", 0, 1.0, "%.2f")
    ui.editor(ui.tab(win, "editor"))
end

-- runs every graphics frame
function _animate(self, dt)
    local speed = spin_speed
    local pose = scene.get_pose(self.cube)
    scene.set_pose(self.cube, pose.pos, math.qmul(math.qaxis({ 0.5, 0.5, 0 }, speed * dt), pose.quat))
end

-- runs every physics step
function _physics(self, dt)
    
end

