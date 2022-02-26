#version 450

out gl_PerVertex {
	vec4 gl_Position;
};

layout(location = 0) out vec2 ndc_xy;

void main() {
	vec2 positions[] = {
		vec2( 1, -1),
		vec2(-1, -1),
		vec2( 1,  1),
		vec2(-1,  1)
	};

	ndc_xy = positions[gl_VertexIndex];
	gl_Position = vec4(positions[gl_VertexIndex], 0, 1);
}