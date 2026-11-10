
local json = require "json"
local uib = require("ui")



-- script variables, bindable via "caps.LuaScript.<var>"
kToneFreq = 3.0 -- Hz; springback speed (higher = stiffer)
kToneDamp = 25.0 -- damping ratio; 1 = critical (no oscillation)
kGripTorque = 1.0 -- N m max friction torque per rotation axis
kBodyDamping = 0.05
kBodyFriction = 10.0

local joints = {}
local boneEnt = {}

local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

-- Daz Genesis 8 per-bone rotation limits (degrees -> radians); missing file = joints stay fully stiff
local RAD = math.pi / 180
local function loadLimits()
    for _, p in ipairs({ "assets/local/genesis8_bone_limits.json", "local/genesis8_bone_limits.json" }) do
        local f = io.open(p)
        if f then
            local data = json.decode(f:read("*a"))
            f:close()
            local out = {}
            for name, ax in pairs(data) do
                -- Daz is left-handed: positive angles run opposite to the scene's right-handed axes, so flip all three
                out[name] = { x = { -ax.x[2] * RAD, -ax.x[1] * RAD },
                             y = { -ax.y[2] * RAD, -ax.y[1] * RAD },
                             z = { -ax.z[2] * RAD, -ax.z[1] * RAD } }
            end
            return out
        end
    end
    print("caps_fit: genesis8_bone_limits.json not found, joints stay fully stiff")
    return {}
end

