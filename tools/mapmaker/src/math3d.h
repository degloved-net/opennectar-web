#pragma once

// Vectores y matrices 4x4 (columnas, como OpenGL e ImGuizmo) para la vista.

#include <cmath>

namespace nm {

struct V3 {
	float x = 0, y = 0, z = 0;
	V3() = default;
	V3(float a, float b, float c)
	    : x(a)
	    , y(b)
	    , z(c)
	{
	}
	V3 operator+(const V3& o) const { return { x + o.x, y + o.y, z + o.z }; }
	V3 operator-(const V3& o) const { return { x - o.x, y - o.y, z - o.z }; }
	V3 operator*(float k) const { return { x * k, y * k, z * k }; }
	float dot(const V3& o) const { return x * o.x + y * o.y + z * o.z; }
	V3 cross(const V3& o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
	float length() const { return std::sqrt(dot(*this)); }
	V3 normalized() const
	{
		const float l = length();
		return l > 0 ? *this * (1.0f / l) : *this;
	}
};

struct M4 {
	float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // columnas

	float& at(int row, int col) { return m[col * 4 + row]; }
	float at(int row, int col) const { return m[col * 4 + row]; }

	M4 operator*(const M4& o) const
	{
		M4 r;
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				float s = 0;
				for (int k = 0; k < 4; k++) s += at(i, k) * o.at(k, j);
				r.at(i, j) = s;
			}
		}
		return r;
	}
	V3 point(const V3& v) const
	{
		const float w = at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3);
		const V3 p { at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3), at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3),
			         at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) };
		return w != 0 && w != 1 ? p * (1.0f / w) : p;
	}

	static M4 translate(const V3& t)
	{
		M4 r;
		r.at(0, 3) = t.x;
		r.at(1, 3) = t.y;
		r.at(2, 3) = t.z;
		return r;
	}
	static M4 scale(float sx, float sy, float sz)
	{
		M4 r;
		r.at(0, 0) = sx;
		r.at(1, 1) = sy;
		r.at(2, 2) = sz;
		return r;
	}
	/// Mismo orden que Matrix4f::makeSRT del juego (rotación XYZ en radianes).
	static M4 srt(const V3& s, const V3& r, const V3& t)
	{
		const float sx = std::sin(r.x), sy = std::sin(r.y), sz = std::sin(r.z);
		const float cx = std::cos(r.x), cy = std::cos(r.y), cz = std::cos(r.z);
		M4 o;
		o.at(0, 0) = cy * cz * s.x;
		o.at(1, 0) = cy * sz * s.x;
		o.at(2, 0) = -sy * s.x;
		o.at(0, 1) = (sx * sy * cz - cx * sz) * s.y;
		o.at(1, 1) = (sx * sy * sz + cx * cz) * s.y;
		o.at(2, 1) = sx * cy * s.y;
		o.at(0, 2) = (cx * cz * sy + sx * sz) * s.z;
		o.at(1, 2) = (cx * sz * sy - sx * cz) * s.z;
		o.at(2, 2) = cx * cy * s.z;
		o.at(0, 3) = t.x;
		o.at(1, 3) = t.y;
		o.at(2, 3) = t.z;
		return o;
	}
	static M4 perspective(float fovY, float aspect, float zn, float zf)
	{
		const float f = 1.0f / std::tan(fovY * 0.5f);
		M4 r;
		r.at(0, 0) = f / aspect;
		r.at(1, 1) = f;
		r.at(2, 2) = (zf + zn) / (zn - zf);
		r.at(2, 3) = 2 * zf * zn / (zn - zf);
		r.at(3, 2) = -1;
		r.at(3, 3) = 0;
		return r;
	}
	static M4 ortho(float halfW, float halfH, float zn, float zf)
	{
		M4 r;
		r.at(0, 0) = 1.0f / halfW;
		r.at(1, 1) = 1.0f / halfH;
		r.at(2, 2) = -2.0f / (zf - zn);
		r.at(2, 3) = -(zf + zn) / (zf - zn);
		return r;
	}
	static M4 lookAt(const V3& eye, const V3& target, const V3& up)
	{
		const V3 f = (target - eye).normalized(), s = f.cross(up).normalized(), u = s.cross(f);
		M4 r;
		r.at(0, 0) = s.x;
		r.at(0, 1) = s.y;
		r.at(0, 2) = s.z;
		r.at(1, 0) = u.x;
		r.at(1, 1) = u.y;
		r.at(1, 2) = u.z;
		r.at(2, 0) = -f.x;
		r.at(2, 1) = -f.y;
		r.at(2, 2) = -f.z;
		r.at(0, 3) = -s.dot(eye);
		r.at(1, 3) = -u.dot(eye);
		r.at(2, 3) = f.dot(eye);
		return r;
	}
	/// Inversa general (para el rayo del ratón).
	M4 inverse() const;
};

