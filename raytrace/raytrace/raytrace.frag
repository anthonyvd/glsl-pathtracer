#version 400

in vec4 gl_FragCoord;
layout(location = 0) out vec4 color;



uniform int samples_per_round;
uniform int frame_count;
uniform sampler2D partial_render;

// settings
const float kTMax = 100000;
const float kTMin = 0.001;

const int kWidth = 960;
const int kHeight = 540;
const int kMaxDepth = 64;
const int kMaxRaymarchSteps = 64;
const float kEpsilon = 0.0001;

const float kAspectRatio = 16.0 / 9.0;
const vec3 kOrigin = vec3(0, 1, -2);
const vec3 kLookAt = vec3(0, 0, 0);
const vec3 kUp = vec3(0, 1, 0);

// Random numbers
struct lcg_t {
	uint mod;
	uint a;
	uint c;
	uint next;
};
lcg_t global_lcg;

lcg_t init_lcg(uint seed) {
	return lcg_t(2147483647, 48271, 0, seed);
}

lcg_t adv(lcg_t g) {
	g.next = (g.a * g.next + g.c) % g.mod;
	return g;
}

#define RADV(g) (g.next = (g.a * g.next + g.c) % g.mod)

float lcg_rfloat() {
	RADV(global_lcg);
	float n = float(global_lcg.next & 0xFFFFFF);
	return fract(sin(n));
}

float rfloat() {
	return lcg_rfloat();
}

float rfloat(float min, float max) {
	return ((rfloat() * (max - min)) + min);
}

vec3 rand_vec(float min, float max) {
	return (vec3(rfloat(min, max), rfloat(min, max), rfloat(min, max)));
}

vec3 rand_vec_polar(float theta, float phi, float r) {
	return vec3(r) * 
		   vec3(sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi));
}

vec3 polar_rand_in_unit_sphere() {
	return rand_vec_polar( 
		rfloat(0.0, 2.0 * 3.14159265359),
		acos(sign(rfloat()) * rfloat()), 
		pow(rfloat(), 1.0/3.0));
}

vec3 rejection_rand_in_unit_sphere() {
	while(true) {
		vec3 v = vec3(
			sign(rfloat()) * rfloat(),
			sign(rfloat()) * rfloat(),
			sign(rfloat()) * rfloat());
		if (dot(v, v) >= 1.0) continue;
		return v;
	}
}

vec3 rand_in_unit_sphere() {
	//return rejection_rand_in_unit_sphere();
	return polar_rand_in_unit_sphere();
}

vec3 rand_unit_vector() {
	return normalize(rand_in_unit_sphere());
}

vec3 rand_in_hemisphere(vec3 normal) {
	vec3 v = rand_in_unit_sphere();
	if (dot(v, normal) > 0.0) return v; // TODO: Change to sign()?
	return -v;
}

struct camera_t {
	vec3 origin;
	vec3 ll;
	vec3 hor;
	vec3 ver;
};

camera_t create_camera(vec3 origin, vec3 look_at, vec3 up, float aspect_ratio, float vertical_fov) {
	float viewport_height = 2.0 * tan(radians(vertical_fov) / 2.0);
	float viewport_width = aspect_ratio * viewport_height;

	vec3 w = normalize(origin - look_at);
	vec3 u = normalize(cross(up, w));
	vec3 v = cross(w, u);

	vec3 hor = viewport_width * u;
	vec3 ver = viewport_height * v;
	vec3 ll = origin - hor / 2.0 - ver / 2.0 - w;
	
	return camera_t(origin, ll, hor, ver);
}

const int kNoMaterial = 0;
const int kLambertianMat = kNoMaterial;
const int kMetalMat = kLambertianMat + 1;
const int kDielectricMat = kMetalMat + 1;
const int kEmitterMat = kDielectricMat + 1;

struct lambertian_mat_t {
	vec3 albedo;
};

struct metal_mat_t {
	vec3 albedo;
	float fuzz;
};

struct dielectric_mat_t {
	float ir;
};

struct emitter_mat_t {
	vec3 color;
};

struct mat_t {
	int material_type;

	lambertian_mat_t l;
	metal_mat_t m;
	dielectric_mat_t d;
	emitter_mat_t e;
};

