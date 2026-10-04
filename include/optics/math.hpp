#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace optics {
constexpr double pi = 3.14159265358979323846;
constexpr double deg = pi / 180.0;
struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
    Vec3 operator-(Vec3 b) const {
        return {x - b.x, y - b.y, z - b.z};
    }
    Vec3 operator-() const {
        return {-x, -y, -z};
    }
    Vec3 operator*(double s) const {
        return {x * s, y * s, z * s};
    }
    Vec3 operator/(double s) const {
        return *this * (1 / s);
    }
    double dot(Vec3 b) const {
        return x * b.x + y * b.y + z * b.z;
    }
    Vec3 cross(Vec3 b) const {
        return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x};
    }
    double norm2() const {
        return dot(*this);
    }
    double norm() const {
        return std::sqrt(norm2());
    }
    Vec3 unit() const {
        double n = norm();
        if (n < 1e-15)
            throw std::invalid_argument("Zero direction");
        return *this / n;
    }
};
inline Vec3 operator*(double s, Vec3 a) {
    return a * s;
}
inline bool finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
struct Pose {
    Vec3 position, tilt; // Euler degrees: Rz Ry Rx, local to world
    Vec3 rotate(Vec3 v) const {
        const double cx = cos(tilt.x * deg), sx = sin(tilt.x * deg), cy = cos(tilt.y * deg),
                     sy = sin(tilt.y * deg), cz = cos(tilt.z * deg), sz = sin(tilt.z * deg);
        v = {v.x, cx * v.y - sx * v.z, sx * v.y + cx * v.z};
        v = {cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z};
        return {cz * v.x - sz * v.y, sz * v.x + cz * v.y, v.z};
    }
    Vec3 inverseRotate(Vec3 v) const {
        return {v.dot(rotate({1, 0, 0})), v.dot(rotate({0, 1, 0})), v.dot(rotate({0, 0, 1}))};
    }
    Vec3 local(Vec3 p) const {
        return inverseRotate(p - position);
    }
    Vec3 world(Vec3 p) const {
        return position + rotate(p);
    }
};
inline Vec3 reflect(Vec3 d, Vec3 n) {
    return (d - n * (2 * d.dot(n))).unit();
}
inline bool refract(Vec3 d, Vec3 normal, double n1, double n2, Vec3& transmitted) {
    if (d.dot(normal) > 0)
        normal = -normal;
    double c = -d.dot(normal), eta = n1 / n2, k = 1 - eta * eta * (1 - c * c);
    if (k < 0)
        return false;
    transmitted = (eta * d + (eta * c - std::sqrt(std::max(0.0, k))) * normal).unit();
    return true;
}
inline double fresnel(Vec3 d, Vec3 normal, double n1, double n2) {
    if (std::abs(n1 - n2) < 1e-14)
        return 0;
    double c = std::clamp(std::abs(d.dot(normal)), 0.0, 1.0), eta = n1 / n2,
           s2 = eta * eta * (1 - c * c);
    if (s2 >= 1)
        return 1;
    double t = sqrt(1 - s2), rs = (n1 * c - n2 * t) / (n1 * c + n2 * t),
           rp = (n2 * c - n1 * t) / (n2 * c + n1 * t);
    return (rs * rs + rp * rp) / 2;
}
} // namespace optics