-- BEGIN GENERATED (solvers/update_caps_fit.sh) -- do not hand-edit
local kFits = {
-- bbox: (-0.1071, 0.5310, -0.0759) - (0.1084, 0.6954, 0.0779)
-- bone 'pelvis' (joint 1, weight > 0.30): 11072 vertices + normals, 1 capsules, underfit 0.019, overhang 0.026
    { name = "pelvis", com = { 0.001, 0.614, -0.006 }, caps = {
      { origin = {0.001, 0.639, -0.002}, axis = {0.003, 0.997, 0.078}, seg = {{0.000, 0.000, 0.077, 0.077} } }
    } },
-- bbox: (-0.0935, 0.6424, -0.0552) - (0.0947, 0.7443, 0.0805)
-- bone 'abdomenLower' (joint 34, weight > 0.30): 6608 vertices + normals, 1 capsules, underfit 0.009, overhang 0.024
    { name = "abdomenLower", com = { 0.001, 0.699, 0.014 }, caps = {
      { origin = {0.000, 0.708, 0.016}, axis = {-0.000, 0.994, 0.112}, seg = {{0.000, 0.000, 0.070, 0.070} } }
    } },
-- bbox: (-0.0949, 0.7231, -0.0555) - (0.0955, 0.8129, 0.0803)
-- bone 'abdomenUpper' (joint 35, weight > 0.30): 5328 vertices + normals, 1 capsules, underfit 0.026, overhang 0.026
    { name = "abdomenUpper", com = { 0.000, 0.762, 0.019 }, caps = {
      { origin = {0.001, 0.721, 0.017}, axis = {0.000, -0.995, 0.102}, seg = {{0.000, 0.000, 0.071, 0.071} } }
    } },
-- bbox: (-0.1094, 0.7654, -0.0821) - (0.1099, 0.9114, 0.0756)
-- bone 'chestLower' (joint 36, weight > 0.30): 10032 vertices + normals, 1 capsules, underfit 0.027, overhang 0.025
    { name = "chestLower", com = { 0.000, 0.837, 0.009 }, caps = {
      { origin = {0.000, 0.819, 0.006}, axis = {0.000, 0.979, -0.204}, seg = {{0.000, 0.000, 0.074, 0.074} } }
    } },
-- bbox: (-0.0673, 0.8695, -0.0823) - (0.0678, 1.0031, 0.0475)
-- bone 'chestUpper' (joint 37, weight > 0.30): 6400 vertices + normals, 1 capsules, underfit 0.029, overhang 0.010
    { name = "chestUpper", com = { 0.000, 0.952, -0.021 }, caps = {
      { origin = {0.000, 0.941, -0.028}, axis = {-0.003, 0.992, -0.125}, seg = {{0.000, 0.000, 0.044, 0.044} } }
    } },
-- bbox: (0.0295, 0.8477, -0.0821) - (0.1420, 0.9768, 0.0369)
-- bone 'lCollar' (joint 38, weight > 0.30): 3680 vertices + normals, 1 capsules, underfit 0.022, overhang 0.006
    { name = "lCollar", com = { 0.085, 0.921, -0.029 }, caps = {
      { origin = {0.081, 0.917, -0.035}, axis = {0.950, -0.275, -0.147}, seg = {{0.000, 0.000, 0.040, 0.040} } }
    } },
-- bbox: (-0.1418, 0.8474, -0.0823) - (-0.0295, 0.9768, 0.0367)
-- bone 'rCollar' (joint 63, weight > 0.30): 3680 vertices + normals, 1 capsules, underfit 0.022, overhang 0.003
    { name = "rCollar", com = { -0.085, 0.921, -0.029 }, caps = {
      { origin = {-0.081, 0.917, -0.035}, axis = {0.948, 0.281, 0.151}, seg = {{0.000, 0.000, 0.040, 0.040} } }
    } },
-- bbox: (-0.0440, 0.9702, -0.0532) - (0.0440, 0.9976, 0.0141)
-- bone 'neckLower' (joint 88, weight > 0.30): 1504 vertices + normals, 1 capsules, underfit 0.003, overhang 0.006
    { name = "neckLower", com = { -0.000, 0.983, -0.009 }, caps = {
      { origin = {0.000, 0.971, -0.026}, axis = {-0.000, 0.980, 0.199}, seg = {{0.000, 0.000, 0.033, 0.033} } }
    } },
-- bbox: (-0.0440, 0.9847, -0.0652) - (0.0439, 1.0378, 0.0283)
-- bone 'neckUpper' (joint 89, weight > 0.30): 2640 vertices + normals, 1 capsules, underfit 0.012, overhang 0.000
    { name = "neckUpper", com = { -0.000, 1.011, -0.020 }, caps = {
      { origin = {-0.000, 1.028, -0.017}, axis = {-0.001, 0.999, 0.034}, seg = {{0.000, 0.000, 0.031, 0.031} } }
    } },
-- bbox: (-0.0911, 1.0194, -0.1100) - (0.0912, 1.2164, 0.0888)
-- bone 'head' (joint 90, weight > 0.30): 25936 vertices + normals, 1 capsules, underfit 0.025, overhang 0.023
    { name = "head", com = { 0.000, 1.096, 0.020 }, caps = {
      { origin = {-0.000, 1.095, 0.024}, axis = {-0.003, 0.733, -0.680}, seg = {{0.000, 0.000, 0.080, 0.080} } }
    } },
-- bbox: (0.0549, 0.0548, -0.0754) - (0.1608, 0.3730, 0.0363)
-- bone 'lShin' (joint 4, weight > 0.30): 9280 vertices + normals, 1 capsules, underfit 0.004, overhang 0.014
    { name = "lShin", com = { 0.113, 0.225, -0.029 }, caps = {
      { origin = {0.138, 0.055, -0.045}, axis = {-0.147, 0.985, 0.093}, seg = {{0.000, 0.325, 0.025, 0.040} } }
    } },
-- bbox: (0.0124, 0.4746, -0.0619) - (0.1195, 0.6077, 0.0539)
-- bone 'lThighBend' (joint 2, weight > 0.30): 3776 vertices + normals, 1 capsules, underfit 0.025, overhang 0.003
    { name = "lThighBend", com = { 0.071, 0.527, -0.003 }, caps = {
      { origin = {0.064, 0.547, 0.001}, axis = {-0.201, 0.978, -0.052}, seg = {{0.000, 0.000, 0.044, 0.044} } }
    } },
-- bbox: (0.0165, 0.3296, -0.0619) - (0.1314, 0.5799, 0.0539)
-- bone 'lThighTwist' (joint 3, weight > 0.30): 8384 vertices + normals, 1 capsules, underfit 0.003, overhang 0.006
    { name = "lThighTwist", com = { 0.081, 0.446, -0.004 }, caps = {
      { origin = {0.099, 0.333, -0.007}, axis = {-0.162, 0.987, 0.025}, seg = {{0.000, 0.243, 0.031, 0.046} } }
    } },
-- bbox: (0.1050, 0.0071, -0.0820) - (0.1770, 0.0799, 0.0074)
-- bone 'lFoot' (joint 5, weight > 0.30): 3040 vertices + normals, 1 capsules, underfit 0.010, overhang 0.010
    { name = "lFoot", com = { 0.137, 0.040, -0.044 }, caps = {
      { origin = {0.137, 0.038, -0.042}, axis = {-0.249, 0.900, -0.357}, seg = {{0.000, 0.000, 0.032, 0.032} } }
    } },
-- bbox: (0.1140, -0.0027, -0.0474) - (0.1929, 0.0738, 0.0529)
-- bone 'lMetatarsals' (joint 6, weight > 0.30): 3568 vertices + normals, 1 capsules, underfit 0.019, overhang 0.007
    { name = "lMetatarsals", com = { 0.155, 0.024, 0.010 }, caps = {
      { origin = {0.150, 0.027, -0.000}, axis = {-0.265, 0.333, -0.905}, seg = {{0.000, 0.000, 0.027, 0.027} } }
    } },
-- bbox: (-0.1566, 0.0544, -0.0758) - (-0.0521, 0.3726, 0.0357)
-- bone 'rShin' (joint 20, weight > 0.30): 9280 vertices + normals, 1 capsules, underfit 0.004, overhang 0.014
    { name = "rShin", com = { -0.109, 0.225, -0.029 }, caps = {
      { origin = {-0.134, 0.054, -0.046}, axis = {0.142, 0.985, 0.093}, seg = {{0.000, 0.325, 0.025, 0.040} } }
    } },
-- bbox: (-0.1174, 0.4745, -0.0622) - (-0.0106, 0.6072, 0.0535)
-- bone 'rThighBend' (joint 18, weight > 0.30): 3776 vertices + normals, 1 capsules, underfit 0.025, overhang 0.000
    { name = "rThighBend", com = { -0.069, 0.527, -0.004 }, caps = {
      { origin = {-0.063, 0.546, 0.000}, axis = {0.196, 0.979, -0.052}, seg = {{0.000, 0.000, 0.044, 0.044} } }
    } },
-- bbox: (-0.1287, 0.3294, -0.0622) - (-0.0146, 0.5795, 0.0535)
-- bone 'rThighTwist' (joint 19, weight > 0.30): 8384 vertices + normals, 1 capsules, underfit 0.003, overhang 0.006
    { name = "rThighTwist", com = { -0.079, 0.446, -0.004 }, caps = {
      { origin = {-0.096, 0.333, -0.007}, axis = {0.157, 0.987, 0.028}, seg = {{0.000, 0.243, 0.031, 0.046} } }
    } },
-- bbox: (-0.1727, 0.0066, -0.0824) - (-0.1008, 0.0795, 0.0070)
-- bone 'rFoot' (joint 21, weight > 0.30): 3040 vertices + normals, 1 capsules, underfit 0.010, overhang 0.011
    { name = "rFoot", com = { -0.132, 0.039, -0.045 }, caps = {
      { origin = {-0.133, 0.037, -0.042}, axis = {0.247, 0.901, -0.357}, seg = {{0.000, 0.000, 0.032, 0.032} } }
    } },
-- bbox: (-0.1888, -0.0031, -0.0478) - (-0.1098, 0.0733, 0.0525)
-- bone 'rMetatarsals' (joint 22, weight > 0.30): 3568 vertices + normals, 1 capsules, underfit 0.019, overhang 0.006
    { name = "rMetatarsals", com = { -0.150, 0.023, 0.009 }, caps = {
      { origin = {-0.146, 0.026, -0.001}, axis = {0.268, 0.334, -0.904}, seg = {{0.000, 0.000, 0.027, 0.027} } }
    } },
-- bbox: (0.1025, 0.8252, -0.0707) - (0.2033, 0.9541, 0.0071)
-- bone 'lShldrBend' (joint 39, weight > 0.30): 3424 vertices + normals, 1 capsules, underfit 0.060, overhang 0.005
    { name = "lShldrBend", com = { 0.147, 0.889, -0.035 }, caps = {
      { origin = {0.125, 0.915, -0.035}, axis = {-0.700, 0.714, 0.007}, seg = {{0.000, 0.000, 0.030, 0.030} } }
    } },
-- bbox: (0.1324, 0.7658, -0.0660) - (0.2549, 0.8853, -0.0038)
-- bone 'lShldrTwist' (joint 40, weight > 0.30): 4064 vertices + normals, 1 capsules, underfit 0.065, overhang 0.003
    { name = "lShldrTwist", com = { 0.201, 0.821, -0.036 }, caps = {
      { origin = {0.214, 0.804, -0.033}, axis = {-0.662, 0.748, -0.039}, seg = {{0.000, 0.000, 0.024, 0.024} } }
    } },
-- bbox: (0.2082, 0.6923, -0.0600) - (0.3232, 0.8111, 0.0184)
-- bone 'lForearmBend' (joint 41, weight > 0.30): 4096 vertices + normals, 1 capsules, underfit 0.058, overhang 0.006
    { name = "lForearmBend", com = { 0.265, 0.755, -0.023 }, caps = {
      { origin = {0.250, 0.772, -0.029}, axis = {-0.697, 0.676, -0.240}, seg = {{0.000, 0.000, 0.024, 0.024} } }
    } },
-- bbox: (0.2618, 0.6535, -0.0401) - (0.3689, 0.7621, 0.0370)
-- bone 'lForearmTwist' (joint 42, weight > 0.30): 4000 vertices + normals, 1 capsules, underfit 0.001, overhang 0.006
    { name = "lForearmTwist", com = { 0.323, 0.700, 0.000 }, caps = {
      { origin = {0.358, 0.662, 0.015}, axis = {-0.662, 0.702, -0.264}, seg = {{0.000, 0.026, 0.020, 0.017}, {0.026, 0.118, 0.017, 0.021} } }
    } },
-- bbox: (0.3364, 0.6323, -0.0132) - (0.3827, 0.6800, 0.0501)
-- bone 'lHand' (joint 43, weight > 0.30): 2480 vertices + normals, 1 capsules, underfit 0.019, overhang 0.005
    { name = "lHand", com = { 0.360, 0.657, 0.014 }, caps = {
      { origin = {0.351, 0.667, 0.014}, axis = {-0.688, 0.725, -0.024}, seg = {{0.000, 0.000, 0.019, 0.019} } }
    } },
-- bbox: (-0.2024, 0.8246, -0.0710) - (-0.1020, 0.9536, 0.0066)
-- bone 'rShldrBend' (joint 64, weight > 0.30): 3424 vertices + normals, 1 capsules, underfit 0.060, overhang 0.004
    { name = "rShldrBend", com = { -0.146, 0.889, -0.036 }, caps = {
      { origin = {-0.125, 0.914, -0.036}, axis = {0.696, 0.718, 0.012}, seg = {{0.000, 0.000, 0.030, 0.030} } }
    } },
-- bbox: (-0.2535, 0.7652, -0.0667) - (-0.1316, 0.8847, -0.0049)
-- bone 'rShldrTwist' (joint 65, weight > 0.30): 4064 vertices + normals, 1 capsules, underfit 0.065, overhang 0.002
    { name = "rShldrTwist", com = { -0.200, 0.821, -0.037 }, caps = {
      { origin = {-0.213, 0.803, -0.034}, axis = {0.658, 0.752, -0.034}, seg = {{0.000, 0.000, 0.024, 0.024} } }
    } },
-- bbox: (-0.3217, 0.6913, -0.0612) - (-0.2068, 0.8103, 0.0164)
-- bone 'rForearmBend' (joint 66, weight > 0.30): 4096 vertices + normals, 1 capsules, underfit 0.058, overhang 0.006
    { name = "rForearmBend", com = { -0.263, 0.754, -0.025 }, caps = {
      { origin = {-0.248, 0.772, -0.030}, axis = {0.695, 0.679, -0.234}, seg = {{0.000, 0.000, 0.024, 0.024} } }
    } },
-- bbox: (-0.3673, 0.6522, -0.0418) - (-0.2602, 0.7611, 0.0348)
-- bone 'rForearmTwist' (joint 67, weight > 0.30): 4000 vertices + normals, 1 capsules, underfit 0.001, overhang 0.006
    { name = "rForearmTwist", com = { -0.321, 0.698, -0.002 }, caps = {
      { origin = {-0.356, 0.660, 0.012}, axis = {0.660, 0.705, -0.258}, seg = {{0.000, 0.026, 0.019, 0.017}, {0.026, 0.118, 0.017, 0.021} } }
    } },
-- bbox: (-0.3810, 0.6309, -0.0157) - (-0.3347, 0.6786, 0.0481)
-- bone 'rHand' (joint 68, weight > 0.30): 2480 vertices + normals, 1 capsules, underfit 0.019, overhang 0.005
    { name = "rHand", com = { -0.358, 0.655, 0.012 }, caps = {
      { origin = {-0.350, 0.665, 0.011}, axis = {0.677, 0.734, -0.054}, seg = {{0.000, 0.000, 0.019, 0.019} } }
    } },
}