#define MAKE_LAMB(albedo) (mat_t(kLambertianMat, lambertian_mat_t(albedo), metal_mat_t(vec3(0), 0), dielectric_mat_t(0), emitter_mat_t(vec3(0))))
#define MAKE_METAL(albedo, fuzz) (mat_t(kMetalMat, lambertian_mat_t(vec3(0)), metal_mat_t(albedo, fuzz), dielectric_mat_t(0), emitter_mat_t(vec3(0))))
#define MAKE_DIEL(ir) (mat_t(kDielectricMat, lambertian_mat_t(vec3(0)), metal_mat_t(vec3(0), 0), dielectric_mat_t(ir), emitter_mat_t(vec3(0))))
#define MAKE_EMIT(c) (mat_t(kEmitterMat, lambertian_mat_t(vec3(0)), metal_mat_t(vec3(0), 0), dielectric_mat_t(0), emitter_mat_t(c)))

mat_t make_no_material() {
	return mat_t(kNoMaterial, lambertian_mat_t(vec3(0)), metal_mat_t(vec3(0), 0), dielectric_mat_t(0), emitter_mat_t(vec3(0)));
}

struct ray_t {
	vec3 origin;
	vec3 direction;
};

ray_t shoot_ray(camera_t c, float s, float t) {
	return ray_t(c.origin, normalize(c.ll + s * c.hor + t * c.ver - c.origin));
}

struct hit_t {
	bool hit;
	vec3 pos;
	vec3 normal;
	float t;
	bool front_face;
	mat_t material;
	vec3 sdf_reflected_offset;
	vec3 sdf_transmitted_offset;
};

struct sphere_t {
	vec3 center;
	float radius;
	mat_t material;
};

struct scatter_t {
	bool scattered;
	vec3 attenuation;
	vec3 emitter_color;
	ray_t scattered_ray;
};

const float s = 1e-8;
bool near_zero(vec3 v) {
	return (abs(v.x) < s) && (abs(v.y) < s) && (abs(v.z) < s);
}

scatter_t scatter_lambertian(ray_t r, hit_t h) {
	vec3 scatter_direction = rand_in_hemisphere(h.normal);
	
	if (near_zero(scatter_direction)) {
		scatter_direction = h.normal;
	}
	
	ray_t scattered_ray = ray_t(h.pos + h.sdf_reflected_offset, normalize(scatter_direction));

	return scatter_t(true, h.material.l.albedo * max(0, dot(h.normal, scattered_ray.direction)), vec3(0), scattered_ray);
}

vec3 reflect(vec3 v, vec3 n) {
	return v - 2 * dot(v, n) * n;
}

scatter_t scatter_metal(ray_t r, hit_t h) {
	vec3 reflected = reflect(normalize(r.direction), h.normal);
	ray_t scattered = ray_t(h.pos + h.sdf_reflected_offset, normalize(reflected + h.material.m.fuzz * rand_in_unit_sphere()));
	return scatter_t(dot(scattered.direction, h.normal) > 0, h.material.m.albedo, vec3(0), scattered);
}

vec3 refract(vec3 unit_direction, vec3 normal, float etai_over_etat) {
	float cos_theta = min(dot(-unit_direction, normal), 1.0);
	vec3 perpendicular = (etai_over_etat * (unit_direction + cos_theta * normal));
	return perpendicular + (-sqrt(abs(1.0 - dot(perpendicular, perpendicular))) * normal);
}

float schlick_reflectance(float cosine, float ref_idx) {
	float r0 = (1.0 - ref_idx) / (1.0 + ref_idx);
	r0 = r0 * r0;
	return r0 + (1.0 - r0) * pow(1.0 - cosine, 5.0);
}

scatter_t scatter_dielectric(ray_t r, hit_t h) {
	float refraction_ratio = h.front_face ? 1.0/h.material.d.ir : h.material.d.ir;

	vec3 unit_direction = normalize(r.direction);
	float cos_theta = min(dot(-unit_direction, h.normal), 1.0);
	float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
	bool cannot_refract = (refraction_ratio * sin_theta) > 1.0;
	vec3 direction;

	if (cannot_refract || schlick_reflectance(cos_theta, refraction_ratio) > rfloat()) {
		return scatter_t(true, vec3(1.0), vec3(0), ray_t(h.pos + h.sdf_reflected_offset, normalize(reflect(unit_direction, h.normal))));
	}
	return scatter_t(true, vec3(1.0), vec3(0), ray_t(h.pos + h.sdf_transmitted_offset, normalize(refract(unit_direction, h.normal, refraction_ratio))));
}

scatter_t scatter_emitter(ray_t r, hit_t h) {
	return scatter_t(false, vec3(0), h.material.e.color, ray_t(vec3(0), vec3(0)));
}

