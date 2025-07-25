#ifndef Rect3D_HPP_INCLUDED
#define Rect3D_HPP_INCLUDED
// <copyright file="Rect3D.hpp" company="3Dconnexion">
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

namespace TDx {
namespace TraceNL {
namespace Media3D {

/// <summary>
/// Represents a 3-D rectangle: for example, a cube.
/// </summary>
struct Rect3D {
public:
  //
  // Summary:
  //     Initializes a new empty instance of the TDx.TraceNL.Media3D.Rect3D
  //     structure.
  //
  Rect3D() : x(0), y(0), z(0), sizeX(-1), sizeY(-1), sizeZ(-1) {}
  //
  // Summary:
  //     Initializes a new instance of the TDx.TraceNL.Media3D.Rect3D
  //     structure.
  //
  // Parameters:
  //   x:
  //     X-axis coordinate of the new TDx.TraceNL.Media3D.Rect3D.
  //
  //   y:
  //     Y-axis coordinate of the new TDx.TraceNL.Media3D.Rect3D.
  //
  //   z:
  //     Z-axis coordinate of the new TDx.TraceNL.Media3D.Rect3D.
  //
  //   sizeX:
  //     Size of the new TDx.TraceNL.Media3D.Rect3D in the X dimension.
  //
  //   sizeY:
  //     Size of the new TDx.TraceNL.Media3D.Rect3D in the Y dimension.
  //
  //   sizeZ:
  //     Size of the new TDx.TraceNL.Media3D.Rect3D in the Z dimension.
  Rect3D(double x, double y, double z, double sizeX, double sizeY, double sizeZ)
      : x(x), y(y), z(z), sizeX(sizeX), sizeY(sizeY), sizeZ(sizeZ) {
    if (sizeX < 0 || sizeY < 0 || sizeZ < 0) {
      throw std::runtime_error("Size values must be positive");
    }
  }

  //
  // Summary:
  //     Gets a value that indicates if the Rect3D is empty
  //
  // Returns:
  //     true if the Rect3D is empty, false otherwise
  bool IsEmpty() const { return sizeX < 0; }

  //
  // Summary:
  //     Updates this Rect3D to reflect the union of the Rect3D and the
  //     specified Point3D.
  //
  // Parameters:
  //   point:
  //     The Point3D whose union with the current Rect3D is to be evaluated.
  //
  void Union(Point3D point) {
    if (IsEmpty()) {
      x = point.x;
      y = point.y;
      z = point.z;
      sizeX = sizeY = sizeZ = 0;
    } else {
      if (point.x < x) {
        x = point.x;
        sizeX += x - point.x;
      } else if (point.x > x + sizeX) {
        sizeX = point.x - x;
      }
      if (point.y < y) {
        y = point.y;
        sizeX += y - point.y;
      } else if (point.y > y + sizeY) {
        sizeY = point.y - y;
      }
      if (point.z < z) {
        z = point.z;
        sizeZ += z - point.z;
      } else if (point.z > z + sizeZ) {
        sizeZ = point.z - z;
      }
    }
  }
  //
  // Summary:
  //     Updates this Rect3D to reflect the union of the Rect3D and a second
  //     specified Rect3D.
  //
  // Parameters:
  //   rect:
  //     The Rect3D whose union with the current Rect3D is to be evaluated.
  //
  void Union(Rect3D rect) {
    if (rect.IsEmpty()) {
      return;
    }

    if (IsEmpty()) {
      *this = rect;
    } else {
      Point3D max(x + sizeX, x + sizeX, x + sizeX);
      Point3D rectMax(rect.x + rect.sizeX, rect.y + rect.sizeY, rect.z + rect.sizeZ);
      if (rect.x < x) {
        x = rect.x;
      }
      if (rect.y < y) {
        y = rect.y;
      }
      if (rect.z < z) {
        z = rect.z;
      }
      if (rectMax.x > max.x) {
        max.x = rectMax.x;
      }
      if (rectMax.y > max.y) {
        max.y = rectMax.y;
      }
      if (rectMax.z > max.z) {
        max.z = rectMax.z;
      }
      sizeX = max.x - x;
      sizeY = max.y - y;
      sizeZ = max.z - z;
    }
  }

public:
  double x, y, z, sizeX, sizeY, sizeZ;
};
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // Rect3D_HPP_INCLUDED