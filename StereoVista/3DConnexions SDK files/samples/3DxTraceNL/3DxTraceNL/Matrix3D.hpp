#ifndef Matrix3D_HPP_INCLUDED
#define Matrix3D_HPP_INCLUDED
// <copyright file="Matrix3D.hpp" company="3Dconnexion">
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

// stdlib
#include <stdexcept>

#include "Point3D.hpp"
#include "Rect3D.hpp"
#include "Vector3D.hpp"

namespace TDx {
namespace TraceNL {
namespace Media3D {
/// <summary>
/// Represents an affine transform in 3-D space.
/// </summary>
struct Matrix3D {
public:
  /// <summary>
  /// Constructor that sets matrix's initial values to the identity matrix.
  /// </summary>
  Matrix3D() : Matrix3D(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) {}

  /// <summary>
  /// Constructor that sets matrix's initial values.
  /// </summary>
  /// <param name="m11">Value of the (1,1) field of the matrix.</param>
  /// <param name="m12">Value of the (1,2) field of the matrix.</param>
  /// <param name="m13">Value of the (1,3) field of the matrix.</param>
  /// <param name="m14">Value of the (1,4) field of the matrix.</param>
  /// <param name="m21">Value of the (2,1) field of the matrix.</param>
  /// <param name="m22">Value of the (2,2) field of the matrix.</param>
  /// <param name="m23">Value of the (2,3) field of the matrix.</param>
  /// <param name="m24">Value of the (2,4) field of the matrix.</param>
  /// <param name="m31">Value of the (3,1) field of the matrix.</param>
  /// <param name="m32">Value of the (3,2) field of the matrix.</param>
  /// <param name="m33">Value of the (3,3) field of the matrix.</param>
  /// <param name="m34">Value of the (3,4) field of the matrix.</param>
  /// <param name="offsetX">Value of the X offset field of the matrix.</param>
  /// <param name="offsetY">Value of the Y offset field of the matrix.</param>
  /// <param name="offsetZ">Value of the Z offset field of the matrix.</param>
  /// <param name="m44">Value of the (4,4) field of the matrix.</param>
  Matrix3D(double m11, double m12, double m13, double m14, double m21, double m22, double m23, double m24, double m31,
           double m32, double m33, double m34, double offsetX, double offsetY, double offsetZ, double m44)
      : m11(m11), m12(m12), m13(m13), m14(m14), m21(m21), m22(m22), m23(m23), m24(m24), m31(m31), m32(m32), m33(m33),
        m34(m34), offsetX(offsetX), offsetY(offsetY), offsetZ(offsetZ), m44(m44) {}

