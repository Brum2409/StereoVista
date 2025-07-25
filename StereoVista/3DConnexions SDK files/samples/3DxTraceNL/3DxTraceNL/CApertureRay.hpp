#ifndef CApertureRay_HPP_INCLUDED
#define CApertureRay_HPP_INCLUDED
// <copyright file="CApertureRay.hpp" company="3Dconnexion">
// -------------------------------------------------------------------------------------------
// Copyright (c) 2018-2019 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer Kit",
// including all accompanying documentation, and is protected by intellectual property laws.
// All use of the 3Dconnexion Software Developer Kit is subject to the License Agreement found
// in the "LicenseAgreementSDK.txt" file.
// All rights not expressly granted by 3Dconnexion are reserved.
// -------------------------------------------------------------------------------------------
// </copyright>
// <history>
// *******************************************************************************************
// File History
//
// $Id: $
//
// </history>
#include "Point3D.hpp"
#include "Vector3D.hpp"

// stdlib
#include <memory>

namespace TDx {
namespace TraceNL {
namespace Media3D {
/// <summary>
/// Representation of a ray.
/// </summary>
class CApertureRay {
public:
  /// <summary>
  /// Creates an instance of the ray.
  /// </summary>
  CApertureRay() = default;

  /// <summary>
  /// Creates an instance of the ray that specifies the origin and direction and the aperture.
  /// </summary>
  /// <param name="origin">The source of the ray.</param>
  /// <param name="direction">The direction of the ray.</param>
  /// <param name="aperture">The diameter of the ray at the near clipping distance.</param>
  CApertureRay(Point3D origin, Vector3D direction, double aperture)
      : m_origin(origin), m_direction(direction), m_aperture(aperture) {}

#if defined(_MSC_EXTENSIONS)
  /// <summary>
  /// Gets or sets the origin of the ray.
  /// </summary>
  __declspec(property(get = GetOrigin, put = PutOrigin)) Point3D Origin;

  /// <summary>
  /// Gets or sets the direction of the ray.
  /// </summary>
  __declspec(property(get = GetDirection, put = PutDirection)) Vector3D Direction;

  /// <summary>
  /// Gets or sets the ray diameter at the near clipping distance.
  /// </summary>
  __declspec(property(get = GetAperture, put = PutAperture)) double Aperture;
  #endif

  /// <summary>
  /// Gets the origin of the ray.
  /// </summary>
  /// <returns>A <see cref="Point3D"/>.</returns>
  Point3D GetOrigin() const { return m_origin; }
  /// <summary>
  /// Sets the origin of the ray.
  /// </summary>
  /// <param name="value">The <see cref="Point3D"/> location of the of the origin.</param>
  void PutOrigin(Point3D value) { m_origin = value; }

  /// <summary>
  /// Gets the direction of the ray.
  /// </summary>
  /// <returns>A <see cref="Vector3D"/>.</returns>
  Vector3D GetDirection() const { return m_direction; }
  /// <summary>
  /// Sets the direction of the ray.
  /// </summary>
  /// <param name="value">A <see cref="Vector3D"/> representing the direction.</param>
  void PutDirection(Vector3D value) { m_direction = value; }

  /// <summary>
  /// Gets the ray diameter at the near clipping distance from the origin.
  /// </summary>
  /// <returns>The diameter of the ray.</returns>
  double GetAperture() const { return m_aperture; }
  /// <summary>
  /// Sets the ray diameter at the near clipping distance from the origin.
  /// </summary>
  /// <param name="value">The diameter of the ray.</param>
  void PutAperture(double value) { m_aperture = value; }

private:
  Point3D m_origin = {0, 0, 0};
  Vector3D m_direction = {0, 0, -1};
  double m_aperture = 0;
};
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // CApertureRay_HPP_INCLUDED