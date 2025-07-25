#ifndef Point2D_HPP_INCLUDED
#define Point2D_HPP_INCLUDED
// <copyright file="Point2D.hpp" company="3Dconnexion">
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
namespace TDx {
namespace TraceNL {
namespace Media2D {

/// <summary>
/// Represents an x-, y--coordinate point in 2-D space.
/// </summary>
struct Point2D {
public:
  //
  // Summary:
  //     Initializes a new instance of the TDx.TraceNL.Media2D.Point2D
  //     structure.
  //
  // Parameters:
  //   x:
  //     The TDx.TraceNL.Media2D.Point2D.X value of the new
  //     TDx.TraceNL.Media.Media3D.Point3D structure.
  //
  //   y:
  //     The TDx.TraceNL.Media2D.Point2D.Y value of the new
  //     TDx.TraceNL.Media3D.Point3D structure.
  Point2D(double x, double y) : x(x), y(y) {}

public:
  double x, y;
};
} // namespace Media2D
} // namespace TraceNL
} // namespace TDx
#endif // Point2D_HPP_INCLUDED