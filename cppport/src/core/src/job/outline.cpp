#include "gs/job/outline.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <utility>

namespace gs::job {

// ---- robust-predicates (Shewchuk's orient2d, as the npm package ports it) ----

namespace {

constexpr double kEpsilon = 1.1102230246251565e-16;
constexpr double kSplitter = 134217729;
constexpr double kResultErrBound = (3 + 8 * kEpsilon) * kEpsilon;
constexpr double kCcwErrBoundA = (3 + 16 * kEpsilon) * kEpsilon;
constexpr double kCcwErrBoundB = (2 + 12 * kEpsilon) * kEpsilon;
constexpr double kCcwErrBoundC = (9 + 64 * kEpsilon) * kEpsilon * kEpsilon;

// fast_expansion_sum_zeroelim
int expansionSum(int elen, const double* e, int flen, const double* f, double* h) {
    double Q, Qnew, hh, bvirt;
    double enow = e[0];
    double fnow = f[0];
    int eindex = 0;
    int findex = 0;
    // (JavaScript reads past the end as undefined; the comparisons below
    // only look at an element while its index is in range.)
    const auto at = [](const double* v, int i, int len) {
        return i < len ? v[i] : std::numeric_limits<double>::quiet_NaN();
    };
    if ((fnow > enow) == (fnow > -enow)) {
        Q = enow;
        enow = at(e, ++eindex, elen);
    } else {
        Q = fnow;
        fnow = at(f, ++findex, flen);
    }
    int hindex = 0;
    if (eindex < elen && findex < flen) {
        if ((fnow > enow) == (fnow > -enow)) {
            Qnew = enow + Q;
            hh = Q - (Qnew - enow);
            enow = at(e, ++eindex, elen);
        } else {
            Qnew = fnow + Q;
            hh = Q - (Qnew - fnow);
            fnow = at(f, ++findex, flen);
        }
        Q = Qnew;
        if (hh != 0) {
            h[hindex++] = hh;
        }
        while (eindex < elen && findex < flen) {
            if ((fnow > enow) == (fnow > -enow)) {
                Qnew = Q + enow;
                bvirt = Qnew - Q;
                hh = Q - (Qnew - bvirt) + (enow - bvirt);
                enow = at(e, ++eindex, elen);
            } else {
                Qnew = Q + fnow;
                bvirt = Qnew - Q;
                hh = Q - (Qnew - bvirt) + (fnow - bvirt);
                fnow = at(f, ++findex, flen);
            }
            Q = Qnew;
            if (hh != 0) {
                h[hindex++] = hh;
            }
        }
    }
    while (eindex < elen) {
        Qnew = Q + enow;
        bvirt = Qnew - Q;
        hh = Q - (Qnew - bvirt) + (enow - bvirt);
        enow = at(e, ++eindex, elen);
        Q = Qnew;
        if (hh != 0) {
            h[hindex++] = hh;
        }
    }
    while (findex < flen) {
        Qnew = Q + fnow;
        bvirt = Qnew - Q;
        hh = Q - (Qnew - bvirt) + (fnow - bvirt);
        fnow = at(f, ++findex, flen);
        Q = Qnew;
        if (hh != 0) {
            h[hindex++] = hh;
        }
    }
    if (Q != 0 || hindex == 0) {
        h[hindex++] = Q;
    }
    return hindex;
}

double estimate(int elen, const double* e) {
    double Q = e[0];
    for (int i = 1; i < elen; ++i) {
        Q += e[i];
    }
    return Q;
}

// a*b as the exact two-term expansion (t1 rounded product, t0 error).
void twoProduct(double a, double b, double& t1, double& t0) {
    t1 = a * b;
    double c = kSplitter * a;
    const double ahi = c - (c - a);
    const double alo = a - ahi;
    c = kSplitter * b;
    const double bhi = c - (c - b);
    const double blo = b - bhi;
    t0 = alo * blo - (t1 - ahi * bhi - alo * bhi - ahi * blo);
}

// (s1 + s0) - (t1 + t0) as a four-term expansion into out.
void twoTwoDiff(double s1, double s0, double t1, double t0, double* out) {
    double _i = s0 - t0;
    double bvirt = s0 - _i;
    out[0] = s0 - (_i + bvirt) + (bvirt - t0);
    const double _j = s1 + _i;
    bvirt = _j - s1;
    const double _0 = s1 - (_j - bvirt) + (_i - bvirt);
    _i = _0 - t1;
    bvirt = _0 - _i;
    out[1] = _0 - (_i + bvirt) + (bvirt - t1);
    const double u3 = _j + _i;
    bvirt = u3 - _j;
    out[2] = _j - (u3 - bvirt) + (_i - bvirt);
    out[3] = u3;
}

double orient2dAdapt(double ax, double ay, double bx, double by, double cx, double cy, double detsum) {
    double B[4], C1[8], C2[12], D[16], u[4];
    double s1, s0, t1, t0;

    const double acx = ax - cx;
    const double bcx = bx - cx;
    const double acy = ay - cy;
    const double bcy = by - cy;

    twoProduct(acx, bcy, s1, s0);
    twoProduct(acy, bcx, t1, t0);
    twoTwoDiff(s1, s0, t1, t0, B);

    double det = estimate(4, B);
    double errbound = kCcwErrBoundB * detsum;
    if (det >= errbound || -det >= errbound) {
        return det;
    }

    double bvirt = ax - acx;
    const double acxtail = ax - (acx + bvirt) + (bvirt - cx);
    bvirt = bx - bcx;
    const double bcxtail = bx - (bcx + bvirt) + (bvirt - cx);
    bvirt = ay - acy;
    const double acytail = ay - (acy + bvirt) + (bvirt - cy);
    bvirt = by - bcy;
    const double bcytail = by - (bcy + bvirt) + (bvirt - cy);

    if (acxtail == 0 && acytail == 0 && bcxtail == 0 && bcytail == 0) {
        return det;
    }

    errbound = kCcwErrBoundC * detsum + kResultErrBound * std::fabs(det);
    det += (acx * bcytail + bcy * acxtail) - (acy * bcxtail + bcx * acytail);
    if (det >= errbound || -det >= errbound) {
        return det;
    }

    twoProduct(acxtail, bcy, s1, s0);
    twoProduct(acytail, bcx, t1, t0);
    twoTwoDiff(s1, s0, t1, t0, u);
    const int c1len = expansionSum(4, B, 4, u, C1);

    twoProduct(acx, bcytail, s1, s0);
    twoProduct(acy, bcxtail, t1, t0);
    twoTwoDiff(s1, s0, t1, t0, u);
    const int c2len = expansionSum(c1len, C1, 4, u, C2);

    twoProduct(acxtail, bcytail, s1, s0);
    twoProduct(acytail, bcxtail, t1, t0);
    twoTwoDiff(s1, s0, t1, t0, u);
    const int dlen = expansionSum(c2len, C2, 4, u, D);

    return D[dlen - 1];
}

}  // namespace

double orient2d(double ax, double ay, double bx, double by, double cx, double cy) {
    const double detleft = (ay - cy) * (bx - cx);
    const double detright = (ax - cx) * (by - cy);
    const double det = detleft - detright;
    const double detsum = std::fabs(detleft + detright);
    if (std::fabs(det) >= kCcwErrBoundA * detsum) {
        return det;
    }
    return -orient2dAdapt(ax, ay, bx, by, cx, cy, detsum);
}

namespace {

using Point = std::pair<double, double>;

// point-in-polygon (ray casting, nested form).
bool pointInPolygon(const Point& p, const std::vector<Point>& vs) {
    const double x = p.first;
    const double y = p.second;
    bool inside = false;
    const std::size_t len = vs.size();
    for (std::size_t i = 0, j = len - 1; i < len; j = i++) {
        const double xi = vs[i].first, yi = vs[i].second;
        const double xj = vs[j].first, yj = vs[j].second;
        const bool intersect = ((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi);
        if (intersect) {
            inside = !inside;
        }
    }
    return inside;
}

double cross(const Point& p1, const Point& p2, const Point& p3) {
    return orient2d(p1.first, p1.second, p2.first, p2.second, p3.first, p3.second);
}

// concaveman's convexHull(): monotone chain, dropping collinear points.
std::vector<Point> convexHull(std::vector<Point> points) {
    std::stable_sort(points.begin(), points.end(), [](const Point& a, const Point& b) {
        return a.first == b.first ? a.second < b.second : a.first < b.first;
    });
    std::vector<Point> lower;
    for (const Point& p : points) {
        while (lower.size() >= 2 && cross(lower[lower.size() - 2], lower.back(), p) <= 0) {
            lower.pop_back();
        }
        lower.push_back(p);
    }
    std::vector<Point> upper;
    for (auto it = points.rbegin(); it != points.rend(); ++it) {
        while (upper.size() >= 2 && cross(upper[upper.size() - 2], upper.back(), *it) <= 0) {
            upper.pop_back();
        }
        upper.push_back(*it);
    }
    if (!upper.empty()) {
        upper.pop_back();
    }
    if (!lower.empty()) {
        lower.pop_back();
    }
    lower.insert(lower.end(), upper.begin(), upper.end());
    return lower;
}

// concaveman's fastConvexHull(): the hull of the points outside the
// quadrilateral of the four extreme points.
std::vector<Point> fastConvexHull(const std::vector<Point>& points) {
    Point left = points[0], top = points[0], right = points[0], bottom = points[0];
    for (const Point& p : points) {
        if (p.first < left.first) left = p;
        if (p.first > right.first) right = p;
        if (p.second < top.second) top = p;
        if (p.second > bottom.second) bottom = p;
    }
    const std::vector<Point> cull{left, top, right, bottom};
    std::vector<Point> filtered = cull;
    for (const Point& p : points) {
        if (!pointInPolygon(p, cull)) {
            filtered.push_back(p);
        }
    }
    return convexHull(std::move(filtered));
}

// concaveman(points, Infinity): with no concavity allowed nothing is ever
// dug in, so the result is the convex hull as a closed ring starting from
// its last point. Throws where concaveman does (an empty hull).
std::optional<std::vector<Point>> convexRing(const std::vector<Point>& points) {
    if (points.empty()) {
        return std::nullopt;  // fastConvexHull reads points[0] of nothing
    }
    const std::vector<Point> hull = fastConvexHull(points);
    if (hull.empty()) {
        return std::nullopt;
    }
    std::vector<Point> ring{hull.back()};
    ring.insert(ring.end(), hull.begin(), hull.end() - 1);
    ring.push_back(hull.back());
    return ring;
}

std::string num(double value) {
    return js::numberToString(value);
}

double fixed3(double value) {
    return js::stringToNumber(js::toFixed(value, 3));
}

using OutlinePoint = std::pair<std::string, std::string>;

class Builder {
public:
    explicit Builder(const OutlineInput& input) : in_(input) {
        const double speed = input.outlineSpeed;
        const bool custom = std::isfinite(speed) && speed > 0;
        motion_ = input.isLaser || custom ? "G1" : "G0";
        if (motion_ == "G1") {
            feed_ = custom ? speed : 3000;
        }
    }

    std::vector<std::string> build(const std::vector<OutlinePoint>& points, bool closeLoop) const {
        std::vector<std::string> code{"%X0=posx,Y0=posy,Z0=posz", "%MM=modal.distance",
                                      "G21 G91 G0 Z" + num(in_.zTravel), "G21 G90"};
        code.push_back(feed_ ? motion_ + " F" + num(*feed_) : motion_);
        if (in_.isLaser) {
            code.emplace_back("M3 S1");
        }
        for (const auto& [x, y] : points) {
            code.push_back("X" + x + " Y" + y);
        }
        if (closeLoop && !points.empty()) {
            code.push_back("X" + points.front().first + " Y" + points.front().second);
        }
        if (in_.isLaser) {
            code.emplace_back("M5 S0");
        }
        code.emplace_back("X[X0] Y[Y0]");
        code.push_back("G21 G91 G0 Z-" + num(in_.zTravel));
        code.emplace_back("[MM]");
        return code;
    }

    std::vector<std::string> simpleOutline() const {
        if (in_.vertices.empty()) {
            const auto b = [](double v) { return "[" + num(v) + "]"; };
            const gcode::BoundingBox& box = in_.bbox;
            return build({{b(box.min.x), b(box.min.y)},
                          {b(box.min.x), b(box.max.y)},
                          {b(box.max.x), b(box.max.y)},
                          {b(box.max.x), b(box.min.y)},
                          {b(box.min.x), b(box.min.y)}},
                         false);
        }
        return build({{"[xmin]", "[ymin]"},
                      {"[xmin]", "[ymax]"},
                      {"[xmax]", "[ymax]"},
                      {"[xmax]", "[ymin]"},
                      {"[xmin]", "[ymin]"}},
                     false);
    }

private:
    const OutlineInput& in_;
    std::string motion_;
    std::optional<double> feed_;
};

std::optional<std::vector<std::string>> detailedOutline(const OutlineInput& in, const Builder& builder) {
    // 1. 2D points rounded to 0.001 mm; 2. one per 0.5 mm cell, first kept.
    std::vector<Point> deduped;
    std::set<std::pair<double, double>> seen;
    for (std::size_t i = 0; i + 1 < in.vertices.size(); i += 3) {
        const double x = fixed3(static_cast<double>(in.vertices[i]));
        const double y = fixed3(static_cast<double>(in.vertices[i + 1]));
        const std::pair<double, double> key{js::mathRound(x * 2) + 0.0, js::mathRound(y * 2) + 0.0};
        if (seen.insert(key).second) {
            deduped.emplace_back(x, y);
        }
    }
    // 3. The hull, without its closing point.
    const auto ring = convexRing(deduped);
    if (!ring) {
        return std::nullopt;
    }
    std::vector<Point> hull(ring->begin(), ring->end() - 1);
    // 4. Winding: reversed when sum((x2-x1)(y2+y1)) > 0.
    double area = 0;
    for (std::size_t i = 0; i < hull.size(); ++i) {
        const Point& pt = hull[i];
        const Point& next = hull[(i + 1) % hull.size()];
        area = area + (next.first - pt.first) * (next.second + pt.second);
    }
    if (area > 0) {
        std::reverse(hull.begin(), hull.end());
    }
    // 5. Start from the vertex nearest the origin.
    std::size_t start = 0;
    double minDist = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < hull.size(); ++i) {
        const double d = hull[i].first * hull[i].first + hull[i].second * hull[i].second;
        if (d < minDist) {
            minDist = d;
            start = i;
        }
    }
    std::rotate(hull.begin(), hull.begin() + static_cast<std::ptrdiff_t>(start), hull.end());
    std::vector<OutlinePoint> points;
    for (const Point& p : hull) {
        points.emplace_back(num(p.first), num(p.second));
    }
    return builder.build(points, true);
}

// The box of every cutting move, arcs by their axis extremes.
class CuttingBounds final : public gcode::GeometrySink {
public:
    double xmin = std::numeric_limits<double>::infinity();
    double xmax = -std::numeric_limits<double>::infinity();
    double ymin = std::numeric_limits<double>::infinity();
    double ymax = -std::numeric_limits<double>::infinity();