scatter_t scatter(ray_t r, hit_t h) {
	switch (h.material.material_type) {
		case kLambertianMat:
			return scatter_lambertian(r, h);
		case kMetalMat:
			return scatter_metal(r, h);
		case kDielectricMat:
			return scatter_dielectric(r, h);
		case kEmitterMat:
			return scatter_emitter(r, h);
	}
	return scatter_t(false, vec3(0), vec3(0), ray_t(vec3(0), vec3(0)));
}

const sphere_t kSpheres[] = sphere_t[](
	// Lambertian spheres
	sphere_t(vec3(0, 0, 2), 0.5, MAKE_LAMB(vec3(0.7, 0.3, 0.3))),
	sphere_t(vec3( 0, -100.5, 0), 100, /*MAKE_METAL(vec3(0.8, 0.6, 0.2), 1.0) )*/MAKE_LAMB(vec3(0.8, 0.8, 0))),
	// Metal spheres
	sphere_t(vec3(-1,      0,   0), 0.5, MAKE_METAL(vec3(0.8, 0.8, 0.8), 0.3)),
	sphere_t(vec3( 1,      0,   0), 0.5, MAKE_METAL(vec3(0.8, 0.6, 0.2), 1.0)),
	// Dielectric spheres
	 sphere_t(vec3(0, 0, 0), 0.5, MAKE_DIEL(1.5)),
	// Emitter spheres
	sphere_t(vec3(0, 3, 0), 2, MAKE_EMIT(vec3(2)))
	//sphere_t(vec3(-3,         5,   -1), 2, MAKE_EMIT(vec3(1)))
);

hit_t hit_sphere(float t_min, float t_max, sphere_t s, ray_t r) {
	hit_t ret = hit_t(false, vec3(0), vec3(0), 0.0, false, make_no_material(), vec3(0), vec3(0));

	vec3 oc = r.origin - s.center;
	float a = dot(r.direction, r.direction);
	float b = 2.0 * dot(oc, r.direction);
	float c = dot(oc, oc) - s.radius * s.radius;
	float discriminant = b * b - 4.0 * a * c;

	if (discriminant < 0) {
		// No hit
		return ret;
	}
	
	float sqrtd = sqrt(discriminant);
	float denom = (2.0 * a);

	ret.t = (-b - sqrtd) / denom;
	if (ret.t >= t_min && ret.t <= t_max) {
		ret.pos = r.origin + ret.t * r.direction;
		ret.normal = normalize(ret.pos - s.center);

		float front_face = dot(r.direction, ret.normal);
		ret.normal = (-sign(front_face)) * ret.normal;
		ret.front_face = front_face < 0;

		ret.material = s.material;

		ret.hit = true;
		return ret;
	}

	ret.t = (-b + sqrtd) / denom;
	if (ret.t >= t_min && ret.t <= t_max) {
		ret.pos = r.origin + ret.t * r.direction;
		ret.normal = normalize(ret.pos - s.center);

		float front_face = dot(r.direction, ret.normal);
		ret.normal = (-sign(front_face)) * ret.normal;
		ret.front_face = front_face < 0;

		ret.material = s.material;

		ret.hit = true;
		return ret;
	}

	return ret;
}

// This kinda breaks
vec3 twist_sdf(vec3 p) {
	const float k = 2.0;
	float c = cos(k * p.y);
	float s = sin(k * p.y);
	mat2 m = mat2(c, -s, s, c);
	vec3 q = vec3(m * p.xz, p.y);

	return q;
}

// Using this crashes
vec3 repeat_inf_sdf(vec3 p, vec3 c) {
	return mod(p + 0.5 * c, c) - 0.5 * c;
}

vec3 repeat_lim_sdf(vec3 p, float c, vec3 l) {
	return p - c * clamp(round(p / c), -l, l);
}

float neg_sdf(float s) {
	return -s;
}

float sub_sdf(float a, float b) {
	return max(-a, b);
}

float union_sdf(float a, float b) {
	return min(a, b);
}

float sphere_sdf(vec3 p, float radius) {
	return length(p) - radius;
}

