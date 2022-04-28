#version 460

layout(set = 0, binding = 0, rgba8) uniform image2D i_canvas;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = vec4(1.0);
}