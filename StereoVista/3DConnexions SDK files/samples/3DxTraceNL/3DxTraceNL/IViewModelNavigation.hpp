#ifndef IViewModelNavigation_HPP_INCLUDED
#define IViewModelNavigation_HPP_INCLUDED
// <copyright file="IViewModelNavigation.hpp" company="3Dconnexion">
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
#include <memory>

namespace TDx {
namespace TraceNL {
// Forward declarations
namespace Media2D {
struct Point2D;
} // namespace Media2D

namespace Media3D {
class CCamera3D;
class CModel3DGroup;
class CViewport3D;
class CApertureRay;
struct Point3D;
} // namespace Media3D

namespace Navigation {
/// <summary>
/// ViewModel interface used for the NavigationModel.
/// </summary>
class IViewModelNavigation {
public:
  /// <summary>
  /// Gets the Viewport.
  /// </summary>
  /// <returns>A <see cref="std::shared_ptr{T}"/> to the viewport used by the view model.</returns>
  virtual const std::shared_ptr<Media3D::CViewport3D> &GetViewport() const = 0;
  /// <summary>
  /// Gets the view model's camera.
  /// </summary>
  /// <returns>The <see cref="Media3D::CCamera3D"/> used by the view model.</returns>
  virtual Media3D::CCamera3D &GetCamera() = 0;
  /// <summary>
  /// Gets the view model's model.
  /// </summary>
  /// <returns>A <see cref="Media3D::CModel3DGroup"/> controlled by the view model.</returns>
  virtual const Media3D::CModel3DGroup &GetModel() const = 0;
  /// <summary>
  /// Gets the group of selected model parts.
  /// </summary>
  /// <returns>A <see cref="Media3D::CModel3DGroup"/> of the selected model parts.</returns>
  virtual const Media3D::CModel3DGroup &GetSelectedModel() const = 0;
  /// <summary>
  /// Gets the pivot position value.
  /// </summary>
  /// <returns>The <see cref="Media3D::Point3D"/> position of the pivot in world
  /// coordinates.</returns>
  virtual Media3D::Point3D GetPivotPosition() const = 0;
  /// <summary>
  /// Sets the position of the pivot.
  /// </summary>
  /// <param name="position">The <see cref="Media3D::Point3D"/> in world coordinates.</param>
  virtual void SetPivotPosition(Media3D::Point3D position) = 0;
  /// <summary>
  /// Gets a value indicating whether a user pivot is set.
  /// </summary>
  /// <returns>true if a user pivot is set, otherwise false.</returns>
  virtual bool IsUserPivot() const = 0;
  /// <summary>
  /// Gets a value indicating whether the pivot is visible.
  /// </summary>
  /// <returns>true if the pivot is visible, otherwise false.</returns>
  virtual bool GetPivotVisible() const = 0;
  /// <summary>
  /// Sets a value indicating whether the Pivot is visible.
  /// </summary>
  /// <param name="visible">true if the pivot is visible, otherwise false.</param>
  virtual void SetPivotVisible(bool visible) = 0;
  /// <summary>
  /// The start of a navigation transaction.
  /// </summary>
  virtual void BeginTransaction() = 0;
  /// <summary>
  /// The completion of a navigation transaction.
  /// </summary>
  virtual void EndTransaction() = 0;
  /// <summary>
  /// Performs hit testing on the model.
  /// </summary>
  /// <param name="hitRay">A <see cref="Media3D::CApertureRay"/> along which to perform the
  /// test.</param>
  /// <param name="selection">true to filter the hits to the current selection.</param>
  /// <param name="hit">A <see cref="Media3D::Point3D"/> containing the hit in world
  /// coordinates.</param>
  /// <returns>true if something was hit, false otherwise.</returns>
  virtual bool HitTest(const Media3D::CApertureRay &hitRay, bool selection,
                       Media3D::Point3D &hit) const = 0;
  /// <summary>
  /// Convert a 2D viewport point to world coordinates.
  /// </summary>
  /// <param name="p2D">The <see cref=""/> on the viewport to convert.</param>
  /// <returns>The <see cref="Media3D::Point3D"/> in world coordinates.</returns>
  virtual Media3D::Point3D ToWorldCoordinates(const Media2D::Point2D &p2D) const = 0;
};
} // namespace Navigation
} // namespace TraceNL
} // namespace TDx
#endif // IViewModelNavigation_HPP_INCLUDED