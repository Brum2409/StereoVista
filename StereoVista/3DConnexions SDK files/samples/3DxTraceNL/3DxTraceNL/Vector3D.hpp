#ifndef Vector3D_HPP_INCLUDED
#define Vector3D_HPP_INCLUDED
// <copyright file="Vector3D.hpp" company="3Dconnexion">
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
#include <cmath>

namespace TDx {
namespace TraceNL {
/// <summary>
/// Contains basic types that support 3-D presentation for this sample
/// </summary>
namespace Media3D {
/// <summary>
/// Represents a displacement in 3-D space.
/// </summary>
struct Vector3D {
public:
  /// <summary>
  /// Initializes a new instance of the TDx.TraceNL.Media3D.Vector3D structure.
  /// </summary>
  Vector3D() = default;

  /// <summary>
  /// Initializes a new instance of the <see cref="Vector3D"/> structure.
  /// </summary>
  /// <param name="x">The x value of the <see cref="Vector3D"/>.</param>
  /// <param name="y">The y value of the <see cref="Vector3D"/>.</param>
  /// <param name="z">The z value of the <see cref="Vector3D"/>.</param>
  Vector3D(double x, double y, double z) : x(x), y(y), z(z) {}

#if defined(_MSC_EXTENSIONS)
  __declspec(property(get = GetLength)) double Length;
#endif
  /// <summary>
  /// Gets the length of this <see cref="Vector3D"/> displacement.
  /// </summary>
  /// <returns>The length.</returns>
  double GetLength() const { return std::sqrt(x * x + y * y + z * z); }

  /// <summary>
  /// Normalizes the <see cref="Vector3D"/> to the unit length.
  /// <summary>
  /// </summary>
  /// <returns>A reference to this <see cref="Vector3D"/>.</returns>
  Vector3D &Normalize() {
    double factor = 1 / GetLength();
    x *= factor;
    y *= factor;
    z *= factor;
    return *this;
  }

  /// <summary>
  /// Scale a vector by a factor.
  /// </summary>
  /// <param name="d">The factor to scale the length of the <see cref="Vector3D"/> by.</param>
  /// <returns>A <see cref="Vector3D"/> scaled by the factor.</returns>
  Vector3D operator*(double d) const {
    Vector3D result(x * d, y * d, z * d);
    return result;
  }

  /// <summary>
  /// Add a <see cref="Vector3D"/> to this <see cref="Vector3D"/>.
  /// </summary>
  /// <param name="rhs">The second summand.</param>
  /// <returns>A <see cref="Vector3D"/> that is the sum of the two <see cref="Vector3D"/>s.</returns>
  Vector3D operator+(const Vector3D &rhs) const {
    Vector3D result(x + rhs.x, y + rhs.y, z + rhs.z);
    return result;
  }

  /// <summary>
  /// Scale this <see cref="Vector3D"/> by a factor in place.
  /// </summary>
  /// <param name="d">The scaling factor.</param>
  /// <returns>A reference to this <see cref="Vectro3D"/>.</returns>
  Vector3D &operator*=(double d) {
    x *= d;
    y *= d;
    z *= d;
    return *this;
  }

  /// <summary>
  /// Calculates the cross product of two <see cref="Vector3D"/>s.
  /// </summary>
  /// <param name="vector1">The first <see cref="Vector3D"/>.</param>
  /// <param name="vector2">The second <see cref="Vector3D"/>.</param>
  /// <returns>The <see cref="Vector3D"/> result of the cross product of vector1 and vector2.</returns>
  static Vector3D CrossProduct(const Vector3D &vector1, const Vector3D &vector2) {
    return Vector3D(vector1.y * vector2.z - vector1.z * vector2.y, vector1.z * vector2.x - vector1.x * vector2.z,
                    vector1.x * vector2.y - vector1.y * vector2.x);
  }

  /// <summary>
  /// Calculates the dot product of two <see cref="Vector3D"/>s.
  /// </summary>
  /// <param name="vector1">The first <see cref="Vector3D"/>.</param>
  /// <param name="vector2">The second <see cref="Vector3D"/>.</param>
  /// <returns>The dot product of vector1 and vector2.</returns>
  static double DotProduct(const Vector3D &vector1, const Vector3D &vector2) {
    return vector1.x * vector2.x + vector1.y * vector2.y + vector1.z * vector2.z;
  }

public:
  /// <summary>
  /// The x of the <see cref="Vector3D"/>.
  /// </summary>
  double x;
  /// <summary>
  /// The y of the <see cref="Vector3D"/>.
  /// </summary>
  double y;
  /// <summary>
  /// The z of the <see cref="Vector3D"/>.
  /// </summary>
  double z;
};

/// <summary>
/// Scale a vector by a factor.
/// </summary>
/// <param name="d">The factor to scale the length of the <see cref="Vector3D"/> by.</param>
/// <param name="v">The <see cref="Vector3D"/> to scale.</param>
/// <returns>A <see cref="Vector3D"/> scaled by the factor.</returns>
inline Vector3D operator*(double d, Vector3D v) {
  v *= d;
  return v;
}
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // Vector3D_HPP_INCLUDED