    void addLine(const gcode::Modal& modal, const gcode::Vec4& from, const gcode::Vec4& to) override {
        if (modal.motion != "G0") {
            update(from.x, from.y);
            update(to.x, to.y);
        }
    }

    void addArc(const gcode::Modal& modal, const gcode::Vec4& from, const gcode::Vec4& to,
                const gcode::Vec4& center) override {
        update(from.x, from.y);
        update(to.x, to.y);
        const double r = std::sqrt(std::pow(from.x - center.x, 2) + std::pow(from.y - center.y, 2));
        if (r == 0) {
            return;
        }
        constexpr double kTau = 2 * std::numbers::pi;
        const auto normalize = [](double a) { return std::fmod(std::fmod(a, kTau) + kTau, kTau); };
        const double sa = normalize(std::atan2(from.y - center.y, from.x - center.x));
        const double ea = normalize(std::atan2(to.y - center.y, to.x - center.x));
        const bool ccw = modal.motion == "G3";
        const auto inSweep = [&](double theta) {
            const double t = normalize(theta);
            if (ccw) {
                return sa <= ea ? t >= sa && t <= ea : t >= sa || t <= ea;
            }
            return sa >= ea ? t <= sa && t >= ea : t <= sa || t >= ea;
        };
        for (const double theta : {0.0, std::numbers::pi / 2, std::numbers::pi, (3 * std::numbers::pi) / 2}) {
            if (inSweep(theta)) {
                const double ex = center.x + r * std::cos(theta);
                const double ey = center.y + r * std::sin(theta);
                update(ex, ey);
            }
        }
    }

private:
    void update(double x, double y) {
        if (x < xmin) xmin = x;
        if (x > xmax) xmax = x;
        if (y < ymin) ymin = y;
        if (y > ymax) ymax = y;
    }
};

std::vector<std::string> rapidlessOutline(const OutlineInput& in, const Builder& builder) {
    CuttingBounds bounds;
    gcode::Interpreter interpreter;
    interpreter.setSink(&bounds);
    for (const std::string_view line : str::splitLines(in.content)) {
        interpreter.processLine(line);
    }
    if (!std::isfinite(bounds.xmin)) {
        return builder.simpleOutline();  // no cutting moves
    }
    const auto f = [](double v) { return js::toFixed(v, 3); };
    return builder.build({{f(bounds.xmin), f(bounds.ymin)},
                          {f(bounds.xmin), f(bounds.ymax)},
                          {f(bounds.xmax), f(bounds.ymax)},
                          {f(bounds.xmax), f(bounds.ymin)},
                          {f(bounds.xmin), f(bounds.ymin)}},
                         false);
}

}  // namespace

std::string_view outlineModeName(OutlineMode mode) {
    switch (mode) {
        case OutlineMode::Detailed: return "Detailed";
        case OutlineMode::Square: return "Square";
        case OutlineMode::RapidlessSquare: return "Rapidless Square";
    }
    return "Detailed";
}

std::optional<OutlineMode> outlineModeFromName(std::string_view name) {
    for (const OutlineMode mode : {OutlineMode::Detailed, OutlineMode::Square, OutlineMode::RapidlessSquare}) {
        if (outlineModeName(mode) == name) {
            return mode;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> outlineProgram(const OutlineInput& input) {
    const Builder builder(input);
    switch (input.mode) {
        case OutlineMode::Square: return builder.simpleOutline();
        case OutlineMode::RapidlessSquare: return rapidlessOutline(input, builder);
        case OutlineMode::Detailed: break;
    }
    return detailedOutline(input, builder);
}

}  // namespace gs::job
