#pragma once

// ============================================================================
// i3s::GeoAnchor — WGS84 geodetic <-> ECEF <-> local-ENU, all in double.
// ----------------------------------------------------------------------------
// The precision scheme (plan §7): global-mode I3S coordinates live in
// lon/lat/height; converting them straight to floats jitters at city scale.
// Instead every layer picks ONE anchor (its root OBB center); node positions
// are converted geodetic -> ECEF -> East/North/Up at that anchor in double,
// ONCE per node on the loader thread, and only the small ENU-relative values
// are ever cast to float. Nothing outside the SLPK loaders sees geodetic
// coordinates.
//
// ENU frame: x = east, y = north, z = up (right-handed). The swizzle into the
// app's Y-up render world is the scene layer's business, not this class's.
//
// No PROJ: WGS84 covers global-mode packages; local-mode (projected CRS)
// packages are already metric and skip this class entirely.
// ============================================================================

#include <glm/glm.hpp>

namespace i3s {

// lonLatHeight: degrees, degrees, meters (ellipsoidal). Returns ECEF meters.
glm::dvec3 geodeticToEcef(const glm::dvec3& lonLatHeight);

// Inverse (Bowring's method, exact enough at nanometer scale for rendering).
glm::dvec3 ecefToGeodetic(const glm::dvec3& ecef);

// Rows are the East / North / Up unit vectors in ECEF at the given geodetic
// position, i.e.  enu = basis * (ecef - origin).
glm::dmat3 enuBasis(const glm::dvec3& lonLatHeight);

class GeoAnchor {
public:
    GeoAnchor() = default;

    // Anchor at a geodetic position (global-mode layers).
    void setGeodetic(const glm::dvec3& lonLatHeight);

    // Anchor at a plain metric position (local-mode / projected-CRS layers:
    // coordinates are already meters, ENU == CRS axes, no ellipsoid math).
    void setLocalMetric(const glm::dvec3& originCrs);

    bool valid() const { return valid_; }
    bool isGeodetic() const { return geodetic_; }
    const glm::dvec3& originGeodetic() const { return originGeodetic_; }

    // Layer-SR position -> ENU meters relative to the anchor, in double.
    // Geodetic mode expects (lonDeg, latDeg, heightM); local mode expects CRS
    // units and is a plain subtraction.
    glm::dvec3 toEnu(const glm::dvec3& positionSR) const;

    // Rotation taking an ECEF-frame direction into the anchor's ENU frame
    // (identity in local mode). Used for OBB quaternions (M0) and
    // earth-centered normals (M1).
    const glm::dmat3& ecefToEnuRotation() const { return basis_; }

private:
    bool valid_ = false;
    bool geodetic_ = false;
    glm::dvec3 originGeodetic_{ 0.0 }; // lon/lat/h (geodetic mode)
    glm::dvec3 originEcef_{ 0.0 };     // ECEF (geodetic) or CRS units (local)
    glm::dmat3 basis_{ 1.0 };          // ECEF -> ENU rotation (identity local)
};

} // namespace i3s
