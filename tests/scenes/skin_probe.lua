-- Probe for the Mesh joints/weights + scene.skeleton Lua APIs.
local M = {}

function M._on_ready()
    local skel = scene.entity("probe_skel")
    scene.replace(skel, "Skeleton", { boneNames = { "armA", "armB", "hand" } })
    local comp = scene.component(skel, "Skeleton")
    local nb = comp and comp.boneNames and #comp.boneNames or -1
    print("skin probe: skeleton = " .. tostring(skel ~= nil) .. " boneNames readback = " .. tostring(nb) .. " (expect 3)")
    local g = scene.entity()
    local ok, err = pcall(scene.replace, g, "Geometry", {
        pos = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 },
        idx = { 0, 1, 2, 0, 2, 3 },
        joints = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 },
        weights = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 },
    })
    print("skin probe: geometry replace = " .. tostring(ok) .. " " .. tostring(err))
    local comp = scene.component(g, "Geometry")
    local nj = comp and comp.joints and #comp.joints or -1
    local nw = comp and comp.weights and #comp.weights or -1
    print("skin probe: readback joints = " .. tostring(nj) .. " weights = " .. tostring(nw) .. " (expect 16/16)")
    local bad = pcall(scene.replace, scene.entity(), "Geometry", {
        pos = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 },
        idx = { 0, 1, 2, 0, 2, 3 },
        joints = { 0, 1, 2 }, -- wrong size: must be rejected
        weights = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 },
    })
    print("skin probe: bad-size rejected = " .. tostring(not bad))
    t.eq("skeleton boneNames readback", nb, 3)
    t.check("geometry replace ok", ok, err)
    t.eq("joints readback", nj, 16)
    t.eq("weights readback", nw, 16)
    t.check("bad-size rejected", not bad)
    t.done()
end

return M