  /// <summary>
  /// Inverts this Matrix3D structure.
  /// </summary>
  void Invert() {
    Matrix3D result = {m11, m21, m31, 0, m12, m22, m32, 0, m13, m23, m33, 0, 0, 0, 0, 1};

    // Position is negative of original position dotted with corresponding
    // orientation vector.
    result.offsetX = (-offsetX * m11) + (-offsetY * m12) + (-offsetZ * m13);
    result.offsetY = (-offsetX * m21) + (-offsetY * m22) + (-offsetZ * m23);
    result.offsetZ = (-offsetX * m31) + (-offsetY * m32) + (-offsetZ * m33);

    *this = result;
  }

public:
  /// <summary>
  /// Matrix values
  /// </summary>
  double m11, m12, m13, m14, m21, m22, m23, m24, m31, m32, m33, m34, offsetX, offsetY, offsetZ, m44;
};

/// <summary>
/// Multiplies the specified matrices.
/// </summary>
/// <param name="m1">A <see cref="Matrix3D"/> to multiply.</param>
/// <param name="m2">The <see cref="Matrix3D"/> by which the first <see cref="Matrix3D"/> is multiplied.</param>
/// <returns>A <see cref="Matrix3D"/> that is the result of multiplication.</returns>
inline Matrix3D operator*(const Matrix3D &m1, const Matrix3D &m2) {
  Matrix3D result = {
      m1.m11 * m2.m11 + m1.m12 * m2.m21 + m1.m13 * m2.m31 + m1.m14 * m2.offsetX,
      m1.m11 * m2.m12 + m1.m12 * m2.m22 + m1.m13 * m2.m32 + m1.m14 * m2.offsetY,
      m1.m11 * m2.m13 + m1.m12 * m2.m23 + m1.m13 * m2.m33 + m1.m14 * m2.offsetZ,
      m1.m11 * m2.m13 + m1.m12 * m2.m23 + m1.m13 * m2.m33 + m1.m14 * m2.m44,
      m1.m21 * m2.m11 + m1.m22 * m2.m21 + m1.m23 * m2.m31 + m1.m24 * m2.offsetX,
      m1.m21 * m2.m12 + m1.m22 * m2.m22 + m1.m23 * m2.m32 + m1.m24 * m2.offsetY,
      m1.m21 * m2.m13 + m1.m22 * m2.m23 + m1.m23 * m2.m33 + m1.m24 * m2.offsetZ,
      m1.m21 * m2.m13 + m1.m22 * m2.m23 + m1.m23 * m2.m33 + m1.m24 * m2.m44,
      m1.m31 * m2.m11 + m1.m32 * m2.m21 + m1.m33 * m2.m31 + m1.m34 * m2.offsetX,
      m1.m31 * m2.m12 + m1.m32 * m2.m22 + m1.m33 * m2.m32 + m1.m34 * m2.offsetY,
      m1.m31 * m2.m13 + m1.m32 * m2.m23 + m1.m33 * m2.m33 + m1.m34 * m2.offsetZ,
      m1.m31 * m2.m13 + m1.m32 * m2.m23 + m1.m33 * m2.m33 + m1.m34 * m2.m44,
      m1.offsetX * m2.m11 + m1.offsetY * m2.m21 + m1.offsetZ * m2.m31 + m1.m44 * m2.offsetX,
      m1.offsetX * m2.m12 + m1.offsetY * m2.m22 + m1.offsetZ * m2.m32 + m1.m44 * m2.offsetY,
      m1.offsetX * m2.m13 + m1.offsetY * m2.m23 + m1.offsetZ * m2.m33 + m1.m44 * m2.offsetZ,
      m1.offsetX * m2.m13 + m1.offsetY * m2.m23 + m1.offsetZ * m2.m33 + m1.m44 * m2.m44,
  };
  return result;
}

/// <summary>
/// Multiplies a <see cref="Point3D" by a <see cref="Matrix3D"/>./>
/// </summary>
/// <param name="p">A <see cref="Point3D"/> to multiply</param>
/// <param name="m">The <see cref="Matrix3D"/> by which the first <see cref="Point3D"/> is multiplied.</param>
/// <returns>A <see cref="Point3D"/> that is the result of the multiplication.</returns>
inline Point3D operator*(const Point3D &p, const Matrix3D &m) {
  Point3D result(p.x * m.m11 + p.y * m.m21 + p.z * m.m31 + m.offsetX,
                 p.x * m.m12 + p.y * m.m22 + p.z * m.m32 + m.offsetY,
                 p.x * m.m13 + p.y * m.m23 + p.z * m.m33 + m.offsetZ);

  return result;
}

/// <summary>
/// Multiplies a <see cref="Vector3D" by a <see cref="Matrix3D"/>./>
/// </summary>
/// <param name="p">A <see cref="Vector3D"/> to multiply</param>
/// <param name="m">The <see cref="Matrix3D"/> by which the first <see cref="Point3D"/> is multiplied.</param>
/// <returns>A <see cref="Vector3D"/> that is the result of the multiplication.</returns>
inline Vector3D operator*(const Vector3D &v, const Matrix3D &m) {
  Vector3D result(v.x * m.m11 + v.y * m.m21 + v.z * m.m31, v.x * m.m12 + v.y * m.m22 + v.z * m.m32,
                  v.x * m.m13 + v.y * m.m23 + v.z * m.m33);

  return result;
}

/// <summary>
/// Multiplies a <see cref="Rect3D" by a <see cref="Matrix3D"/>./>
/// </summary>
/// <param name="p">A <see cref="Rect3D"/> to multiply</param>
/// <param name="m">The <see cref="Matrix3D"/> by which the first <see cref="Point3D"/> is multiplied.</param>
/// <returns>A <see cref="Rect3D"/> that is the result of the multiplication.</returns>
inline Rect3D operator*(const Rect3D &r, const Matrix3D &m) {
  Rect3D result;
  result.Union(Point3D(r.x, r.y, r.z) * m);
  result.Union(Point3D(r.x + r.sizeX, r.y, r.z) * m);
  result.Union(Point3D(r.x, r.y + r.sizeY, r.z) * m);
  result.Union(Point3D(r.x + r.sizeX, r.y + r.sizeY, r.z) * m);
  result.Union(Point3D(r.x, r.y, r.z + r.sizeZ) * m);
  result.Union(Point3D(r.x + r.sizeX, r.y, r.z + r.sizeZ) * m);
  result.Union(Point3D(r.x, r.y + r.sizeY, r.z + r.sizeZ) * m);
  result.Union(Point3D(r.x + r.sizeX, r.y + r.sizeY, r.z + r.sizeZ) * m);
  return result;
}
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // Matrix3D_HPP_INCLUDED