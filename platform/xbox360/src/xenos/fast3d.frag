#version 450
// Fast3D pixel shader template for the Xenos backend (gfx_xenos.cpp). CMake writes variants of it with defines
// after the #version line (src/fast/CMakeLists.txt):
//   TP0, TP1  three-point filtering of texture 0, 1      CYC2   the second combiner cycle
//   KILL      texture edge / alpha threshold may discard  NOISE  the combiner reads noise or dithers alpha
//   T0, T1    the combiner reads texture 0, 1             MASK0  texture 0 has a mask (and maybe a blend) over it
// It follows libultraship's default.shader.glsl: per cycle, (a - b) * c + d for colour and alpha, then fog, texture
// edge, noise, grayscale, alpha threshold and invisible. xost lowers no function calls and no discard beside another
// branch, so the shader is branch-free, each of a - b, c and d a weighted sum of the combiner's sources with the
// weights worked out on the CPU from the combiner's mode (BuildCombinerConstants in gfx_xenos.cpp).
//
// The sources a vertex carries (the combiner's inputs 1-7 and the constants 0 and 1) are summed in the vertex
// shader, which hands each operand's part over as one interpolant (v_ab0 to v_d1): a weighted sum of interpolated
// values is the interpolated weighted sum, and it costs a vertex what it would cost every pixel. Here each operand
// adds what only a pixel has, the texels, the first cycle's result and the noise. Each weight is a vec4: rgb the
// colour combiner's, a the alpha combiner's, so one sum serves both.
layout(location = 0) in vec4 v_uv;
layout(location = 1) in vec4 v_clamp;
layout(location = 2) in vec4 v_fog;
layout(location = 3) in vec4 v_gray;
layout(location = 4) in vec4 v_ab0;
layout(location = 5) in vec4 v_c0;
layout(location = 6) in vec4 v_d0;
#ifdef CYC2
// Last, so a one-cycle variant is drawn with three interpolators fewer (xsdk:docs/HARDWARE.md, "Interpolators a pixel
// shader doesn't read").
layout(location = 7) in vec4 v_ab1;
layout(location = 8) in vec4 v_c1;
layout(location = 9) in vec4 v_d1;
#endif
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = 0) uniform sampler2D u_tex0;
layout(set = 0, binding = 1) uniform sampler2D u_tex1;
// Ship of Harkinian's blended textures (Gfx_RegisterBlendedTexture): where the mask's alpha is set, texture 0 shows
// the blend texture, or nothing (libultraship's default.shader.glsl, o_masks and o_blend). The pause screen's dungeon
// maps colour the rooms seen this way.
layout(set = 0, binding = 2) uniform sampler2D u_mask0;
layout(set = 0, binding = 3) uniform sampler2D u_blend0;

// XenosPixelConstants in gfx_xenos.h. sizes and misc change a draw, the rest with the combiner.
layout(push_constant) uniform Push {
	vec4 sizes;    // texture 0 width, height, texture 1 width, height
	vec4 misc;     // frame count, noise scale, cycle 2 alpha wrap (0 narrow, 1 wide), cycle 2 colour wrap
	vec4 opts0;    // alpha, fog, texture edge, noise
	vec4 opts1;    // alpha threshold, invisible, grayscale, texture 0's blend (1) or none (0) under its mask
	vec4 clamps;   // clamp s0, t0, s1, t1: 0 or 1
	// Operand o (0 a - b, 1 c, 2 d of cycle 1; 3 to 5 cycle 2's), source s: w[o * 6 + s], s 0 texel0, 1 texel0
	// alpha, 2 texel1, 3 texel1 alpha, 4 the first cycle's result, 5 noise.
	vec4 w[36];
} pc;

#define WRAP(x, lo, hi) (mod((x) - (lo), (hi) - (lo)) + (lo))

// Three-point filtering as libultraship's filter3point; every value named once, as xost wants with a discard. The
// second and third texels sit one texel from the first, towards the triangle the sample is in: sign(o) is 1 - 2s
// there, where |o| isn't 0 (oot360: 15 fewer instructions a texture than computing each coordinate apart).
#define THREE_POINT(P_, tex, uv, size) \
	vec2 P_##i = vec2(1.0) / (size); \
	vec2 P_##f = fract((uv) * (size) - vec2(0.5)); \
	float P_##s = step(1.0, P_##f.x + P_##f.y); \
	vec2 P_##o = P_##f - vec2(P_##s); \
	vec2 P_##b = (uv) - P_##o * P_##i; \
	vec2 P_##d = (1.0 - 2.0 * P_##s) * P_##i; \
	vec4 P_##c0 = texture(tex, P_##b); \
	vec4 P_##c1 = texture(tex, vec2(P_##b.x + P_##d.x, P_##b.y)); \
	vec4 P_##c2 = texture(tex, vec2(P_##b.x, P_##b.y + P_##d.y)); \
	vec2 P_##w = abs(P_##o); \
	vec4 P_ = P_##c0 + P_##w.x * (P_##c1 - P_##c0) + P_##w.y * (P_##c2 - P_##c0);

