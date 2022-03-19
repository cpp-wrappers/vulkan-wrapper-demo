#version 460

#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadEXT vec3 hit_value;

hitAttributeEXT vec3 attribs;

void main() {
	hit_value = attribs;
}