float box_sdf(vec3 p, vec3 bounds) {
	vec3 q = abs(p) - bounds;
	return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float octahedron_bound_sdf(vec3 p, float s) {
	p = abs(p);
	return (p.x + p.y + p.z - s) * 0.57735027;
}

float torus_sdf(vec3 p, vec2 t) {
	vec2 q = vec2(length(p.xz) - t.x, p.y);
	return length(q) - t.y;
}

float plane_sdf(vec3 p, vec3 n, float h) {
	return dot(p, n) + h;
}

float scene_sdf(vec3 p) {
	return neg_sdf(box_sdf(p, vec3(10, 10, 10)));
	//return sphere_sdf(p, 0.5);
	//return sub_sdf(box_sdf(p, vec3(0.25, 0.25, 0.25)), octahedron_bound_sdf(p, 0.5));
	//return union_sdf(sub_sdf(sphere_sdf(p, 1.2), box_sdf(p, vec3(1, 1, 1))), octahedron_bound_sdf(p, 1));
	//return octahedron_bound_sdf(p, 0.5);
	return kTMax + 1;
}

const vec3 kOffsets[] = vec3[](
	vec3(kEpsilon, 0.0, 0.0),
	vec3(0.0, kEpsilon, 0.0),
	vec3(0.0, 0.0, kEpsilon));

vec3 estimate_sdf_normal(vec3 p) {
	return normalize(vec3(
		scene_sdf(p + kOffsets[0]) - scene_sdf(p - kOffsets[0]),
		scene_sdf(p + kOffsets[1]) - scene_sdf(p - kOffsets[1]),
		scene_sdf(p + kOffsets[2]) - scene_sdf(p - kOffsets[2])));
}

hit_t eval_sdf(ray_t r, float t) {
	vec3 pos = r.origin + t * r.direction;
	float dist = scene_sdf(pos);
	if (abs(dist) < kEpsilon) {
		vec3 normal = estimate_sdf_normal(pos);

		float front_face = dot(r.direction, normal);
		normal = (-sign(front_face)) * normal;

		return hit_t(true, pos, normal, t, front_face < 0, 
			//MAKE_DIEL(1.5)  
			//MAKE_METAL(vec3(0.8, 0.8, 0.8), 0.3) 
			MAKE_LAMB(vec3(0.7, 0.3, 0.3))
			, 
			//vec3(0), vec3(0));
			kEpsilon * normal, -(2.0 * kEpsilon) * normal);
	}

	return hit_t(false, vec3(0), vec3(0), dist, false, make_no_material(), vec3(0), vec3(0));
}

void main() {
	global_lcg = init_lcg(((frame_count << 20) | (int(gl_FragCoord.x) << 10) | int(gl_FragCoord.y)));

	camera_t c = create_camera(kOrigin, kLookAt, kUp, kAspectRatio, 60.0);

	vec3 color_accumulator = texture2D(partial_render, vec2(gl_FragCoord.x / float(kWidth), gl_FragCoord.y / float(kHeight))).xyz;
	color_accumulator *= color_accumulator;
	color_accumulator *= ((frame_count - 1) * samples_per_round);

	for (int i = 0; i < samples_per_round; ++i) {
		float du = rfloat();
		float dv = rfloat();

		float u = (gl_FragCoord.x + du) / (kWidth - 1.0);
		float v = (gl_FragCoord.y + dv) / (kHeight - 1.0);
		ray_t r = shoot_ray(c, u, v);

		vec3 path_color = vec3(0);
		int current_depth = 0;
		vec3 attenuation = vec3(1.0);

		while (current_depth < kMaxDepth) {
			hit_t hit = hit_t(false, vec3(0), vec3(0), kTMax, false, make_no_material(), vec3(0), vec3(0));
			
			for (int i = 0; i < kSpheres.length(); ++i) {
				hit_t h = hit_sphere(kTMin, kTMax, kSpheres[i], r);
				if (h.hit && h.t < hit.t)
					hit = h;
			}
			
			float march_dist = 0;
			for (int i = 0; i < kMaxRaymarchSteps && march_dist <= kTMax; ++i) {
				hit_t h = eval_sdf(r, march_dist);
				if (h.hit && h.t < hit.t) {
					hit = h;
					break;
				}

				march_dist += abs(h.t);
			}
			
			if (hit.hit) {
				vec3 norm = hit.normal;
				// Color according to normals:
				//path_color = 0.5 * vec3(norm.x + 1, norm.y + 1, norm.z + 1);
				
				scatter_t s = scatter(r, hit);
				if (s.scattered) {
					r = s.scattered_ray;
					attenuation *= s.attenuation;
				} else {
					path_color = s.emitter_color;
					break;
				}
			} else {
				// blue gradient background
				float t = 0.5 * (r.direction.y + 1.0);
				path_color = (1.0 - t) * vec3(1, 1, 1) + t * vec3(0.5, 0.7, 1.0);
				//path_color = vec3(0);
				break;
			}

			current_depth++;
		}

		color_accumulator += (attenuation * path_color);
	}

	float total_samples = samples_per_round * frame_count;

	vec3 col = vec3(sqrt(color_accumulator.x / total_samples), sqrt(color_accumulator.y / total_samples), sqrt(color_accumulator.z / total_samples));
	color = vec4(col, 1.0);
}