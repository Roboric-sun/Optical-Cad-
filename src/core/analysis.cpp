#include "optics/model.hpp"
#include <complex>
#include <limits>
#include <numeric>

namespace optics {
RayFan rayFan(const SequentialSystem& sys, const Catalog& cat, Field field, int samples) {
    if (samples < 3 || samples > 401 || !std::isfinite(field.x) || !std::isfinite(field.y) ||
        std::abs(field.x) >= 89 || std::abs(field.y) >= 89)
        throw std::invalid_argument("Invalid ray-fan field or sample count");
    auto errors = sys.validate(cat);
    if (!errors.empty())
        throw std::invalid_argument(errors.front());
    RayFan out;
    Vec3 radial{tan(field.x * deg), tan(field.y * deg), 0};
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
            auto sample = waveSample(s, c, f, px, py, cr, chief, r);
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
OptimizationResult optimizeRadii(SequentialSystem& s, const Catalog& c, size_t iterations) {
    const double targetEfl = paraxial(s, c, s.wavelengths.at(s.primary).um).efl;
    auto score = [&](SequentialSystem& candidate) {
        if (!candidate.validate(c).empty())
            return 1e9;
        try {
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
            if (s.surfaces[i].radius == 0 || s.surfaces[i].kind != SurfaceKind::Refract)
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
    return out;
}
AnalysisCurve longitudinalAberration(const SequentialSystem& s, const Catalog& c, int samples) {
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
    AnalysisCurve out;
    out.y.resize(2);
    double base = s.vertices().back(), w = s.wavelengths[s.primary].um;
    for (auto f : s.fields) {
        out.x.push_back(std::hypot(f.x, f.y));
        double norm = std::hypot(f.x, f.y), tx = norm > 1e-9 ? f.x / norm : 0,
               ty = norm > 1e-9 ? f.y / norm : 1;
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
    AnalysisCurve out;
    out.y.resize(1);
    double reference = paraxial(s, c, s.wavelengths[s.primary].um).bfl;
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
AnalysisCurve geometricMTF(const Spot& sp, double maximumFrequency, int samples) {
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
