-- math API check: vector ops (vadd/vsub/vscale/vlen/vnorm/vdot/vcross) and quaternion helpers
-- (qrot, qfromto, qmul/qinv round-trip). Pure computation; no scene state.

function _on_ready(self)
    t.eq("vadd", math.vadd({ 1, 2, 3 }, { 4, 5, 6 }), { 5, 7, 9 })
    t.eq("vsub", math.vsub({ 4, 5, 6 }, { 1, 2, 3 }), { 3, 3, 3 })
    t.eq("vscale", math.vscale({ 1, -2, 3 }, 2.5), { 2.5, -5, 7.5 })
    t.eq("vlen 3-4-0", math.vlen({ 3, 4, 0 }), 5, 1e-5)
    t.eq("vlen zero", math.vlen({ 0, 0, 0 }), 0, 1e-6)
    t.eq("vnorm", math.vnorm({ 0, 0, 5 }), { 0, 0, 1 }, 1e-6)
    t.eq("vdot", math.vdot({ 1, 2, 3 }, { 4, 5, 6 }), 32, 1e-5)
    t.eq("vdot orthogonal", math.vdot({ 1, 0, 0 }, { 0, 1, 0 }), 0, 1e-6)
    t.eq("vcross x*y = z", math.vcross({ 1, 0, 0 }, { 0, 1, 0 }), { 0, 0, 1 }, 1e-6)
    t.eq("vcross y*x = -z", math.vcross({ 0, 1, 0 }, { 1, 0, 0 }), { 0, 0, -1 }, 1e-6)
    t.eq("vcross parallel = 0", math.vcross({ 2, 0, 0 }, { 3, 0, 0 }), { 0, 0, 0 }, 1e-6)

    -- qrot: +90 deg about Z sends +X to +Y
    local qz = math.qaxis({ 0, 0, 1 }, math.pi / 2)
    t.eq("qrot x->y", math.qrot(qz, { 1, 0, 0 }), { 0, 1, 0 }, 1e-5)

    -- qfromto: shortest arc a->b; applying it to a lands on b
    local q = math.qfromto({ 1, 0, 0 }, { 0, 1, 0 })
    t.eq("qfromto maps a to b", math.qrot(q, { 1, 0, 0 }), { 0, 1, 0 }, 1e-5)
    -- antiparallel case: axis is arbitrary but the result must still map a to -a
    local qa = math.qfromto({ 0, 1, 0 }, { 0, -1, 0 })
    t.eq("qfromto antiparallel", math.qrot(qa, { 0, 1, 0 }), { 0, -1, 0 }, 1e-5)

    -- qmul/qinv round-trip on an arbitrary rotation
    local qr = math.qaxis({ 0.3, 0.5, 0.8 }, 1.2)
    local v = { 1.7, -0.4, 2.2 }
    t.eq("qrot(qinv(q), qrot(q, v)) == v", math.qrot(math.qinv(qr), math.qrot(qr, v)), v, 1e-5)
    local id = math.qmul(qr, math.qinv(qr))
    t.eq("q * qinv(q) is identity", math.qrot(id, { 3, -1, 2 }), { 3, -1, 2 }, 1e-5)
    t.done()
end
