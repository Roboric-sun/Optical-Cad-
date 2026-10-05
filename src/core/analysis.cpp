#include "optics/model.hpp"
#include <complex>
#include <limits>
#include <numeric>

namespace optics {
RayFan rayFan(const SequentialSystem& sys, const Catalog& cat, Field field, int samples) {
    if (!sys.solves.empty()) return rayFan(resolvedSystem(sys, cat), cat, field, samples);
    if (samples < 3 || samples > 401 || !validField(sys, field))
        throw std::invalid_argument("Invalid ray-fan field or sample count");
    auto errors = sys.validate(cat);
    if (!errors.empty())
        throw std::invalid_argument(errors.front());
    RayFan out;
    const auto angles = angularField(sys, cat, field);
    Vec3 radial{tan(angles.x * deg), tan(angles.y * deg), 0};
    out.tangentialAxis = radial.norm() > 1e-12 ? radial.unit() : Vec3{0, 1, 0};
    out.sagittalAxis = {out.tangentialAxis.y, -out.tangentialAxis.x, 0};
    auto chief = trace(sys, cat, pupilRay(sys, cat, field, sys.wavelengths[sys.primary].um, 0, 0));
    if (chief.status != TraceStatus::Complete)
        throw std::runtime_error("Primary chief ray does not reach image");
    out.referenceImage = chief.image;
    for (int i = 0; i < samples; ++i) {
        double pupil = 2. * i / (samples - 1) - 1;
        out.tangential.x.push_back(pupil);
        out.sagittal.x.push_back(pupil);
    }
    for (auto wave : sys.wavelengths) {
        out.wavelengths.push_back(wave.um);
        for (int plane = 0; plane < 2; ++plane) {
            auto axis = plane ? out.sagittalAxis : out.tangentialAxis;
            auto& curve = plane ? out.sagittal : out.tangential;
            std::vector<double> values;
            for (double pupil : curve.x) {
                auto result = trace(
                    sys, cat, pupilRay(sys, cat, field, wave.um, pupil * axis.x, pupil * axis.y));
                values.push_back(result.status == TraceStatus::Complete
                                     ? (result.image - chief.image).dot(axis) * 1000
                                     : std::numeric_limits<double>::quiet_NaN());
            }
            curve.y.push_back(std::move(values));
        }
    }
    return out;
}
Spot spot(const SequentialSystem& sys, const Catalog& cat, Field field, int grid) {
    if (!sys.solves.empty()) return spot(resolvedSystem(sys, cat), cat, field, grid);
    if (grid == 0)
        grid = sys.pupilGrid;
    if (grid < 3 || grid > 65)
        throw std::invalid_argument("Pupil grid out of range");
    Spot out;
    double total = 0;
    for (auto wave : sys.wavelengths)
        for (int iy = 0; iy < grid; ++iy)
            for (int ix = 0; ix < grid; ++ix) {
                double px = 2.0 * ix / (grid - 1) - 1, py = 2.0 * iy / (grid - 1) - 1;
                if (px * px + py * py > 1 + 1e-12)
                    continue;
                ++out.launched;
                auto t = trace(sys, cat, pupilRay(sys, cat, field, wave.um, px, py));
                if (t.status != TraceStatus::Complete)
                    continue;
                double p = t.power * wave.weight;
                out.samples.push_back({px, py, wave.um, t.image, p});
                out.centroid = out.centroid + t.image * p;
                total += p;
            }
    if (total <= 0)
        return out;
    out.centroid = out.centroid / total;
    for (auto& s : out.samples) {
        double r = (s.image - out.centroid).norm();
        out.rms += s.power * r * r;
        out.maxRadius = std::max(out.maxRadius, r);
    }
    out.rms = sqrt(out.rms / total);
    return out;
}
static std::optional<WaveSample> waveSample(const SequentialSystem& s, const Catalog& c, Field f,
                                            double px, double py, const Ray& chiefRay,
                                            const RayTrace& chief, double referenceRadius) {
    double w = s.wavelengths[s.primary].um;
    Ray ray = pupilRay(s, c, f, w, px, py);
    auto t = trace(s, c, ray);
    if (t.status != TraceStatus::Complete)
        return {};
    Vec3 oc = t.exitPoint - chief.image;
    double B = oc.dot(t.exitDirection), C = oc.norm2() - referenceRadius * referenceRadius,
           disc = B * B - C;
    if (disc < 0)
        return {};
    double r1 = -B - sqrt(disc), r2 = -B + sqrt(disc), dist = std::abs(r1) < std::abs(r2) ? r1 : r2;
    // OPL is accumulated only to the last physical surface. Reference sphere
    // through chief exit removes ideal convergence, preserving aberration.
    double phase =
        s.objectDistance == 0 ? (ray.origin - chiefRay.origin).dot(chiefRay.direction) : 0;
    double opd = t.opl + t.index * dist + phase - chief.opl;
    return WaveSample{px, py, opd, t.power};
}
Wavefront wavefront(const SequentialSystem& s, const Catalog& c, Field f, int grid) {
    if (!s.solves.empty()) return wavefront(resolvedSystem(s, c), c, f, grid);
    if (grid < 3 || grid > 129) throw std::invalid_argument("Wavefront grid must be 3–129");
    Wavefront out;
    out.wavelength = s.wavelengths.at(s.primary).um;
    auto cr = pupilRay(s, c, f, out.wavelength, 0, 0);
    auto chief = trace(s, c, cr);
    if (chief.status != TraceStatus::Complete)
        throw std::runtime_error("Chief ray does not reach image");
    double radius = (chief.exitPoint - chief.image).norm();
    if (radius < 1e-6)
        throw std::runtime_error("Reference sphere has zero radius");
    double sum = 0, total = 0;
    for (int iy = 0; iy < grid; ++iy)
        for (int ix = 0; ix < grid; ++ix) {
            double x = 2.0 * ix / (grid - 1) - 1, y = 2.0 * iy / (grid - 1) - 1;
            if (x * x + y * y > 1 + 1e-12)
                continue;
            auto sample = waveSample(s, c, f, x, y, cr, chief, radius);
            if (sample) {
                out.samples.push_back(*sample);
                sum += sample->opd * sample->power;
                total += sample->power;
            }
        }
    if (total <= 0)
        throw std::runtime_error("Empty pupil");
    double mean = sum / total, lo = 1e100, hi = -1e100;
    for (auto& sample : out.samples) {
        sample.opd -= mean;
        lo = std::min(lo, sample.opd);
        hi = std::max(hi, sample.opd);
        out.rms += sample.power * sample.opd * sample.opd;
    }
    out.rms = sqrt(out.rms / total);
    out.pv = hi - lo;
    return out;
}
using Complex = std::complex<double>;
static void fft(std::vector<Complex>& a) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        Complex wlen = std::polar(1.0, -2 * pi / len);
        for (size_t i = 0; i < n; i += len) {
            Complex w = 1;
            for (size_t j = 0; j < len / 2; ++j) {
                Complex u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
static void fft2(std::vector<Complex>& a, int n) {
    std::vector<Complex> row(n);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x)
            row[x] = a[y * n + x];
        fft(row);
        for (int x = 0; x < n; ++x)
            a[y * n + x] = row[x];
    }
    for (int x = 0; x < n; ++x) {
        for (int y = 0; y < n; ++y)
            row[y] = a[y * n + x];
        fft(row);
        for (int y = 0; y < n; ++y)
            a[y * n + x] = row[y];
    }
}
Diffraction diffraction(const SequentialSystem& s, const Catalog& c, Field f, int n) {
    if (!s.solves.empty()) return diffraction(resolvedSystem(s, c), c, f, n);
    if (s.primary >= s.wavelengths.size()) throw std::invalid_argument("Primary wavelength does not exist");
    if (n < 32 || n > 256 || (n & (n - 1)))
        throw std::invalid_argument("FFT size must be power of 2, 32–256");
    Diffraction out;
    out.size = n;
    std::vector<Complex> pupil(n * n);
    double w = s.wavelengths[s.primary].um;
    auto cr = pupilRay(s, c, f, w, 0, 0);
    auto chief = trace(s, c, cr);
    if (chief.status != TraceStatus::Complete)
        throw std::runtime_error("Chief ray blocked");
    double r = (chief.exitPoint - chief.image).norm();
    if (r < 1e-6)
        throw std::runtime_error("Image must be away from last surface");
    // Entrance-pupil grid with 2x zero padding. Scalar Fraunhofer model.
    double step = 4.0 / n;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            double px = (x - n / 2) * step, py = (y - n / 2) * step;
            if (px * px + py * py > 1)
                continue;
            // FFT samples physical pupil positions, never a stretched nominal grid.
            const auto nominal = nominalPupil(f, px, py);
            if (!nominal) continue;
            auto sample = waveSample(s, c, f, nominal->x, nominal->y, cr, chief, r);
            if (sample)
                pupil[y * n + x] =
                    std::polar(sqrt(sample->power), 2 * pi * sample->opd / (w * 1e-3));
        }
    fft2(pupil, n);
    out.psf.resize(n * n);
    double energy = 0;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            double value = std::norm(pupil[y * n + x]);
            out.psf[((y + n / 2) % n) * n + (x + n / 2) % n] = value;
            energy += value;
        }
    if (energy <= 0)
        throw std::runtime_error("Pupil has no energy");
    for (double& v : out.psf)
        v /= energy;
    double focal = std::abs(paraxial(s, c, w).efl);
    out.pixelUm = w * focal / (n * step * s.pupilDiameter / 2);
    std::vector<Complex> image(n * n);
    for (int i = 0; i < n * n; ++i)
        image[i] = out.psf[i];
    fft2(image, n);
    double zero = std::abs(image[0]);
    for (int i = 0; i <= n / 2; ++i) {
        out.frequency.push_back(i / (n * out.pixelUm * 1e-3));
        out.mtfX.push_back(std::abs(image[i]) / zero);
        out.mtfY.push_back(std::abs(image[i * n]) / zero);
    }
    return out;
}
Diffraction polychromaticDiffraction(const SequentialSystem& sys, const Catalog& c, Field field,
                                    int size, int grid) {
    auto s = resolvedSystem(sys, c);
    if (size < 32 || size > 128 || (size & (size - 1)) || grid < size / 2 + 1 || grid > 65)
        throw std::invalid_argument("Exit-pupil diffraction: image 32/64/128, pupil grid 9–65");
    auto errors = s.validate(c);
    if (!errors.empty()) throw std::invalid_argument(errors.front());
    const auto angular = angularField(s, c, field);
    if (std::hypot(angular.x, angular.y) > 10)
        throw std::invalid_argument("Скалярная дифракция: проверяемая область поля до 10°");
    // Convert the object once; changing primary during the spectral loop must not
    // redefine image-height fields or move the physical object between colours.
    s.fieldType = FieldType::Angle;
    Field unvignetted = angular;
    unvignetted.vux = unvignetted.vlx = unvignetted.vuy = unvignetted.vly = 0;
    const auto reference = trace(s, c, pupilRay(s, c, unvignetted, s.wavelengths[s.primary].um, 0, 0));
    if (reference.status != TraceStatus::Complete) throw std::runtime_error("Primary chief ray blocked");
    struct Sample { double x, y, phase, amplitude; };
    struct Channel { double wavelength, radius, index, weight; Vec3 offset; std::vector<Sample> samples; };
    std::vector<Channel> channels;
    double spectralWeight = 0;
    for (const auto& wave : s.wavelengths) spectralWeight += wave.weight;
    double pixel = std::numeric_limits<double>::infinity();
    const double step = 2. / (grid - 1);
    for (size_t wi = 0; wi < s.wavelengths.size(); ++wi) {
        s.primary = wi;
        const double w = s.wavelengths[wi].um;
        // Image the STOP through the downstream reduced-angle matrix.
        double A = 1, B = 0, C = 0, D = 1, n = 1;
        for (size_t i = 0; i < s.surfaces.size(); ++i) {
            const auto& sf = s.surfaces[i];
            if (sf.kind == SurfaceKind::Mirror || sf.tilt.norm2() || sf.decenter.norm2())
                throw std::invalid_argument("Exit-pupil diffraction requires centered refractors");
            const double nn = sf.kind == SurfaceKind::Stop ? n : c.get(sf.material).index(w);
            if (i >= s.stop) {
                const double power = sf.radius == 0 ? 0 : (nn - n) / sf.radius;
                C -= power * A; D -= power * B;
                if (i + 1 < s.surfaces.size()) { A += sf.thickness / nn * C; B += sf.thickness / nn * D; }
            }
            n = nn;
        }
        if (std::abs(D) < 1e-8) throw std::invalid_argument("Exit pupil at infinity is unsupported");
        const double pupilZ = s.vertices().back() - n * B / D;
        const auto chiefRay = pupilRay(s, c, unvignetted, w, 0, 0);
        const auto chief = trace(s, c, chiefRay);
        if (chief.status != TraceStatus::Complete) throw std::runtime_error("Spectral chief ray blocked");
        auto project = [&](const RayTrace& t) {
            return t.exitPoint + t.exitDirection * ((pupilZ - t.exitPoint.z) / t.exitDirection.z);
        };
        const Vec3 centre = project(chief);
        const double radius = (chief.image - centre).norm();
        if (s.imageZ() <= pupilZ || radius < 1e-5) throw std::invalid_argument("Exit pupil must precede the real image");
        Channel channel{w, radius, n, 0, chief.image - reference.image, {}};
        double extent = 0;
        auto mapping = [&](double x, double y) -> std::optional<Vec3> {
            const auto path = trace(s, c, pupilRay(s, c, unvignetted, w, x, y), false, SIZE_MAX, false);
            if (path.status != TraceStatus::Complete || std::abs(path.exitDirection.z) < .9) return {};
            return project(path);
        };
        for (int iy = 0; iy < grid; ++iy) for (int ix = 0; ix < grid; ++ix) {
            const double x = -1 + ix * step, y = -1 + iy * step;
            if (!nominalPupil(angular, x, y)) continue;
            const auto ray = pupilRay(s, c, unvignetted, w, x, y);
            const auto path = trace(s, c, ray);
            if (path.status != TraceStatus::Complete) continue;
            const auto ep = project(path);
            const auto dx = mapping(x + 1e-4, y), dy = mapping(x, y + 1e-4);
            if (!dx || !dy) throw std::runtime_error("Exit-pupil mapping failed");
            const auto u = (*dx - ep) / 1e-4, v = (*dy - ep) / 1e-4;
            const double jacobian = u.x * v.y - u.y * v.x;
            if (!std::isfinite(jacobian) || jacobian <= 1e-10)
                throw std::invalid_argument("Folded or singular exit pupil is unsupported");
            const auto relative = ep - centre;
            if (std::hypot(relative.x, relative.y) / radius > .25)
                throw std::invalid_argument("Exit-pupil scalar model requires pupil radius / image distance ≤ 0.25");
            const auto wave = waveSample(s, c, unvignetted, x, y, chiefRay, chief, radius);
            if (!wave) throw std::runtime_error("Reference sphere intersection failed");
            // Flux conservation: E_exit=sqrt(T/J); dA_exit=J dA_input.
            channel.samples.push_back({relative.x, relative.y, 2 * pi * wave->opd / (w * 1e-3),
                                       std::sqrt(wave->power * jacobian)});
            channel.weight += wave->power;
            extent = std::max(extent, std::hypot(relative.x, relative.y));
        }
        if (channel.samples.empty() || extent <= 0 || channel.weight <= 0)
            throw std::runtime_error("Empty exit pupil");
        channel.weight *= s.wavelengths[wi].weight / spectralWeight;
        pixel = std::min(pixel, w * radius / (4 * n * extent));
        channels.push_back(std::move(channel));
    }
    Diffraction out; out.size = size; out.pixelUm = pixel; out.psf.assign(size * size, 0);
    double totalWeight = 0;
    for (const auto& channel : channels) {
        std::vector<double> image(size * size);
        double energy = 0;
        const double k = 2 * pi * channel.index / (channel.wavelength * 1e-3 * channel.radius);
        // Separable evaluation of the nonuniform Fourier sum avoids O(N^2 P)
        // transcendental calls while retaining a common physical image grid.
        std::vector<Complex> xphase(size * channel.samples.size());
        for (int x = 0; x < size; ++x) {
            const double xx = (x - size / 2) * pixel * 1e-3 - channel.offset.x;
            for (size_t j = 0; j < channel.samples.size(); ++j) {
                const auto& p = channel.samples[j];
                xphase[x * channel.samples.size() + j] = std::polar(p.amplitude, p.phase - k * p.x * xx);
            }
        }
        for (int y = 0; y < size; ++y) {
            const double yy = (y - size / 2) * pixel * 1e-3 - channel.offset.y;
            std::vector<Complex> yp; yp.reserve(channel.samples.size());
            for (const auto& p : channel.samples) yp.push_back(std::polar(1., -k * p.y * yy));
            for (int x = 0; x < size; ++x) {
                Complex sum{};
                for (size_t j = 0; j < yp.size(); ++j) sum += xphase[x * yp.size() + j] * yp[j];
                image[y * size + x] = std::norm(sum); energy += image[y * size + x];
            }
        }
        if (!std::isfinite(energy) || energy <= 0) throw std::runtime_error("Invalid diffraction energy");
        for (size_t i = 0; i < image.size(); ++i) out.psf[i] += image[i] / energy * channel.weight;
        totalWeight += channel.weight;
    }
    std::vector<Complex> otf(size * size);
    for (size_t i = 0; i < out.psf.size(); ++i) { out.psf[i] /= totalWeight; otf[i] = out.psf[i]; }
    fft2(otf, size);
    for (int i = 0; i <= size / 2; ++i) {
        out.frequency.push_back(i / (size * pixel * 1e-3));
        out.mtfX.push_back(std::abs(otf[i]) / std::abs(otf[0]));
        out.mtfY.push_back(std::abs(otf[i * size]) / std::abs(otf[0]));
    }
    return out;
}
OptimizationResult optimizeRadii(SequentialSystem& system, const Catalog& c, size_t iterations,
                                std::function<bool(size_t, size_t, double)> progress) {
    auto s = system;
    if (iterations == 0 || iterations > 1000) throw std::invalid_argument("Invalid iteration count");
    const double targetEfl = paraxial(s, c, s.wavelengths.at(s.primary).um).efl;
    auto score = [&](SequentialSystem& candidate) {
        if (!candidate.validate(c).empty())
            return 1e9;
        try {
            applySolves(candidate, c);
            autofocus(candidate, c);
            double val = 0, weights = 0;
            for (auto f : candidate.fields) {
                auto sp = spot(candidate, c, f, 9);
                if (sp.samples.size() < sp.launched * 0.9)
                    return 1e9;
                val += f.weight * sp.rms * sp.rms;
                weights += f.weight;
            }
            double efl = paraxial(candidate, c, candidate.wavelengths[candidate.primary].um).efl;
            double penalty =
                (efl - targetEfl) / targetEfl * std::max(.5, candidate.pupilDiameter / 4);
            return sqrt(val / weights + penalty * penalty);
        } catch (...) {
            return 1e9;
        }
    };
    OptimizationResult out;
    out.before = score(s);
    out.after = out.before;
    out.evaluations = 1;
    if (out.before >= 1e9)
        throw std::runtime_error("Optimization requires valid non-vignetted system");
    double step = 0.12;
    for (size_t pass = 0; pass < iterations; ++pass) {
        bool changed = false;
        for (size_t i = 0; i < s.surfaces.size(); ++i) {
            if (progress && !progress(pass, iterations, out.after)) { out.cancelled = true; return out; }
            if (s.surfaces[i].radius == 0 || s.surfaces[i].kind != SurfaceKind::Refract ||
                isSolved(s, SolveParameter::Radius, i))
                continue;
            auto original = s;
            auto best = s;
            double local = out.after;
            for (double sign : {-1.0, 1.0}) {
                auto trial = original;
                trial.surfaces[i].radius *= 1 + sign * step;
                double v = score(trial);
                ++out.evaluations;
                if (v < local) {
                    local = v;
                    best = trial;
                }
            }
            if (local < out.after) {
                s = best;
                out.after = local;
                changed = true;
            }
        }
        if (!changed)
            step *= 0.5;
        if (step < 1e-4)
            break;
    }
    if (progress && !progress(iterations, iterations, out.after)) { out.cancelled = true; return out; }
    system = std::move(s);
    return out;
}
AnalysisCurve longitudinalAberration(const SequentialSystem& s, const Catalog& c, int samples) {
    if (!s.solves.empty()) return longitudinalAberration(resolvedSystem(s, c), c, samples);
    if (samples < 2 || samples > 1001) throw std::invalid_argument("Longitudinal sample count must be 2–1001");
    AnalysisCurve out;
    out.y.resize(s.wavelengths.size());
    for (int i = 0; i < samples; ++i) {
        double py = .001 + .999 * i / (samples - 1.);
        out.x.push_back(py);
        for (size_t w = 0; w < s.wavelengths.size(); ++w) {
            auto t = trace(s, c, pupilRay(s, c, {0, 0, 1}, s.wavelengths[w].um, 0, py), false);
            double value = std::numeric_limits<double>::quiet_NaN();
            if (t.status == TraceStatus::Complete && std::abs(t.exitDirection.y) > 1e-12)
                value = t.exitPoint.z - t.exitPoint.y * t.exitDirection.z / t.exitDirection.y -
                        s.imageZ();
            out.y[w].push_back(value);
        }
    }
    return out;
}
AnalysisCurve fieldCurvature(const SequentialSystem& s, const Catalog& c) {
    if (!s.solves.empty()) return fieldCurvature(resolvedSystem(s, c), c);
    AnalysisCurve out;
    out.y.resize(2);
    if (s.surfaces.empty()) throw std::invalid_argument("Field curvature requires surfaces");
    double base = s.vertices().back(), w = s.wavelengths.at(s.primary).um;
    for (auto f : s.fields) {
        out.x.push_back(std::hypot(f.x, f.y));
        const auto angles = angularField(s, c, f);
        double norm = std::hypot(angles.x, angles.y), tx = norm > 1e-9 ? angles.x / norm : 0,
               ty = norm > 1e-9 ? angles.y / norm : 1;
        for (int plane = 0; plane < 2; ++plane) {
            double ax = plane ? -ty : tx, ay = plane ? tx : ty;
            auto a = trace(s, c, pupilRay(s, c, f, w, ax * .01, ay * .01), false),
                 b = trace(s, c, pupilRay(s, c, f, w, -ax * .01, -ay * .01), false);
            double focus = std::numeric_limits<double>::quiet_NaN();
            if (a.status == TraceStatus::Complete && b.status == TraceStatus::Complete &&
                std::abs(a.exitDirection.z) > 1e-10 && std::abs(b.exitDirection.z) > 1e-10) {
                auto ua = a.exitDirection / a.exitDirection.z,
                     ub = b.exitDirection / b.exitDirection.z;
                auto da = (a.exitPoint + ua * (base - a.exitPoint.z)) -
                          (b.exitPoint + ub * (base - b.exitPoint.z));
                auto du = ua - ub;
                double den = du.x * du.x + du.y * du.y;
                if (den > 1e-15)
                    focus = base - (da.x * du.x + da.y * du.y) / den - s.imageZ();
            }
            out.y[plane].push_back(focus);
        }
    }
    return out;
}
AnalysisCurve chromaticFocus(const SequentialSystem& s, const Catalog& c, int samples) {
    if (!s.solves.empty()) return chromaticFocus(resolvedSystem(s, c), c, samples);
    if (samples < 2 || samples > 1001 || s.primary >= s.wavelengths.size())
        throw std::invalid_argument("Invalid chromatic-focus sampling or primary wavelength");
    AnalysisCurve out;
    out.y.resize(1);
    double reference = paraxial(s, c, s.wavelengths.at(s.primary).um).bfl;
    double low = 1e9, high = 0;
    for (auto w : s.wavelengths) {
        low = std::min(low, w.um);
        high = std::max(high, w.um);
    }
    if (high - low < 1e-6) {
        low = std::max(.2, low - .1);
        high = std::min(5., high + .1);
    }
    for (int i = 0; i < samples; ++i) {
        double w = low + (high - low) * i / (samples - 1.);
        out.x.push_back(w * 1000);
        out.y[0].push_back(paraxial(s, c, w).bfl - reference);
    }
    return out;
}
AnalysisCurve encircledEnergy(const Spot& sp) {
    if (!finite(sp.centroid)) throw std::invalid_argument("Invalid spot centroid");
    AnalysisCurve out; out.y.resize(1);
    std::vector<std::pair<double,double>> radii;
    double total = 0;
    for (const auto& sample : sp.samples) {
        if (!finite(sample.image) || !std::isfinite(sample.power) || sample.power < 0)
            throw std::invalid_argument("Invalid spot sample");
        if (sample.power > 0) radii.emplace_back((sample.image - sp.centroid).norm(), sample.power);
        total += sample.power;
    }
    if (!std::isfinite(total) || total <= 0) throw std::invalid_argument("Encircled energy requires a nonempty spot");
    std::sort(radii.begin(), radii.end());
    double cumulative = 0;
    for (const auto& [radius, power] : radii) {
        cumulative += power;
        if (!out.x.empty() && radius == out.x.back()) out.y[0].back() = cumulative / total;
        else { out.x.push_back(radius); out.y[0].push_back(cumulative / total); }
    }
    return out;
}
AnalysisCurve distortion(const SequentialSystem& sys, const Catalog& cat) {
    const auto s = resolvedSystem(sys, cat);
    const double w = s.wavelengths.at(s.primary).um;
    const auto p = paraxial(s, cat, w);
    AnalysisCurve out; out.y.resize(1);
    for (const auto& field : s.fields) {
        const auto a = angularField(s, cat, field);
        const double factor = s.objectDistance > 0 ? -s.objectDistance / (p.matrix[2]*s.objectDistance+p.matrix[3]) : -1/p.matrix[2];
        if (!std::isfinite(factor)) throw std::invalid_argument("Distortion has an infinite Gaussian conjugate");
        const Vec3 ideal{factor*tan(a.x*deg), factor*tan(a.y*deg), 0};
        const auto chief = trace(s, cat, pupilRay(s, cat, field, w, 0, 0));
        if (chief.status != TraceStatus::Complete) throw std::invalid_argument("Distortion chief ray blocked");
        out.x.push_back(std::hypot(field.x, field.y));
        const double radius = ideal.norm();
        out.y[0].push_back(radius < 1e-12 ? 0 : 100 * (Vec3{chief.image.x,chief.image.y,0}.dot(ideal)/(radius*radius) - 1));
    }
    return out;
}
AnalysisCurve geometricMTF(const Spot& sp, double maximumFrequency, int samples) {
    if (samples < 2 || samples > 10001 || !std::isfinite(maximumFrequency) || maximumFrequency <= 0)
        throw std::invalid_argument("Invalid MTF frequency range or sampling");
    AnalysisCurve out;
    out.y.resize(2);
    double total = 0;
    for (auto p : sp.samples)
        total += p.power;
    if (total <= 0)
        return out;
    for (int i = 0; i < samples; ++i) {
        double f = maximumFrequency * i / (samples - 1.);
        out.x.push_back(f);
        for (int axis = 0; axis < 2; ++axis) {
            Complex sum = 0;
            for (auto p : sp.samples)
                sum += std::polar(
                    p.power,
                    2 * pi * f * (axis ? p.image.y - sp.centroid.y : p.image.x - sp.centroid.x));
            out.y[axis].push_back(std::abs(sum) / total);
        }
    }
    return out;
}
} // namespace optics
