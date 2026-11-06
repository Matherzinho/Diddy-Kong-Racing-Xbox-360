#version 450
// Fast3D vertex shader for the Xenos backend (gfx_xenos.cpp). The interpreter packs each vertex as its shader needs
// (position, then per texture its coordinates and clamp limits, fog, grayscale, then the combiner inputs); the backend
// points each input below at its place in that packing, or at the position for one the shader doesn't use, so one
// vertex shader serves every combiner. xost maps location 0 to POSITION, 1 to COLOR and 2 on to TEXCOORD0 on.
layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec4 a_in1;
layout(location = 2) in vec2 a_uv0;
layout(location = 3) in vec2 a_uv1;
layout(location = 4) in float a_clamp_s0;
layout(location = 5) in float a_clamp_t0;
layout(location = 6) in float a_clamp_s1;
layout(location = 7) in float a_clamp_t1;
layout(location = 8) in vec4 a_fog;
layout(location = 9) in vec4 a_gray;
layout(location = 10) in vec4 a_in2;
layout(location = 11) in vec4 a_in3;
layout(location = 12) in vec4 a_in4;
layout(location = 13) in vec4 a_in5;
layout(location = 14) in vec4 a_in6;
layout(location = 15) in vec4 a_in7;

layout(location = 0) out vec4 v_uv;     // uv0.xy, uv1.xy
layout(location = 1) out vec4 v_clamp;  // clamp limits s0, t0, s1, t1
layout(location = 2) out vec4 v_fog;
layout(location = 3) out vec4 v_gray;
// The vertex's part of each combiner operand (fast3d.frag): cycle 1's a - b, c and d, then cycle 2's.
layout(location = 4) out vec4 v_ab0;
layout(location = 5) out vec4 v_c0;
layout(location = 6) out vec4 v_d0;
layout(location = 7) out vec4 v_ab1;
layout(location = 8) out vec4 v_c1;
layout(location = 9) out vec4 v_d1;

// XenosVertexConstants in gfx_xenos.h.
layout(push_constant) uniform Push {
	// x: 1 to draw at prim_depth (G_ZS_PRIM), y: that depth, 0 to 1.
	vec4 prim;
	// Operand o's weight of input s (0 to 6) and its constant term (7): w[o * 8 + s], rgb colour, a alpha.
	vec4 w[48];
} pc;

#define SEL(o) (a_in1 * pc.w[(o) * 8] + a_in2 * pc.w[(o) * 8 + 1] + a_in3 * pc.w[(o) * 8 + 2] + \
	a_in4 * pc.w[(o) * 8 + 3] + a_in5 * pc.w[(o) * 8 + 4] + a_in6 * pc.w[(o) * 8 + 5] + a_in7 * pc.w[(o) * 8 + 6] + \
	pc.w[(o) * 8 + 7])

void main()
{
	vec4 p = a_pos;
	// Prim depth is one depth for the whole draw: set in clip space, as the pixel shader writing depth would keep
	// the GPU's hierarchical depth off.
	p.z = mix(p.z, pc.prim.y * p.w, pc.prim.x);
	// dkr360: Golden Balloon clips at the near plane on the CPU and holds z to w at the far one (gfx_pc_dkr.c,
	// dkr_emit_tri), so depth goes through as it comes, 0 to 1.
	gl_Position = p;
	v_uv = vec4(a_uv0, a_uv1);
	v_clamp = vec4(a_clamp_s0, a_clamp_t0, a_clamp_s1, a_clamp_t1);
	v_fog = a_fog;
	v_gray = a_gray;
	v_ab0 = SEL(0);
	v_c0 = SEL(1);
	v_d0 = SEL(2);
	v_ab1 = SEL(3);
	v_c1 = SEL(4);
	v_d1 = SEL(5);
}
