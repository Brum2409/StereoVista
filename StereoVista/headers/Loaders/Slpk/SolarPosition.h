#pragma once

// ============================================================================
// i3s::SolarPosition — sun direction from geolocation + UTC time (M4).
// ----------------------------------------------------------------------------
// NOAA's simplified solar position algorithm (the one behind the NOAA solar
// calculator): fractional-year Fourier series for declination + equation of
// time, then the standard hour-angle -> altitude/azimuth transform. Accuracy
// is a few hundredths of a degree over 1900-2100 — far below the sun's own
// 0.5 degree disc, so plenty for daylight/shadow study.
//
// Pure math, no dependencies beyond glm. Everything double.
// ============================================================================

#include <glm/glm.hpp>

namespace i3s {

// Unit vector TOWARD the sun in the local East/North/Up frame at
// (lonDeg, latDeg) for the given UTC calendar time. z < 0 = sun below the
// horizon. Gregorian calendar; utcHours is fractional [0,24).
glm::dvec3 solarDirectionEnu(double lonDeg, double latDeg, int year, int month,
                             int day, double utcHours);

// Solar elevation above the horizon in degrees (convenience for UI readouts;
// same math as solarDirectionEnu).
double solarElevationDeg(double lonDeg, double latDeg, int year, int month,
                         int day, double utcHours);

} // namespace i3s
