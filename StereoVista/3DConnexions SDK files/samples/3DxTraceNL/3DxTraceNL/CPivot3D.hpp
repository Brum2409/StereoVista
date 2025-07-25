#ifndef CPivot3D_HPP_INCLUDED
#define CPivot3D_HPP_INCLUDED
// <copyright file="CPivot3D.hpp" company="3Dconnexion">
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

namespace TDx {
namespace TraceNL {
namespace Media3D {
/// <summary>
/// The rotation pivot representation.
/// </summary>
class CPivot3D {
public:

  //
  // Summary:
  //     Gets or sets the position of the pivot in world coordinates.
  //
  Point3D GetPosition() const { return m_position; }
  void PutPosition(Point3D value) { m_position = value; }

  //
  // Summary:
  //     Gets or sets a value that indicates whether the pivot is visible.
  //
  bool IsVisible() const { return m_visible; }
  void Visible(bool value) { m_visible = value; }

private:
  Point3D m_position = {0, 0, 0};
  bool m_visible = true;
};
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // CPivot_HPP_INCLUDED