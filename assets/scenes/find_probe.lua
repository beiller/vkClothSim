-- Probe: validate scene.find_components (subtree + whole-registry) and scene.get_entity.
local M = {}

function M._on_ready(self)
    local root = scene.entity("froot")
    scene.replace(root, "Transform", {})
    local child = scene.entity("fchild")
    scene.replace(child, "Camera", { fovDeg = 50 })
    scene.add_child(root, child)
    local sib = scene.entity("fsib")
    scene.replace(sib, "PointLight", { intensity = 5 }) -- top-level, not under root

    local inSub = scene.find_components("Camera", root)
    local allCam = scene.find_components("Camera")
    local allLight = scene.find_components("PointLight")
    local none = scene.find_components("RigidBody", root)
    local byName = scene.get_entity("froot")
    local missing = scene.get_entity("does_not_exist")

    print("find_probe: Camera under froot = " .. tostring(#inSub) .. " (expect 1)")
    print("find_probe: Camera all = " .. tostring(#allCam) .. " (expect 2: scene cam + fchild)")
    print("find_probe: PointLight all = " .. tostring(#allLight) .. " (expect 1)")
    print("find_probe: RigidBody under froot = " .. tostring(#none) .. " (expect 0)")
    print("find_probe: get_entity(froot) ok = " .. tostring(byName ~= nil))
    print("find_probe: get_entity(missing) nil = " .. tostring(missing == nil))
end

return M