// Each operand's pixel terms, only those the variant can weigh. The second cycle reads texel0 and texel1 swapped, as
// libultraship does, so there texel0's weights take t1.
#ifdef T0
#define TEX0_C1(o) + t0 * pc.w[(o) * 6] + vec4(t0.a) * pc.w[(o) * 6 + 1]
#define TEX0_C2(o) + t0 * pc.w[(o) * 6 + 2] + vec4(t0.a) * pc.w[(o) * 6 + 3]
#else
#define TEX0_C1(o)
#define TEX0_C2(o)
#endif
#ifdef T1
#define TEX1_C1(o) + t1 * pc.w[(o) * 6 + 2] + vec4(t1.a) * pc.w[(o) * 6 + 3]
#define TEX1_C2(o) + t1 * pc.w[(o) * 6] + vec4(t1.a) * pc.w[(o) * 6 + 1]
#else
#define TEX1_C1(o)
#define TEX1_C2(o)
#endif
#ifdef NOISE
#define NOISE_T(o) + vec4(noise) * pc.w[(o) * 6 + 5]
#else
#define NOISE_T(o)
#endif
#define OP1(v, o) ((v) TEX0_C1(o) TEX1_C1(o) NOISE_T(o))
#define OP2(v, o) ((v) TEX0_C2(o) TEX1_C2(o) + comb * pc.w[(o) * 6 + 4] NOISE_T(o))

void main()
{
#ifdef T0
	vec2 size0 = pc.sizes.xy;
	// clamp() takes constant bounds on xost; min and max take any.
	vec2 uv0 = mix(v_uv.xy, min(max(v_uv.xy, vec2(0.5) / size0), v_clamp.xy), pc.clamps.xy);
#ifdef TP0
	THREE_POINT(t0, u_tex0, uv0, size0)
#else
	vec4 t0 = texture(u_tex0, uv0);
#endif
#ifdef MASK0
	t0 = mix(t0, texture(u_blend0, uv0) * pc.opts1.w, texture(u_mask0, uv0).a);
#endif
#endif
#ifdef T1
	vec2 size1 = pc.sizes.zw;
	vec2 uv1 = mix(v_uv.zw, min(max(v_uv.zw, vec2(0.5) / size1), v_clamp.zw), pc.clamps.zw);
#ifdef TP1
	THREE_POINT(t1, u_tex1, uv1, size1)
#else
	vec4 t1 = texture(u_tex1, uv1);
#endif
#endif

#ifdef NOISE
	// A hash without sin, whose range on Xenos is about -pi to pi (xsc.md, "Approximate builtins").
	vec3 np0 = fract(vec3(floor(gl_FragCoord.xy * pc.misc.y), pc.misc.x) * vec3(0.1031, 0.1030, 0.0973));
	vec3 np = np0 + vec3(dot(np0, np0.yzx + vec3(33.33)));
	float noise = fract((np.x + np.y) * np.z);
#endif

	// Cycle 1: colour in rgb, alpha in a.
	vec4 r1 = OP1(v_ab0, 0) * OP1(v_c0, 1) + OP1(v_d0, 2);

#ifdef CYC2
	// Cycle 2 reads the first cycle's result wrapped.
	vec4 comb = vec4(mix(WRAP(r1.rgb, vec3(-0.51), vec3(1.51)), WRAP(r1.rgb, vec3(-1.01), vec3(1.01)), pc.misc.w),
		mix(WRAP(r1.a, -0.51, 1.51), WRAP(r1.a, -1.01, 1.01), pc.misc.z));
	vec4 combined = OP2(v_ab1, 3) * OP2(v_c1, 4) + OP2(v_d1, 5);
#else
	vec4 combined = r1;
#endif

	vec4 texel = clamp(WRAP(combined, vec4(-0.51), vec4(1.51)), 0.0, 1.0);
	float use_alpha = pc.opts0.x;
	float alpha0 = mix(1.0, texel.a, use_alpha);
	vec3 fogged = mix(texel.rgb, mix(texel.rgb, v_fog.rgb, v_fog.a), pc.opts0.y);

	float edge = pc.opts0.z * use_alpha;
	float edge_kill = edge * step(alpha0, 0.19);
	float alpha1 = mix(alpha0, 1.0, edge);

#ifdef NOISE
	float noisy = floor(clamp(noise + alpha1, 0.0, 1.0));
	float alpha2 = mix(alpha1, alpha1 * noisy, pc.opts0.w * use_alpha);
#else
	float alpha2 = alpha1;
#endif

	float intensity = (fogged.r + fogged.g + fogged.b) / 3.0;
	vec3 shaded = mix(fogged, mix(fogged, v_gray.rgb * intensity, v_gray.a), pc.opts1.z);

	float threshold_kill = pc.opts1.x * use_alpha * step(alpha2, 8.0 / 256.0 - 0.0001);
	float alpha3 = mix(alpha2, 0.0, pc.opts1.y * use_alpha);
	o_color = vec4(shaded, alpha3);
#ifdef KILL
	if (edge_kill + threshold_kill > 0.5)
		discard;
#endif
}
