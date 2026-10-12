-- demo behavior: scattered rigid capsules + cloth/ball soft bodies
-- (static data, lights, materials and UI live in demo.json)

-- 50 scattered dynamic capsules, all sharing one material and one geometry
local capGeom = scene.geometry(mesh.capsule({ halfLen = 0.9, radius = 0.5 }))
math.randomseed(12345)
for i = 1, 50 do
    local px = (math.random() * 2 - 1) * 2
    local py = 4.5 + 3.5 * math.random()
    local pz = (math.random() * 2 - 1) * 2
    local q = scene.qmul(scene.qaxis({0, 1, 0}, math.random() * 2 * math.pi),
                         scene.qaxis({1, 0, 0}, (math.random() * 2 - 1) * 0.9))
    scene.capsule(capGeom, 0.9, 0.5, "mat_capsule", {px, py, pz}, q)
end

-- soft bodies
local cloth = mesh.cloth({ cw = 64, ch = 64, span = 8.0, y0 = 10.0 })
scene.cloth(cloth, mesh.clothCons({ cw = 64, ch = 64, span = 8.0 }), "mat_cloth", 3.0, "cloth")
local ball = mesh.sphere({ subdiv = 2, radius = 1.5, center = {0, 12, 0} })
scene.ball(ball, mesh.ballCons(ball), "mat_ball", 2.0, "ball")
