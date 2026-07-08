#include "Loaders/Slpk/GeoAnchor.h"

#include <cmath>

namespace i3s {

namespace {

// WGS84 ellipsoid.
constexpr double kSemiMajor = 6378137.0;               // a
constexpr double kFlattening = 1.0 / 298.257223563;    // f
constexpr double kSemiMinor = kSemiMajor * (1.0 - kFlattening); // b
constexpr double kE2 = kFlattening * (2.0 - kFlattening);       // first eccentricity^2
constexpr double kEp2 = kE2 / (1.0 - kE2);             // second eccentricity^2
constexpr double kDegToRad = 0.017453292519943295;
constexpr double kRadToDeg = 57.29577951308232;

} // namespace

glm::dvec3 geodeticToEcef(const glm::dvec3& lonLatHeight) {
    const double lon = lonLatHeight.x * kDegToRad;
    const double lat = lonLatHeight.y * kDegToRad;
    const double h = lonLatHeight.z;
    const double sinLat = std::sin(lat);
    const double cosLat = std::cos(lat);
    const double sinLon = std::sin(lon);
    const double cosLon = std::cos(lon);
    // Prime-vertical radius of curvature.
    const double n = kSemiMajor / std::sqrt(1.0 - kE2 * sinLat * sinLat);
    return glm::dvec3((n + h) * cosLat * cosLon,
                      (n + h) * cosLat * sinLon,
                      (n * (1.0 - kE2) + h) * sinLat);
}

glm::dvec3 ecefToGeodetic(const glm::dvec3& ecef) {
    // Bowring's closed-form approximation — sub-millimeter for terrestrial
    // points, which is far beyond rendering needs.
    const double x = ecef.x, y = ecef.y, z = ecef.z;
    const double p = std::sqrt(x * x + y * y);
    if (p < 1e-9) { // on the polar axis
        const double lat = z >= 0.0 ? 90.0 : -90.0;
        return glm::dvec3(0.0, lat, std::abs(z) - kSemiMinor);
    }
    const double theta = std::atan2(z * kSemiMajor, p * kSemiMinor);
    const double sinT = std::sin(theta);
    const double cosT = std::cos(theta);
    const double lat = std::atan2(z + kEp2 * kSemiMinor * sinT * sinT * sinT,
                                  p - kE2 * kSemiMajor * cosT * cosT * cosT);
    const double lon = std::atan2(y, x);
    const double sinLat = std::sin(lat);
    const double n = kSemiMajor / std::sqrt(1.0 - kE2 * sinLat * sinLat);
    const double h = p / std::cos(lat) - n;
    return glm::dvec3(lon * kRadToDeg, lat * kRadToDeg, h);
}

glm::dmat3 enuBasis(const glm::dvec3& lonLatHeight) {
    const double lon = lonLatHeight.x * kDegToRad;
    const double lat = lonLatHeight.y * kDegToRad;
    const double sinLat = std::sin(lat);
    const double cosLat = std::cos(lat);
    const double sinLon = std::sin(lon);
    const double cosLon = std::cos(lon);
    // Rows East / North / Up. NOTE glm::dmat3 is column-major: mat[col][row].
    glm::dmat3 m(1.0);
    // East row
    m[0][0] = -sinLon;          m[1][0] = cosLon;           m[2][0] = 0.0;
    // North row
    m[0][1] = -sinLat * cosLon; m[1][1] = -sinLat * sinLon; m[2][1] = cosLat;
    // Up row
    m[0][2] = cosLat * cosLon;  m[1][2] = cosLat * sinLon;  m[2][2] = sinLat;
    return m;
}

void GeoAnchor::setGeodetic(const glm::dvec3& lonLatHeight) {
    originGeodetic_ = lonLatHeight;
    originEcef_ = geodeticToEcef(lonLatHeight);
    basis_ = enuBasis(lonLatHeight);
    geodetic_ = true;
    valid_ = true;
}

void GeoAnchor::setLocalMetric(const glm::dvec3& originCrs) {
    originGeodetic_ = glm::dvec3(0.0);
    originEcef_ = originCrs;
    basis_ = glm::dmat3(1.0);
    geodetic_ = false;
    valid_ = true;
}

glm::dvec3 GeoAnchor::toEnu(const glm::dvec3& positionSR) const {
    if (!geodetic_)
        return positionSR - originEcef_;
    return basis_ * (geodeticToEcef(positionSR) - originEcef_);
}

} // namespace i3s
