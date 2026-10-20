-- Probe for Mesh.shapes: create via scene.replace, read back with scene.shapes,
-- set via scene.set_morph (mesh target and skeleton target).
local M = {}

function M._on_ready()
    local g = scene.entity()
    local ok, err = pcall(scene.replace, g, "Geometry", {
        pos = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 },
        idx = { 0, 1, 2, 0, 2, 3 },
        shapes = {
            { name = "puff", weight = 0, pos = { 0, 0, 0.5, 0, 0, 0.5, 0, 0, 0.5, 0, 0, 0.5 } },
            { name = "flat", weight = 0.25 },
        },
    })
    print("shape probe: create = " .. tostring(ok))
    if not ok then
        print("shape probe: create error: " .. tostring(err))
        return
    end
    local shapes = scene.shapes(g)
    print("shape probe: count = " .. #shapes .. " (expect 2)")
    print("shape probe: puff = " .. tostring(shapes[1].name) .. " w=" .. tostring(shapes[1].weight) .. " (expect 0)")
    print("shape probe: flat = " .. tostring(shapes[2].name) .. " w=" .. tostring(shapes[2].weight) .. " (expect 0.25)")
    scene.set_morph(g, "puff", 0.7)
    local after = scene.shapes(g)
    print("shape probe: puff after set = " .. tostring(after[1].weight) .. " (expect 0.7)")
    scene.set_morph(g, "bogus", 0.9)
    print("shape probe: bogus name = no error")
    local skel = scene.skeleton("probe_skel2", { "A" })
    scene.link_mesh(skel, g)
    scene.set_morph(skel, "flat", 0.5)
    local via = scene.shapes(g)
    print("shape probe: flat via skel = " .. tostring(via[2].weight) .. " (expect 0.5)")
    local bad = pcall(scene.replace, scene.entity(), "Geometry", {
        pos = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 },
        idx = { 0, 1, 2, 0, 2, 3 },
        shapes = { { name = "x", pos = { 1, 2 } } },
    })
    print("shape probe: bad-size rejected = " .. tostring(not bad))
end

return M