local kJoints = {
    { name = "hip", parent = "root", pos = { 0.0006, 0.6567, 0.0092 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "pelvis", parent = "hip", pos = { 0.0006, 0.6732, 0.0091 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lThighBend", parent = "pelvis", pos = { 0.0527, 0.6023, -0.0002 }, quat = { 0.0131, 0.0430, 0.0688, 0.9966 } },
    { name = "lThighTwist", parent = "lThighBend", pos = { 0.0692, 0.4738, -0.0042 }, quat = { 0.0131, 0.0430, 0.0688, 0.9966 } },
    { name = "lShin", parent = "lThighTwist", pos = { 0.0957, 0.3301, -0.0125 }, quat = { 0.0510, 0.0878, 0.0703, 0.9923 } },
    { name = "lFoot", parent = "lShin", pos = { 0.1339, 0.0621, -0.0353 }, quat = { 0.1790, 0.1466, 0.0515, 0.9715 } },
    { name = "lMetatarsals", parent = "lFoot", pos = { 0.1348, 0.0583, -0.0417 }, quat = { 0.1887, 0.1366, 0.0514, 0.9711 } },
    { name = "lToe", parent = "lMetatarsals", pos = { 0.1632, 0.0155, 0.0403 }, quat = { 0.1100, 0.1632, -0.0000, 0.9804 } },
    { name = "lSmallToe4", parent = "lToe", pos = { 0.1899, 0.0147, 0.0323 }, quat = { 0.2580, 0.1275, -0.0577, 0.9560 } },
    { name = "lSmallToe4_2", parent = "lSmallToe4", pos = { 0.1915, 0.0117, 0.0398 }, quat = { 0.2580, 0.1275, -0.0577, 0.9560 } },
    { name = "lSmallToe3", parent = "lToe", pos = { 0.1782, 0.0168, 0.0389 }, quat = { 0.2441, 0.1582, -0.0268, 0.9564 } },
    { name = "lSmallToe3_2", parent = "lSmallToe3", pos = { 0.1812, 0.0126, 0.0484 }, quat = { 0.2441, 0.1582, -0.0268, 0.9564 } },
    { name = "lSmallToe2", parent = "lToe", pos = { 0.1696, 0.0193, 0.0446 }, quat = { 0.2294, 0.1556, 0.0006, 0.9608 } },
    { name = "lSmallToe2_2", parent = "lSmallToe2", pos = { 0.1735, 0.0143, 0.0571 }, quat = { 0.2294, 0.1556, 0.0006, 0.9608 } },
    { name = "lSmallToe1", parent = "lToe", pos = { 0.1568, 0.0192, 0.0478 }, quat = { 0.1916, 0.1774, 0.0031, 0.9653 } },
    { name = "lSmallToe1_2", parent = "lSmallToe1", pos = { 0.1622, 0.0145, 0.0622 }, quat = { 0.1916, 0.1774, 0.0031, 0.9653 } },
    { name = "lBigToe", parent = "lToe", pos = { 0.1374, 0.0166, 0.0484 }, quat = { 0.1518, 0.1802, 0.0102, 0.9718 } },
    { name = "lBigToe_2", parent = "lBigToe", pos = { 0.1437, 0.0116, 0.0657 }, quat = { 0.1518, 0.1802, 0.0102, 0.9718 } },
    { name = "rThighBend", parent = "pelvis", pos = { -0.0513, 0.6021, -0.0004 }, quat = { 0.0131, -0.0430, -0.0688, 0.9966 } },
    { name = "rThighTwist", parent = "rThighBend", pos = { -0.0670, 0.4735, -0.0045 }, quat = { 0.0131, -0.0430, -0.0688, 0.9966 } },
    { name = "rShin", parent = "rThighTwist", pos = { -0.0929, 0.3297, -0.0131 }, quat = { 0.0510, -0.0878, -0.0703, 0.9923 } },
    { name = "rFoot", parent = "rShin", pos = { -0.1297, 0.0616, -0.0357 }, quat = { 0.1790, -0.1466, -0.0515, 0.9715 } },
    { name = "rMetatarsals", parent = "rFoot", pos = { -0.1305, 0.0578, -0.0421 }, quat = { 0.1887, -0.1366, -0.0514, 0.9711 } },
    { name = "rToe", parent = "rMetatarsals", pos = { -0.1592, 0.0150, 0.0398 }, quat = { 0.1100, -0.1632, 0.0000, 0.9804 } },
    { name = "rSmallToe4", parent = "rToe", pos = { -0.1858, 0.0141, 0.0317 }, quat = { 0.2580, -0.1275, 0.0577, 0.9560 } },
    { name = "rSmallToe4_2", parent = "rSmallToe4", pos = { -0.1876, 0.0111, 0.0393 }, quat = { 0.2580, -0.1275, 0.0577, 0.9560 } },
    { name = "rSmallToe3", parent = "rToe", pos = { -0.1742, 0.0163, 0.0384 }, quat = { 0.2441, -0.1582, 0.0268, 0.9564 } },
    { name = "rSmallToe3_2", parent = "rSmallToe3", pos = { -0.1773, 0.0120, 0.0479 }, quat = { 0.2441, -0.1582, 0.0268, 0.9564 } },
    { name = "rSmallToe2", parent = "rToe", pos = { -0.1657, 0.0187, 0.0441 }, quat = { 0.2294, -0.1556, -0.0006, 0.9608 } },
    { name = "rSmallToe2_2", parent = "rSmallToe2", pos = { -0.1696, 0.0138, 0.0566 }, quat = { 0.2294, -0.1556, -0.0006, 0.9608 } },
    { name = "rSmallToe1", parent = "rToe", pos = { -0.1529, 0.0187, 0.0473 }, quat = { 0.1916, -0.1774, -0.0031, 0.9653 } },
    { name = "rSmallToe1_2", parent = "rSmallToe1", pos = { -0.1583, 0.0140, 0.0617 }, quat = { 0.1916, -0.1774, -0.0031, 0.9653 } },
    { name = "rBigToe", parent = "rToe", pos = { -0.1335, 0.0162, 0.0480 }, quat = { 0.1518, -0.1802, -0.0102, 0.9718 } },
    { name = "rBigToe_2", parent = "rBigToe", pos = { -0.1398, 0.0111, 0.0653 }, quat = { 0.1518, -0.1802, -0.0102, 0.9718 } },
    { name = "abdomenLower", parent = "hip", pos = { 0.0006, 0.6730, -0.0020 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "abdomenUpper", parent = "abdomenLower", pos = { 0.0005, 0.7245, 0.0047 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "chestLower", parent = "abdomenUpper", pos = { 0.0004, 0.7764, 0.0025 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "chestUpper", parent = "chestLower", pos = { 0.0002, 0.8638, -0.0175 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lCollar", parent = "chestUpper", pos = { 0.0245, 0.9386, -0.0262 }, quat = { 0.0009, 0.0306, 0.0153, 0.9994 } },
    { name = "lShldrBend", parent = "lCollar", pos = { 0.1112, 0.9253, -0.0335 }, quat = { 0.0139, -0.0048, -0.4116, 0.9112 } },
    { name = "lShldrTwist", parent = "lShldrBend", pos = { 0.1669, 0.8649, -0.0324 }, quat = { 0.0124, -0.0016, -0.4130, 0.9107 } },
    { name = "lForearmBend", parent = "lShldrTwist", pos = { 0.2344, 0.7906, -0.0328 }, quat = { -0.0449, -0.1279, -0.3782, 0.9157 } },
    { name = "lForearmTwist", parent = "lForearmBend", pos = { 0.2894, 0.7344, -0.0086 }, quat = { -0.0446, -0.1275, -0.3775, 0.9161 } },
    { name = "lHand", parent = "lForearmTwist", pos = { 0.3496, 0.6673, 0.0145 }, quat = { 0.0116, -0.0006, -0.3756, 0.9267 } },
    { name = "lThumb1", parent = "lHand", pos = { 0.3535, 0.6559, 0.0267 }, quat = { -0.3402, -0.4394, -0.4259, 0.7140 } },
    { name = "lThumb2", parent = "lThumb1", pos = { 0.3610, 0.6410, 0.0508 }, quat = { -0.2963, -0.3205, -0.5022, 0.7465 } },
    { name = "lThumb3", parent = "lThumb2", pos = { 0.3674, 0.6245, 0.0619 }, quat = { -0.3808, -0.4228, -0.4046, 0.7160 } },
    { name = "lCarpal1", parent = "lHand", pos = { 0.3540, 0.6679, 0.0234 }, quat = { 0.0643, -0.1044, -0.4156, 0.9013 } },
    { name = "lIndex1", parent = "lCarpal1", pos = { 0.3913, 0.6276, 0.0397 }, quat = { 0.0601, -0.0581, -0.4306, 0.8987 } },
    { name = "lIndex2", parent = "lIndex1", pos = { 0.4089, 0.6072, 0.0443 }, quat = { 0.0310, -0.0351, -0.4323, 0.9005 } },
    { name = "lIndex3", parent = "lIndex2", pos = { 0.4184, 0.5957, 0.0461 }, quat = { 0.0280, -0.0281, -0.4422, 0.8960 } },
    { name = "lCarpal2", parent = "lHand", pos = { 0.3557, 0.6664, 0.0164 }, quat = { 0.0316, -0.0401, -0.4151, 0.9083 } },
    { name = "lMid1", parent = "lCarpal2", pos = { 0.3950, 0.6268, 0.0239 }, quat = { 0.0275, -0.0229, -0.4049, 0.9137 } },
    { name = "lMid2", parent = "lMid1", pos = { 0.4161, 0.6052, 0.0263 }, quat = { 0.0268, -0.0243, -0.4537, 0.8904 } },
    { name = "lMid3", parent = "lMid2", pos = { 0.4267, 0.5907, 0.0280 }, quat = { 0.0106, 0.0020, -0.3989, 0.9169 } },
    { name = "lCarpal3", parent = "lHand", pos = { 0.3566, 0.6660, 0.0083 }, quat = { 0.0039, 0.0153, -0.4163, 0.9091 } },
    { name = "lRing1", parent = "lCarpal3", pos = { 0.3927, 0.6287, 0.0091 }, quat = { 0.0016, 0.0316, -0.4217, 0.9062 } },
    { name = "lRing2", parent = "lRing1", pos = { 0.4112, 0.6080, 0.0086 }, quat = { 0.0187, 0.0325, -0.4241, 0.9048 } },
    { name = "lRing3", parent = "lRing2", pos = { 0.4215, 0.5960, 0.0082 }, quat = { 0.0027, 0.0174, -0.4211, 0.9068 } },
    { name = "lCarpal4", parent = "lHand", pos = { 0.3569, 0.6659, 0.0008 }, quat = { -0.0329, 0.0842, -0.4233, 0.9015 } },
    { name = "lPinky1", parent = "lCarpal4", pos = { 0.3870, 0.6283, -0.0068 }, quat = { -0.0071, 0.0579, -0.4151, 0.9079 } },
    { name = "lPinky2", parent = "lPinky1", pos = { 0.4002, 0.6141, -0.0077 }, quat = { -0.0328, 0.0495, -0.4129, 0.9088 } },
    { name = "lPinky3", parent = "lPinky2", pos = { 0.4083, 0.6049, -0.0088 }, quat = { -0.0476, 0.0428, -0.4155, 0.9073 } },
    { name = "rCollar", parent = "chestUpper", pos = { -0.0244, 0.9385, -0.0262 }, quat = { 0.0009, -0.0306, -0.0153, 0.9994 } },
    { name = "rShldrBend", parent = "rCollar", pos = { -0.1109, 0.9250, -0.0340 }, quat = { 0.0139, 0.0048, 0.4116, 0.9112 } },
    { name = "rShldrTwist", parent = "rShldrBend", pos = { -0.1661, 0.8644, -0.0332 }, quat = { 0.0124, 0.0016, 0.4130, 0.9107 } },
    { name = "rForearmBend", parent = "rShldrTwist", pos = { -0.2331, 0.7899, -0.0342 }, quat = { -0.0449, 0.1279, 0.3782, 0.9157 } },
    { name = "rForearmTwist", parent = "rForearmBend", pos = { -0.2878, 0.7335, -0.0104 }, quat = { -0.0446, 0.1275, 0.3775, 0.9161 } },
    { name = "rHand", parent = "rForearmTwist", pos = { -0.3479, 0.6660, 0.0121 }, quat = { 0.0116, 0.0006, 0.3756, 0.9267 } },
    { name = "rThumb1", parent = "rHand", pos = { -0.3517, 0.6545, 0.0243 }, quat = { -0.3402, 0.4394, 0.4259, 0.7140 } },
    { name = "rThumb2", parent = "rThumb1", pos = { -0.3590, 0.6395, 0.0485 }, quat = { -0.2963, 0.3205, 0.5022, 0.7465 } },
    { name = "rThumb3", parent = "rThumb2", pos = { -0.3653, 0.6229, 0.0596 }, quat = { -0.3808, 0.4228, 0.4046, 0.7160 } },
    { name = "rCarpal1", parent = "rHand", pos = { -0.3522, 0.6666, 0.0210 }, quat = { 0.0643, 0.1044, 0.4156, 0.9013 } },
    { name = "rIndex1", parent = "rCarpal1", pos = { -0.3892, 0.6261, 0.0374 }, quat = { 0.0601, 0.0581, 0.4306, 0.8987 } },
    { name = "rIndex2", parent = "rIndex1", pos = { -0.4066, 0.6058, 0.0420 }, quat = { 0.0310, 0.0351, 0.4323, 0.9005 } },
    { name = "rIndex3", parent = "rIndex2", pos = { -0.4159, 0.5942, 0.0438 }, quat = { 0.0280, 0.0281, 0.4422, 0.8960 } },
    { name = "rCarpal2", parent = "rHand", pos = { -0.3540, 0.6651, 0.0140 }, quat = { 0.0316, 0.0401, 0.4151, 0.9083 } },
    { name = "rMid1", parent = "rCarpal2", pos = { -0.3930, 0.6254, 0.0215 }, quat = { 0.0275, 0.0229, 0.4049, 0.9137 } },
    { name = "rMid2", parent = "rMid1", pos = { -0.4138, 0.6038, 0.0240 }, quat = { 0.0268, 0.0243, 0.4537, 0.8904 } },
    { name = "rMid3", parent = "rMid2", pos = { -0.4242, 0.5893, 0.0257 }, quat = { 0.0106, -0.0020, 0.3989, 0.9169 } },
    { name = "rCarpal3", parent = "rHand", pos = { -0.3548, 0.6646, 0.0060 }, quat = { 0.0039, -0.0153, 0.4163, 0.9091 } },
    { name = "rRing1", parent = "rCarpal3", pos = { -0.3907, 0.6273, 0.0068 }, quat = { 0.0016, -0.0316, 0.4217, 0.9062 } },
    { name = "rRing2", parent = "rRing1", pos = { -0.4088, 0.6067, 0.0063 }, quat = { 0.0187, -0.0325, 0.4241, 0.9048 } },
    { name = "rRing3", parent = "rRing2", pos = { -0.4189, 0.5946, 0.0059 }, quat = { 0.0027, -0.0174, 0.4211, 0.9068 } },
    { name = "rCarpal4", parent = "rHand", pos = { -0.3552, 0.6646, -0.0015 }, quat = { -0.0329, -0.0842, 0.4233, 0.9015 } },
    { name = "rPinky1", parent = "rCarpal4", pos = { -0.3851, 0.6270, -0.0091 }, quat = { -0.0071, -0.0579, 0.4151, 0.9079 } },
    { name = "rPinky2", parent = "rPinky1", pos = { -0.3980, 0.6128, -0.0100 }, quat = { -0.0328, -0.0495, 0.4129, 0.9088 } },
    { name = "rPinky3", parent = "rPinky2", pos = { -0.4060, 0.6035, -0.0111 }, quat = { -0.0476, -0.0428, 0.4155, 0.9073 } },
    { name = "neckLower", parent = "chestUpper", pos = { -0.0000, 0.9897, -0.0256 }, quat = { 0.0740, 0.0000, 0.0000, 0.9973 } },
    { name = "neckUpper", parent = "neckLower", pos = { -0.0001, 1.0126, -0.0135 }, quat = { 0.0740, 0.0000, 0.0000, 0.9973 } },
    { name = "head", parent = "neckUpper", pos = { -0.0000, 1.0481, -0.0140 }, quat = { 0.0061, 0.0000, 0.0000, 1.0000 } },
    { name = "upperTeeth", parent = "head", pos = { -0.0000, 1.0449, 0.0552 }, quat = { 0.0095, 0.0001, 0.0001, 1.0000 } },
    { name = "lowerJaw", parent = "head", pos = { -0.0000, 1.0590, 0.0035 }, quat = { 0.2041, 0.0000, 0.0000, 0.9790 } },
    { name = "lowerTeeth", parent = "lowerJaw", pos = { 0.0000, 1.0356, 0.0549 }, quat = { 0.1007, 0.0000, 0.0000, 0.9949 } },
    { name = "tongue01", parent = "lowerTeeth", pos = { 0.0000, 1.0271, 0.0397 }, quat = { -0.3511, 0.0000, 0.0000, 0.9363 } },
    { name = "tongue02", parent = "tongue01", pos = { 0.0000, 1.0330, 0.0478 }, quat = { -0.0913, 0.0000, 0.0000, 0.9958 } },
    { name = "tongue03", parent = "tongue02", pos = { 0.0000, 1.0345, 0.0580 }, quat = { 0.0109, 0.0000, 0.0000, 0.9999 } },
    { name = "tongue04", parent = "tongue03", pos = { 0.0000, 1.0346, 0.0676 }, quat = { -0.0598, 0.0000, 0.0000, 0.9982 } },
    { name = "lowerFaceRig", parent = "lowerJaw", pos = { -0.0000, 1.0609, 0.0028 }, quat = { 0.2041, 0.0000, 0.0000, 0.9790 } },
    { name = "lNasolabialLower", parent = "lowerFaceRig", pos = { 0.0242, 1.0228, 0.0660 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rNasolabialLower", parent = "lowerFaceRig", pos = { -0.0242, 1.0227, 0.0660 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lNasolabialMouthCorner", parent = "lowerFaceRig", pos = { 0.0366, 1.0352, 0.0712 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rNasolabialMouthCorner", parent = "lowerFaceRig", pos = { -0.0366, 1.0352, 0.0712 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lLipCorner", parent = "lowerFaceRig", pos = { 0.0253, 1.0341, 0.0735 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lLipLowerOuter", parent = "lowerFaceRig", pos = { 0.0139, 1.0301, 0.0771 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lLipLowerInner", parent = "lowerFaceRig", pos = { 0.0089, 1.0291, 0.0769 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "LipLowerMiddle", parent = "lowerFaceRig", pos = { -0.0000, 1.0261, 0.0788 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rLipLowerInner", parent = "lowerFaceRig", pos = { -0.0089, 1.0291, 0.0769 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rLipLowerOuter", parent = "lowerFaceRig", pos = { -0.0139, 1.0301, 0.0770 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rLipCorner", parent = "lowerFaceRig", pos = { -0.0253, 1.0341, 0.0735 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "LipBelow", parent = "lowerFaceRig", pos = { 0.0001, 1.0192, 0.0753 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "Chin", parent = "lowerFaceRig", pos = { 0.0000, 1.0127, 0.0679 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lCheekLower", parent = "lowerFaceRig", pos = { 0.0518, 1.0370, 0.0546 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rCheekLower", parent = "lowerFaceRig", pos = { -0.0518, 1.0370, 0.0545 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "BelowJaw", parent = "lowerFaceRig", pos = { 0.0000, 1.0069, 0.0446 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lJawClench", parent = "lowerFaceRig", pos = { 0.0549, 1.0413, 0.0135 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rJawClench", parent = "lowerFaceRig", pos = { -0.0550, 1.0412, 0.0134 }, quat = { -0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "upperFaceRig", parent = "head", pos = { -0.0000, 1.0481, -0.0140 }, quat = { 0.0061, 0.0000, 0.0000, 1.0000 } },
    { name = "rBrowInner", parent = "upperFaceRig", pos = { -0.0188, 1.1131, 0.0801 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rBrowMid", parent = "upperFaceRig", pos = { -0.0352, 1.1143, 0.0780 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rBrowOuter", parent = "upperFaceRig", pos = { -0.0497, 1.1074, 0.0674 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lBrowInner", parent = "upperFaceRig", pos = { 0.0188, 1.1131, 0.0801 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lBrowMid", parent = "upperFaceRig", pos = { 0.0352, 1.1143, 0.0781 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lBrowOuter", parent = "upperFaceRig", pos = { 0.0497, 1.1074, 0.0675 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "CenterBrow", parent = "upperFaceRig", pos = { 0.0000, 1.1114, 0.0850 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "MidNoseBridge", parent = "upperFaceRig", pos = { 0.0000, 1.0879, 0.0803 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lEyelidInner", parent = "upperFaceRig", pos = { 0.0291, 1.0944, 0.0629 }, quat = { -0.3517, -0.0000, 0.9361, 0.0000 } },
    { name = "lEyelidUpperInner", parent = "upperFaceRig", pos = { 0.0290, 1.0945, 0.0630 }, quat = { -0.1132, -0.0118, 0.0027, 0.9935 } },
    { name = "lEyelidUpper", parent = "upperFaceRig", pos = { 0.0293, 1.0945, 0.0630 }, quat = { -0.1142, 0.0000, 0.0000, 0.9935 } },
    { name = "lEyelidUpperOuter", parent = "upperFaceRig", pos = { 0.0295, 1.0945, 0.0630 }, quat = { -0.1110, 0.0128, -0.0028, 0.9937 } },
    { name = "lEyelidOuter", parent = "upperFaceRig", pos = { 0.0295, 1.0944, 0.0629 }, quat = { 0.5140, 0.0000, 0.8578, 0.0000 } },
    { name = "lEyelidLowerOuter", parent = "upperFaceRig", pos = { 0.0296, 1.0942, 0.0630 }, quat = { 0.0247, -0.1801, 0.9833, -0.0045 } },
    { name = "lEyelidLower", parent = "upperFaceRig", pos = { 0.0293, 1.0942, 0.0630 }, quat = { 0.0000, -0.1776, 0.9841, -0.0000 } },
    { name = "lEyelidLowerInner", parent = "upperFaceRig", pos = { 0.0291, 1.0942, 0.0630 }, quat = { -0.0233, -0.1771, 0.9839, 0.0042 } },
    { name = "rEyelidInner", parent = "upperFaceRig", pos = { -0.0291, 1.0945, 0.0629 }, quat = { 0.3517, -0.0000, 0.9361, -0.0000 } },
    { name = "rEyelidUpperInner", parent = "upperFaceRig", pos = { -0.0290, 1.0946, 0.0630 }, quat = { -0.1132, 0.0118, -0.0027, 0.9935 } },
    { name = "rEyelidUpper", parent = "upperFaceRig", pos = { -0.0292, 1.0946, 0.0630 }, quat = { -0.1142, 0.0000, 0.0000, 0.9935 } },
    { name = "rEyelidUpperOuter", parent = "upperFaceRig", pos = { -0.0295, 1.0946, 0.0630 }, quat = { -0.1110, -0.0128, 0.0028, 0.9937 } },
    { name = "rEyelidOuter", parent = "upperFaceRig", pos = { -0.0295, 1.0944, 0.0629 }, quat = { -0.5140, 0.0000, 0.8578, -0.0000 } },
    { name = "rEyelidLowerOuter", parent = "upperFaceRig", pos = { -0.0296, 1.0943, 0.0630 }, quat = { -0.0247, -0.1801, 0.9833, 0.0045 } },
    { name = "rEyelidLower", parent = "upperFaceRig", pos = { -0.0292, 1.0943, 0.0630 }, quat = { -0.0000, -0.1776, 0.9841, 0.0000 } },
    { name = "rEyelidLowerInner", parent = "upperFaceRig", pos = { -0.0290, 1.0943, 0.0630 }, quat = { 0.0233, -0.1771, 0.9839, -0.0042 } },
    { name = "lSquintInner", parent = "upperFaceRig", pos = { 0.0253, 1.0862, 0.0799 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lSquintOuter", parent = "upperFaceRig", pos = { 0.0470, 1.0839, 0.0694 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rSquintInner", parent = "upperFaceRig", pos = { -0.0253, 1.0862, 0.0799 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rSquintOuter", parent = "upperFaceRig", pos = { -0.0471, 1.0840, 0.0694 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lCheekUpper", parent = "upperFaceRig", pos = { 0.0521, 1.0667, 0.0671 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rCheekUpper", parent = "upperFaceRig", pos = { -0.0521, 1.0667, 0.0671 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "Nose", parent = "upperFaceRig", pos = { 0.0000, 1.0635, 0.0883 }, quat = { -0.0382, 0.0000, 0.0000, 0.9993 } },
    { name = "lNostril", parent = "upperFaceRig", pos = { 0.0095, 1.0591, 0.0845 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rNostril", parent = "upperFaceRig", pos = { -0.0094, 1.0591, 0.0844 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lLipBelowNose", parent = "upperFaceRig", pos = { 0.0103, 1.0498, 0.0798 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rLipBelowNose", parent = "upperFaceRig", pos = { -0.0102, 1.0498, 0.0797 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lLipNasolabialCrease", parent = "upperFaceRig", pos = { 0.0190, 1.0432, 0.0830 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rLipNasolabialCrease", parent = "upperFaceRig", pos = { -0.0190, 1.0432, 0.0830 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lNasolabialUpper", parent = "upperFaceRig", pos = { 0.0160, 1.0721, 0.0807 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rNasolabialUpper", parent = "upperFaceRig", pos = { -0.0160, 1.0721, 0.0806 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lNasolabialMiddle", parent = "upperFaceRig", pos = { 0.0271, 1.0565, 0.0825 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "rNasolabialMiddle", parent = "upperFaceRig", pos = { -0.0271, 1.0565, 0.0824 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "LipUpperMiddle", parent = "upperFaceRig", pos = { 0.0000, 1.0425, 0.0853 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lLipUpperOuter", parent = "upperFaceRig", pos = { 0.0143, 1.0377, 0.0814 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lLipUpperInner", parent = "upperFaceRig", pos = { 0.0083, 1.0402, 0.0826 }, quat = { -0.0000, -0.0016, -0.0009, 1.0000 } },
    { name = "rLipUpperInner", parent = "upperFaceRig", pos = { -0.0083, 1.0402, 0.0826 }, quat = { -0.0000, 0.0016, 0.0009, 1.0000 } },
    { name = "rLipUpperOuter", parent = "upperFaceRig", pos = { -0.0143, 1.0377, 0.0814 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
    { name = "lEye", parent = "head", pos = { 0.0293, 1.0943, 0.0620 }, quat = { -0.0000, -0.0000, 1.0000, 0.0000 } },
    { name = "rEye", parent = "head", pos = { -0.0293, 1.0944, 0.0620 }, quat = { 0.0000, -0.0000, 1.0000, -0.0000 } },
    { name = "lEar", parent = "head", pos = { 0.0694, 1.0804, -0.0050 }, quat = { -0.0349, 0.4969, -0.0404, 0.8662 } },
    { name = "rEar", parent = "head", pos = { -0.0694, 1.0804, -0.0051 }, quat = { -0.0349, -0.4969, 0.0404, 0.8662 } },
    { name = "lPectoral", parent = "chestLower", pos = { 0.0235, 0.8324, -0.0515 }, quat = { -0.0758, 0.1825, 0.0141, 0.9802 } },
    { name = "rPectoral", parent = "chestLower", pos = { -0.0228, 0.8323, -0.0516 }, quat = { -0.0758, -0.1825, -0.0141, 0.9802 } },
    { name = "root", parent = "", pos = { 0.0000, 0.0000, 0.0000 }, quat = { 0.0000, 0.0000, 0.0000, 1.0000 } },
}
-- END GENERATED

function _on_ready(self)
    local ch = scene.entity("char")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
-- entity-form gltf load: skinned model (the table API supports skinning too; kept on the entity path for full parity)
    graphics.gltf(ch, "local/hashimoto.glb")
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 55, 1, 55 })) })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })
    -- static ground collider (kinematic = immovable)
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 55, 1, 55 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })
    local mat = scene.get_entity("mat_caps")
    local n, nj, nlim = 0, 0, 0
    local kLimits = loadLimits()
    -- "alive at rest": static friction holds the pose, a small zero-target position motor
    -- springs joints back to rest (muscle tone), linear damping settles the sway

    for _, f in ipairs(kFits) do
        -- one RigidBody per bone; its collision is the compound of the bone's tapered capsule segments
        local bone = scene.entity(f.name)
        boneEnt[f.name] = bone
        scene.replace(bone, "Transform", { pos = f.com })
        local cols = {}
        for c, cap in ipairs(f.caps) do
            local q = math.qfromto({ 0, 1, 0 }, cap.axis)
            for i, s in ipairs(cap.seg) do
                n = n + 1
                -- cap.origin is world space, the body origin is f.com: convert to bone-local
                local off = { cap.origin[1] - f.com[1] + cap.axis[1] * (s[1] + s[2]) * 0.5,
                             cap.origin[2] - f.com[2] + cap.axis[2] * (s[1] + s[2]) * 0.5,
                             cap.origin[3] - f.com[3] + cap.axis[3] * (s[1] + s[2]) * 0.5 }
                -- visual segment; follows the falling bone
                -- local e = scene.entity(f.name .. "_" .. c .. "_" .. i)
                -- scene.replace(e, "Transform", { pos = off, quat = q })
                -- scene.replace(e, "Parent", { e = bone })
                -- scene.replace(e, "Renderable", { geom = geom(mesh.taperedCapsule({
                --    halfHeight = (s[2] - s[1]) * 0.5, bottomRadius = s[3], topRadius = s[4] })) })
                -- scene.replace(e, "MaterialRef", { material = mat })
                -- collision segment; local offset from the bone origin
                local shape = scene.entity()
                scene.replace(shape, "TaperedCapsule", { halfHeight = (s[2] - s[1]) * 0.5,
                                                       bottomRadius = s[3], topRadius = s[4] })
                local col = scene.entity()
                scene.replace(col, "Transform", { pos = off, quat = q })
                scene.replace(col, "Collider", { shape = shape })
                cols[#cols + 1] = col
            end
        end
        scene.replace(bone, "RigidBody", { colliders = cols, forceSleep = false, damping = kBodyDamping, friction = kBodyFriction })
        -- reparent the bone the rigid body
        local parent = bone
        local child = scene.get_entity("hashimoto/" .. f.name)
        print(f.name)
        scene.add_child(parent, child, { keepWorld = true })
    end
    -- perfectly stiff 6DOF weld: each shaped bone is joined to its shaped parent at the
    -- bone's joint world position (kJoints); zero limits hold the rest-pose relative pose
    local jointBy = {}
    for _, j in ipairs(kJoints) do
        jointBy[j.name] = j
    end
    for name, bone in pairs(boneEnt) do
        local j = jointBy[name]
        local parent = j and boneEnt[j.parent]
        if j and parent then
            local bq = scene.world_pose(bone).quat
            local pq = scene.world_pose(parent).quat
            local relQuat = math.qmul(math.qinv(pq), bq)
            local lim = kLimits[j.name]
            if lim then
                nlim = nlim + 1
            end
            local w = scene.entity()
            scene.replace(w, "SixDofJoint", {
                bodyA = bone,
                bodyB = parent,
                relQuat = relQuat,
                axisQuat = j.quat,
                grabPos = j.pos,
                excludeCollision = 1,
                limOn = 1,
                pyramid = 1,
                limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 },
                limAngMin = lim and { lim.x[1], lim.y[1], lim.z[1] } or { 0, 0, 0 },
                limAngMax = lim and { lim.x[2], lim.y[2], lim.z[2] } or { 0, 0, 0 },
                motorState = { 0, 0, 0, 2, 2, 2 }, -- position motor on the rotation axes only
                motorFreq = { kToneFreq, kToneFreq, kToneFreq },
                motorDamp = { kToneDamp, kToneDamp, kToneDamp },
                frictionAng = { kGripTorque, kGripTorque, kGripTorque } })
            joints[#joints + 1] = w
            nj = nj + 1
            -- joint debug axis: parented to bone at the joint point, showing the LIMIT frame
            -- (axisQuat = j.quat at creation; the frame follows body A as it moves)
            -- local bp = scene.world_pose(bone).pos
            -- local ax = mesh.createAxis({ len = 0.15 })
            -- scene.replace(ax, "Transform", { pos = math.qrot(math.qinv(bq), { j.pos[1] - bp[1], j.pos[2] - bp[2], j.pos[3] - bp[3] }),
            --                                quat = math.qmul(math.qinv(bq), j.quat) })
            -- scene.replace(ax, "Parent", { e = bone })
        end
    end

    local w = scene.entity()
    local bq = scene.world_pose(boneEnt["pelvis"]).quat
    local pq = scene.world_pose(boneEnt["abdomenLower"]).quat
    local relQuat = math.qmul(math.qinv(pq), bq)
    local lim = kLimits["pelvis"]
    if lim then
        nlim = nlim + 1
    end
    scene.replace(w, "SixDofJoint", {
        bodyA = boneEnt["pelvis"],
        bodyB = boneEnt["abdomenLower"],
        relQuat = relQuat,
        axisQuat = jointBy["hip"].quat,
        grabPos = jointBy["hip"].pos,
        excludeCollision = 1,
        limOn = 1,
        pyramid = 1,
        limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 },
        limAngMin = lim and { lim.x[1], lim.y[1], lim.z[1] } or { 0, 0, 0 },
        limAngMax = lim and { lim.x[2], lim.y[2], lim.z[2] } or { 0, 0, 0 },
        motorState = { 0, 0, 0, 2, 2, 2 }, -- position motor on the rotation axes only
        motorFreq = { kToneFreq, kToneFreq, kToneFreq },
        motorDamp = { kToneDamp, kToneDamp, kToneDamp },
        frictionAng = { kGripTorque, kGripTorque, kGripTorque } })
    joints[#joints + 1] = w
    local bp = scene.world_pose(boneEnt["pelvis"]).pos
    local ax = mesh.createAxis({ len = 0.15 })
    scene.replace(ax, "Transform", { pos = math.qrot(math.qinv(bq), { jointBy["hip"].pos[1] - bp[1], jointBy["hip"].pos[2] - bp[2], jointBy["hip"].pos[3] - bp[3] }),
                                   quat = math.qmul(math.qinv(bq), jointBy["hip"].quat) })
    scene.replace(ax, "Parent", { e = boneEnt["pelvis"] })

    -- hack to fix things that have "hip" weights (some grafts)
    scene.add_child(boneEnt["pelvis"], scene.get_entity("hashimoto/hip"), { keepWorld = true })


    --- add collision exceptions
    physics.collision_exception(boneEnt["rThighTwist"], boneEnt["pelvis"])          -- ignore
    physics.collision_exception(boneEnt["lThighTwist"], boneEnt["pelvis"])          -- ignore

    physics.collision_exception(boneEnt["pelvis"], boneEnt["abdomenUpper"])          -- ignore
    physics.collision_exception(boneEnt["abdomenLower"], boneEnt["chestLower"])          -- ignore
    physics.collision_exception(boneEnt["abdomenUpper"], boneEnt["chestUpper"])          -- ignore


    print("caps_fit: " .. n .. " capsule segments in " .. #kFits .. " bone bodies, " .. nj .. " joints (" .. nlim .. " limited)")


    -- the ui: one window, a text line, sliders, and the scene editor
    local win = uib.make_window("ragdoll demo", 320, 200, { 0, 1.5, 2.8 })
    local tab = uib.tab(win, "scene")
    uib.text(tab, "Rig settings")
    uib.bind_slider(tab, "kToneFreq", "caps.LuaScript.kToneFreq", 0, 100.0, "%.2f")
    uib.bind_slider(tab, "kToneDamp", "caps.LuaScript.kToneDamp", 0, 500.0, "%.1f")
    uib.bind_slider(tab, "kGripTorque", "caps.LuaScript.kGripTorque", 0, 100.0, "%.2f")
    uib.bind_slider(tab, "kBodyDamping", "caps.LuaScript.kBodyDamping", 0, 100.0, "%.2f")
    uib.bind_slider(tab, "kBodyFriction", "caps.LuaScript.kBodyFriction", 0, 100.0, "%.2f")
    uib.editor(uib.tab(win, "editor"))
    self.metricsText = uib.text(uib.make_window("metrics", 160, 180, { 0.45, 1.72, 2.8 }), "")
end

-- refresh the metrics window from the engine's performance string every frame
function _animate(self, dt)
    local t = scene.component(self.metricsText, "UIText")
    if t then
        t.label = engine.performance_metrics()
    end
end

-- slider handler, called only when a slider moves (or every frame — the diff no-ops if nothing changed)
function applyTone(self)
    local f, d, g = kToneFreq, kToneDamp, kGripTorque
    for _, w in ipairs(joints) do
        local j = scene.component(w, "SixDofJoint")
        j.motorFreq = { f, f, f }
        j.motorDamp = { d, d, d }
        j.frictionAng = { g, g, g }
    end
    for _, b in pairs(boneEnt) do
        local rb = scene.component(b, "RigidBody")
        rb.damping = kBodyDamping
        rb.friction = kBodyFriction
    end
end

function _physics(self, dt)
  --print(dt)
  applyTone(self)
end

