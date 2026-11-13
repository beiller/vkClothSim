-- Light color animation: everything is created in Lua (camera, light, clip, player).
-- The point light's color cycles white -> red -> white over 2s, linear interpolation, looping.
function _on_ready(self)
    -- camera so the scene renders when opened windowed
    local cam = scene.entity("cam")
    scene.emplace(cam, "Transform", { pos = {0, 1, 3}, quat = {0, 0, 0, 1} })
    scene.emplace(cam, "Camera", { fovDeg = 50, active = true })
    scene.attach_script(cam, "scenes/camera") -- attaches the "fly camera movement" script which gives "look" and "wasd + qe" kb movement

    -- the light being animated (white at rest)
    local light = scene.entity("light")
    scene.emplace(light, "Transform", { pos = {1.5, 1.5, 1.5} })
    scene.emplace(light, "PointLight", { intensity = 5, radius = 0.15 })

    -- a cube under the light so the color change is visible (mesh.box takes half-extents)
    local mat = scene.entity("cube_mat")
    scene.emplace(mat, "Material", { baseColor = {0.8, 0.8, 0.8}, metallic = 0, roughness = 0.6 })
    local geom = scene.entity("cube_geom")
    scene.emplace(geom, "Geometry", mesh.box({0.5, 0.5, 0.5}))
    local cube = scene.entity("cube")
    scene.emplace(cube, "Transform", { pos = {0, -0.5, 0} })
    scene.emplace(cube, "Renderable", { geom = geom })
    scene.emplace(cube, "MaterialRef", { material = mat })

    -- clip: one track on the light's color; keys white (t=0) -> red (t=1) -> white (t=2), linear.
    -- duration is computed on write (max last keyframe time = 2).
    local clip = scene.entity("light_color_clip")
    local light_strength = 10.0
    scene.emplace(clip, "AnimationClip", {
        targets = { "light.PointLight.color" },
        interps = { 0 },
        trackStart = { 0, 4 },
        times = { 0, 1, 2, 3 },
        values = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0 },
    })

    -- player: loop mode (1), starts playing immediately
    local player = scene.entity("light_color_player")
    scene.emplace(player, "AnimationPlayer", { clip = clip, loop = 1, playing = true })
end

-- print the animated color once per second so the cycle is visible in the console
local totalDt = 0.0
function _animate(self, dt)
    totalDt = totalDt + dt
    if totalDt > 0.5 then
        totalDt = 0.0
        local c = scene.get("light.PointLight.color")
        print(string.format("light color: %.2f %.2f %.2f", c[1], c[2], c[3]))
    end
end
