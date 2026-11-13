-- probe: does math.* read quats in [x,y,z,w] order? (identity qrot must return v unchanged)
local function qp(q)
    return string.format("{%.4f,%.4f,%.4f,%.4f}", q[1], q[2], q[3], q[4])
end

function _on_ready(self)
    local v = math.qrot({ 0, 0, 0, 1 }, { 0, 1, 0 }) -- identity: expect {0,1,0}
    print("PROBE qrot_id=" .. table.concat(v, ","))
    local q90z = math.qaxis({ 0, 0, 1 }, math.pi / 2) -- expect ~{0,0,0.7071,0.7071}
    print("PROBE qaxis_z90=" .. qp(q90z))
    local r = math.qrot(q90z, { 1, 0, 0 }) -- +X rotated +90deg about Z -> expect {0,1,0}
    print("PROBE rot_x_by_z90=" .. table.concat(r, ","))
    local m = math.qmul({ 0, 0, 0, 1 }, q90z) -- identity * q90z == q90z
    print("PROBE id_mul_q90z=" .. qp(m))
    t.done()
end
