#ifndef CCamera3D_HPP_INCLUDED
#define CCamera3D_HPP_INCLUDED
// <copyright file="CCamera3D.hpp" company="3Dconnexion">
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
#include <cmath>
#include <stdexcept>

namespace TDx {
namespace TraceNL {
namespace Media3D {
/// <summary>
/// Provides functionality for 3-D cameras.
/// </summary>
class CCamera3D {
public:
  /// <summary>
  /// Gets or sets the distance to the camera's near clipping plane.
  /// </summary>
  /// <returns>A double that specifies the distance to the plane.</returns>
  double NearPlaneDistance = 0.01;

  /// <summary>
  /// Gets or set the distance to the camera's far clipping plane.
  /// </summary>
  /// <returns>A double that specifies the distance to the plane.</returns>
  double FarPlaneDistance = 1000.0;

  /// <summary>
  /// Gets or set the position of the camera in world coordinates.
  /// </summary>
  /// <returns>A <see cref="Point3D"/> that specifies the position of the
  /// camera.</returns>
  Point3D Position = {0, 0, 0};

  /// <summary>
  /// Gets or sets the direction in which the camera is looking.
  /// </summary>
  /// <returns>A unit <see cref="Vector3D"/> in world coordinates.</returns>
  Vector3D LookDirection = {0, 0, -1};

  /// <summary>
  /// Gets or sets the camera's upward direction.
  /// </summary>
  /// <returns>A unit <see cref="Vector3D"/> in world coordinates.</returns>
  Vector3D UpDirection = {0, 1, 0};

  /// <summary>
  /// Gets whether the camera uses a perspective projection.
  /// </summary>
  /// <returns>true if the camera uses a perspective projection, otherwise false.</returns>
  bool IsPerspective() const { return m_perspective; }

  /// <summary>
  /// Sets whether the camera uses a perspective projection.
  /// </summary>
  /// <param name="value">true to use a perspective projection, otherwise false.</param>
  void PutIsPerspective(bool value) { m_perspective = value; }

  /// <summary>
  /// Gets the world width of the orthographic camera's view on the projection plane.
  /// </summary>
  /// <returns>The camera view's width.</returns>
  double GetWidth() const {
    if (m_perspective) {
      return std::tan(m_fov / 2.0) * NearPlaneDistance * 2.0 * m_aspect;
    }

    return m_height * m_aspect;
  }

  /// <summary>
  /// Sets the world width of the orthographic camera's view at the near plane.
  /// </summary>
  /// <param name="value">The width of the view in world units.</param>
  /// <exception cref="std::runtime_error">If the camera is using a perspective projection.</exception>
  void PutWidth(double value) {
    if (m_perspective) {
      throw std::runtime_error("Cannot set the width of a perspective camera");
    }
    m_height = value / m_aspect;
  }

  /// <summary>
  /// Gets the vertical field of View of the perspective camera.
  /// </summary>
  /// <returns>The field of view angle in radians.</returns>
  double GetFov() const {
    if (!m_perspective) {
      throw std::runtime_error("Cannot get the field of view of an orthographic camera");
    }
    return m_fov;
  }

  /// <summary>
  /// Sets the vertical field of View of the perspective camera.
  /// </summary>
  /// <param name="value">The field of view angle in radians.</param>
  void PutFov(double value) {
    if (!m_perspective) {
      throw std::runtime_error("Cannot set the field of view of an orthographic camera");
    }
    m_fov = value;
  }

private:
  double m_farPlaneDistance = 1000.0;
  bool m_perspective = true;
  double m_height = 0.02;
  double m_aspect = 16. / 9.;
  double m_fov = 2.0 * std::atan(1.0);
};
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // CCamera3D_HPP_INCLUDED