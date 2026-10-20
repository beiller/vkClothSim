1 Normals or view vector in mismatched spaces. reflect(I, N) needs I and N in the same space (usually world). If the normal is in object/view space and the view vector in world space (or vice versa), the reflected direction is garbage that still looks “plausible”. Check what space your vertex shader outputs and whether you apply the normal matrix (transpose(inverse(model))), especially if you pass the mat4 to a vec3 incorrectly.
2 Interpolating something non-linear, or not normalizing after interpolation. If you compute the reflection in the vertex shader and interpolate the result (or UVs derived from it), the atan2 wrap seam gets interpolated across triangles, producing exactly these streaky wedges. Compute the direction and the UV in the fragment shader, and normalize() the interpolated normal/view vectors there.
3 Equirect math issues. Typical correct form:
glsl
   vec2 uv = vec2(atan(d.z, d.x) / (2.0*PI) + 0.5,
                  acos(clamp(d.y, -1.0, 1.0)) / PI);

Check d is normalized, you’re using atan(z, x) (not x, z swapped inconsistently), and the Y axis convention matches Vulkan (Y points down in clip space, and your world up may differ).
4. Mip/derivative seam artifact. The atan2 discontinuity causes huge UV derivatives at the wrap, so the sampler picks the lowest mip and you get a visible line. Fix with textureGrad using derivatives computed from a continuous version of the direction, or sample with textureLod(..., 0). This explains a seam line but not the scrambling, so treat it as a secondary issue.
5. Descriptor/sampler problems (less likely here). Wrong image view (e.g. sampling a single array layer or mip), VK_SAMPLER_ADDRESS_MODE_* set to something odd, or a cubemap image view mixed with a 2D sampler. If you converted the equirect to a cubemap, a wrong face order/orientation or a view type of 2D_ARRAY vs CUBE produces exactly this “jigsaw of correct faces” look. If you’re using a cubemap, this becomes my top suspect: the pieces look like face-sized chunks placed with wrong orientation.
6. Format issues. Worth checking but unlikely to cause this: R32G32B32_SFLOAT isn’t widely supported for sampling (use R16G16B16A16_SFLOAT or R32G32B32A32_SFLOAT), and UNORM vs SRGB would only change tone, not layout. Also check your staging copy’s bufferRowLength/bufferImageHeight and image extent: a wrong row pitch makes the image shear, not rearrange into clean wedges, so this is less probable.

Quick debugging steps (fastest first)

Output normalize(N) * 0.5 + 0.5 as color. You should see a smooth rainbow-ish sphere. If not, it’s your normals/space.
Output the reflected direction as color the same way. It should vary smoothly with no hard edges. Seams here mean the vector math is wrong.
Replace the env lookup with a procedural sky (mix by d.y). If the sphere then looks right, the direction is fine and the problem is the texture/sampling/cubemap side.
Display the HDRI on a fullscreen quad with plain UVs. If it’s correct, the upload path is fine.
Run with validation layers and VK_LAYER_KHRONOS_validation sync validation enabled.