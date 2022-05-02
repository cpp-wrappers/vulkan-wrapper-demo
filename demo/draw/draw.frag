#version 460

layout(set = 0, binding = 0, rgba8) uniform image2D i_canvas;

layout(push_constant) uniform u_cursor {
	vec2 pos;
};

layout(location = 0) out vec4 out_color;

void main() {
	vec4 col = imageLoad(i_canvas, ivec2(gl_FragCoord.xy));
	col.rgb -= 1.0 / 256;
	col = max(vec4(0), col);

	//if(distance(pos, gl_FragCoord.xy) < 10) {
	col = max(col, 10.0 - distance(pos, gl_FragCoord.xy));
	//}

	out_color = col;

	imageStore(i_canvas, ivec2(gl_FragCoord.xy), col);
}