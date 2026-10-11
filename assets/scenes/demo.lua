-- demo behavior: scattered rigid capsules + cloth/ball soft bodies
-- (static data, lights, materials and UI live in demo.json)

-- 50 scattered dynamic capsules, all sharing one material
math.randomseed(12345)
for i = 1, 50 do
    local px = (math.random() * 2 - 1) * 2
    local py = 4.5 + 3.5 * math.random()
    local pz = (math.random() * 2 - 1) * 2
    local q = scene.qmul(scene.qaxis({0, 1, 0}, math.random() * 2 * math.pi),
                         scene.qaxis({1, 0, 0}, (math.random() * 2 - 1) * 0.9))
    scene.capsule(0.9, 0.5, "mat_capsule", {px, py, pz}, q)
end

-- soft bodies
scene.cloth(64, 64, 8.0, 10.0, "mat_cloth", 3.0, "cloth")
scene.ball(2, 1.5, "mat_ball", {0, 12, 0}, 2.0, "ball")
