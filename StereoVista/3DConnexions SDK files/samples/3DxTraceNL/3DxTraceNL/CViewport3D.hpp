#ifndef CViewport3D_HPP_INCLUDED
#define CViewport3D_HPP_INCLUDED
// <copyright file="CViewport3D.hpp" company="3Dconnexion">
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
namespace Media3D {
/// <summary>
/// Provides functionality for 3-D cameras.
/// </summary>
class CViewport3D {
public:
  /// <summary>
  /// Gets the viewport width.
  /// </summary>
  double GetActualWidth() const { return m_height * m_aspect; }

  /// <summary>
  /// Gets the viewport height.
  /// </summary>
  double GetActualHeight() const { return m_height; }

private:
  double m_height = 900;
  double m_aspect = 16. / 9.;
};
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // CViewport3D_HPP_INCLUDED