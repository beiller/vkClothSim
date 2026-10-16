-- Joint inspector: hides the character's meshes and puts a small sphere on every joint.
-- Markers are children of the joint entities, so they follow the bones when posed.
-- Toggle a mesh back on from Lua: scene.visible("hashimoto/Mesh", true)
local M = {}

local meshNodes = {
    "hashimoto/Mesh",
    "hashimoto/Genesis8FemaleEyelashes",
    "hashimoto/HeadLight_R_608",
    "hashimoto/HeadLight_L_608",
    "hashimoto/GoldenPalace_2254",
    "hashimoto/Base_98721",
}

function M._on_ready(self)
    render.material("ground", {0.35, 0.35, 0.38}, 0, 0.95)
    render.mesh(scene.entity("ground"), render.geometry(mesh.box({ 20, 1, 20 })), "ground", { 0, -1, 0 })

    local ch = scene.entity("char")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
    render.gltf(ch, "local/hashimoto.glb")

    for _, n in ipairs(meshNodes) do
        scene.visible(n, false)
    end

    render.material("joint", {1, 0.15, 0.15}, 0, 0.4, nil, {1, 0.15, 0.15})
    local g = render.geometry(mesh.sphere({ subdiv = 2, radius = 0.015 }))
    for _, j in ipairs(scene.joints(ch)) do
        local m = scene.entity()
        render.mesh(m, g, "joint", nil, nil, { parent = j, noShadow = true })
    end
end
return M
