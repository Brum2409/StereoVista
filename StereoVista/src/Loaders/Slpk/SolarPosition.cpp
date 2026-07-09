#include "Loaders/Slpk/SolarPosition.h"

#include <cmath>

namespace i3s {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

// 1-based day of year for a Gregorian date (month clamped to [1,12]).
int dayOfYear(int year, int month, int day) {
    static const int kCumulative[12] = { 0,   31,  59,  90,  120, 151,
                                         181, 212, 243, 273, 304, 334 };
    if (month < 1)
        month = 1;
    if (month > 12)
        month = 12;
    int doy = kCumulative[month - 1] + day;
    if (month > 2 && isLeapYear(year))
        ++doy;
    return doy;
}

} // namespace

glm::dvec3 solarDirectionEnu(double lonDeg, double latDeg, int year, int month,
                             int day, double utcHours) {
    // NOAA solar calculator formulas (General Solar Position Calculations,
    // NOAA Global Monitoring Division).
    const int doy = dayOfYear(year, month, day);
    const double daysInYear = isLeapYear(year) ? 366.0 : 365.0;

    // Fractional year in radians, evaluated at the given hour.
    const double gamma =
        2.0 * kPi / daysInYear * (doy - 1 + (utcHours - 12.0) / 24.0);

    // Equation of time (minutes) and solar declination (radians).
    const double eqTime =
        229.18 * (0.000075 + 0.001868 * std::cos(gamma) -
                  0.032077 * std::sin(gamma) - 0.014615 * std::cos(2.0 * gamma) -
                  0.040849 * std::sin(2.0 * gamma));
    const double decl = 0.006918 - 0.399912 * std::cos(gamma) +
                        0.070257 * std::sin(gamma) -
                        0.006758 * std::cos(2.0 * gamma) +
                        0.000907 * std::sin(2.0 * gamma) -
                        0.002697 * std::cos(3.0 * gamma) +
                        0.00148 * std::sin(3.0 * gamma);

    // True solar time (minutes) at the site; hour angle 0 = solar noon,
    // positive in the afternoon.
    const double timeOffset = eqTime + 4.0 * lonDeg; // timezone folded into UTC
    double trueSolarTime = utcHours * 60.0 + timeOffset;
    trueSolarTime = std::fmod(trueSolarTime, 1440.0);
    if (trueSolarTime < 0.0)
        trueSolarTime += 1440.0;
    const double hourAngle = (trueSolarTime / 4.0 - 180.0) * kDegToRad;

    const double lat = latDeg * kDegToRad;
    const double sinAlt = std::sin(lat) * std::sin(decl) +
                          std::cos(lat) * std::cos(decl) * std::cos(hourAngle);
    const double alt = std::asin(glm::clamp(sinAlt, -1.0, 1.0));

    // Azimuth from north, clockwise (east = 90 deg). atan2 form is stable at
    // the poles and at noon, unlike the acos variant.
    const double azimuth = std::atan2(
        std::sin(hourAngle) * std::cos(decl),
        std::cos(hourAngle) * std::cos(decl) * std::sin(lat) -
            std::sin(decl) * std::cos(lat));
    // atan2 above measures from SOUTH (astronomer's convention); rotate to
    // the from-north compass convention.
    const double azFromNorth = azimuth + kPi;

    const double cosAlt = std::cos(alt);
    return glm::dvec3(cosAlt * std::sin(azFromNorth),  // east
                      cosAlt * std::cos(azFromNorth),  // north
                      std::sin(alt));                  // up
}

double solarElevationDeg(double lonDeg, double latDeg, int year, int month,
                         int day, double utcHours) {
    const glm::dvec3 dir =
        solarDirectionEnu(lonDeg, latDeg, year, month, day, utcHours);
    return std::asin(glm::clamp(dir.z, -1.0, 1.0)) * kRadToDeg;
}

} // namespace i3s