inline M4 M4::inverse() const
{
	const float* a = m;
	float inv[16];
	inv[0]  = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
	inv[4]  = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
	inv[8]  = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
	inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
	inv[1]  = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
	inv[5]  = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
	inv[9]  = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
	inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
	inv[2]  = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
	inv[6]  = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
	inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
	inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
	inv[3]  = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
	inv[7]  = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
	inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
	inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
	const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
	M4 r;
	if (det == 0) return r;
	for (int i = 0; i < 16; i++) r.m[i] = inv[i] / det;
	return r;
}

/// Cámara del editor: orbital alrededor de un punto, libre (WASD) o cenital.
struct Camera {
	V3 target { 0, 0, 0 };
	float yaw = -0.6f, pitch = 0.75f, distance = 3200.0f;
	bool topDown   = false; ///< ortográfica desde arriba
	float topZoom  = 1800.0f; ///< medio alto de la vista cenital, en unidades
	float fovY     = 0.8f;

	V3 eye() const
	{
		const float cp = std::cos(pitch);
		return target + V3(std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp) * distance;
	}
	M4 view() const
	{
		if (topDown) return M4::lookAt(target + V3(0, 10000, 0), target, V3(0, 0, -1));
		return M4::lookAt(eye(), target, V3(0, 1, 0));
	}
	M4 projection(float aspect) const
	{
		if (topDown) return M4::ortho(topZoom * aspect, topZoom, 10.0f, 30000.0f);
		return M4::perspective(fovY, aspect, 10.0f, 60000.0f);
	}
};

struct Ray {
	V3 origin, dir;
};

/// Rayo de la cámara por el punto (nx, ny) de la vista, en [-1, 1].
inline Ray cameraRay(const Camera& cam, float aspect, float nx, float ny)
{
	const M4 inv = (cam.projection(aspect) * cam.view()).inverse();
	const V3 a = inv.point({ nx, ny, -1 }), b = inv.point({ nx, ny, 1 });
	return { a, (b - a).normalized() };
}

/// Corte con el plano horizontal y = h; false si no lo corta delante.
inline bool rayPlaneY(const Ray& r, float h, V3& hit)
{
	if (std::fabs(r.dir.y) < 1e-6f) return false;
	const float t = (h - r.origin.y) / r.dir.y;
	if (t < 0) return false;
	hit = r.origin + r.dir * t;
	return true;
}

/// Möller–Trumbore; distancia en t o -1.
inline float rayTriangle(const Ray& r, const V3& a, const V3& b, const V3& c)
{
	const V3 e1 = b - a, e2 = c - a, p = r.dir.cross(e2);
	const float det = e1.dot(p);
	if (std::fabs(det) < 1e-7f) return -1;
	const float inv = 1.0f / det;
	const V3 s      = r.origin - a;
	const float u   = s.dot(p) * inv;
	if (u < 0 || u > 1) return -1;
	const V3 q    = s.cross(e1);
	const float v = r.dir.dot(q) * inv;
	if (v < 0 || u + v > 1) return -1;
	const float t = e2.dot(q) * inv;
	return t > 0 ? t : -1;
}

constexpr float kDeg = 3.14159265f / 180.0f;

} // namespace nm
