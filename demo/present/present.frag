#version 450

layout(set = 0, binding = 0) uniform u_uniform_buffer {
	mat4 proj_view_inversed;
	float t;
};

layout(location = 0) in vec2 ndc_xy;

layout(location = 0) out vec4 color;

struct sphere {
	vec3 pos;
	float rad;
};

struct ray {
	vec3 pos;
	vec3 dir;
};

struct ray_sphere_intersection_result {
	float close;
	float far;
};

vec2 ray_sphere_intersection(ray r, sphere s) {
	vec3 diff = r.pos - s.pos;

	float a = dot(r.dir, r.dir);
	float b = 2.0*dot(diff, r.dir);
	float c = dot(diff, diff) - s.rad * s.rad;

	float d = b*b - 4.0*a*c;

	if(d >= 0.0) {
		float xl = (-b - sqrt(d)) / (2.0 * a);
		float xr = (-b + sqrt(d)) / (2.0 * a);
		return vec2(xl, xr);
	}

	return vec2(0.0, 0.0);
}

bool success(vec2 v) { return dot(v, v) != 0.0; }

struct layer {
	vec3 pos;
	float bottom;
	float height;
};

vec2 ray_layer_intersection(ray r, layer l, bool to_space) {
	sphere top_sphere = sphere(l.pos, l.bottom + l.height);
	sphere bot_sphere = sphere(l.pos, l.bottom);
	
	vec2 top = ray_sphere_intersection(r, top_sphere);
	vec2 bot = ray_sphere_intersection(r, bot_sphere);

	if(success(bot)) {
		if(bot[0] < 0.0 && bot[1] > 0.0) {
			return vec2(0.0, top[1]);
		}
		if(bot[0] > 0.0) {
			return vec2(max(top[0], 0.0), to_space ? top[1] : bot[0]);
		}
		if(top[1] > 0.0){
			return vec2(max(bot[1], 0.0), top[1]);
		}
	}
	else {
		return vec2(max(top[0], 0.0), top[1]);
	}
	
	return vec2(0.0, 0.0);
}

float density_ratio(float h) {
	return exp(-h/0.06);
}

float density_ratio(vec3 point, layer l) {
	return density_ratio(distance(point, l.pos) - l.bottom);
}

float od_integration(vec3 po, vec3 dir, float dist, layer l) {	
	return density_ratio(po + dir * dist / 2.0, l) * dist;
}

vec3 resulting_attenuation(ray v, vec3 star_dir, layer l, vec3 coeffs) {
	vec2 atmo_range = ray_layer_intersection(v, l, false);
	float atmo_dist = (atmo_range[1] - atmo_range[0]);

	v.pos += v.dir * atmo_range[0];
	v.pos += v.dir * atmo_dist / 2.0;

	ray ray_to_star = ray(v.pos, star_dir);
	vec2 to_star_range = ray_layer_intersection(ray_to_star, l, true);

	return
		exp(
			-density_ratio(v.pos, l)
			-coeffs * (
				od_integration(v.pos, star_dir, to_star_range[1], l)
			)
		) *
		atmo_dist *
		coeffs;
}

vec3 sky_color(vec3 dir) {
	vec3 eye_pos = vec3(0, 6.0 + 0.02, 0.);

	ray eye = ray(eye_pos, dir);

	vec3 sun_dir = vec3(cos(t / 20.0), sin(t / 20.0), 0);

	float a = dot(dir, sun_dir);
	vec3 rgb = pow(vec3(7.2, 5.7, 4.2), vec3(4.0));

	layer molecules = layer(
		vec3(0.),
		6.0,
		0.08
	);

	vec3 color = resulting_attenuation(eye, sun_dir, molecules, 25000.0/rgb);
	float sun = 0.0;
	float sun_a = 0.9999;
	float h = 0.0003;
	
	if(a > sun_a) {
		sun = 1.0;
	}
	else if(a > sun_a - h) {
		sun= (a - (sun_a - h)) / h;
	}
	sun*=15.0;
	color += color * sun;
	color *= vec3(2.3, 1.2, 3.0) / 9.0;

	return pow(color, vec3(1/2.2));
}

void main() {
	vec4 near = proj_view_inversed * vec4(ndc_xy, -1.0,  1.0);
	//vec4 far  = proj_view_inversed * vec4(ndc_xy,  1.0,  1.0);

	vec3 dir = normalize(near.xyz / near.w);//(far.xyz / far.w) - (near.xyz / near.w));

	color = vec4(sky_color(dir), 1.0);
}