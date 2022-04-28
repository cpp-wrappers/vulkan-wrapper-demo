#version 460

struct gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	float x = float(gl_VertexIndex / 2) * 2.0 - 1.0;
	float y = float(gl_VertexIndex % 2) * 2.0 - 1.0;

	gl_Position = vec4(x, y, 0, 1);
}