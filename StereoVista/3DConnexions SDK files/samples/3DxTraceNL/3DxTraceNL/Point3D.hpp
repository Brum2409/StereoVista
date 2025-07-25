#ifndef Point3D_HPP_INCLUDED
#define Point3D_HPP_INCLUDED
// <copyright file="Point3D.hpp" company="3Dconnexion">
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
#include "Vector3D.hpp"
namespace TDx {
namespace TraceNL {
namespace Media3D {
/// <summary>
/// Represents an x-, y-, and z-coordinate point in 3-D space.
/// </summary>
struct Point3D {
public:
  Point3D() = default;
  //
  // Summary:
  //     Initializes a new instance of the TDx.TraceNL.Media3D.Point3D
  //     structure.
  //
  // Parameters:
  //   x:
  //     The TDx.TraceNL.Media3D.Point3D.X value of the new
  //     TDx.TraceNL.Media.Media3D.Point3D structure.
  //
  //   y:
  //     The TDx.TraceNL.Media3D.Point3D.Y value of the new
  //     TDx.TraceNL.Media3D.Point3D structure.
  //
  //   z:
  //     The TDx.TraceNL.Media3D.Point3D.Z value of the new
  //     TDx.TraceNL.Media3D.Point3D structure.
  Point3D(double x, double y, double z) : x(x), y(y), z(z) {}
  //
  // Summary:
  //     Add a vector displacement to this point
  //
  // Parameters:
  //   rhs
  //     The vector to add
  //
  // Returns:
  //     The new point
  Point3D operator+(const Vector3D &rhs) const {
    Point3D result(x + rhs.x, y + rhs.y, z + rhs.z);
    return result;
  }
  //
  // Summary:
  //     Subtract a vector displacement to this point
  //
  // Parameters:
  //   rhs
  //     The vector to add
  //
  // Returns:
  //     The new point
  Point3D operator-(const Vector3D &rhs) const {
    Point3D result(x - rhs.x, y - rhs.y, z - rhs.z);
    return result;
  }
  //
  // Summary:
  //     Subtracts a TDx.TraceNL.Media3D.Point3D structure from this structure
  //     and returns the result as a TDx.TraceNL.Media3D.Vector3D structure.
  //
  // Parameters:
  //   rhs:
  //     The TDx.TraceNL.Media3D.Point3D structure to subtract from this
  //     structure.
  //
  // Returns:
  //     A TDx.TraceNL.Media.Media3D.Vector3D structure that represents the
  //     difference between the points.
  Vector3D operator-(const Point3D &rhs) const {
    Vector3D result(x - rhs.x, y - rhs.y, z - rhs.z);
    return result;
  }

public:
  double x, y, z;
};
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // Point3D_HPP_INCLUDED