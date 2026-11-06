#version 450
// Textured, coloured 2D quads and triangles for the Xenos backend: Dear ImGui's draw lists, framebuffer copies and
// restores, and clears. Position in pixels or clip space as xform maps it; depth from push constant depth.x.
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(push_constant) uniform Push { vec4 xform; vec4 depth; } pc;

void main()
{
	gl_Position = vec4(a_pos * pc.xform.xy + pc.xform.zw, pc.depth.x, 1.0);
	v_uv = a_uv;
	v_color = a_color;
}
