#ifndef CViewportViewModel_HPP_INCLUDED
#define CViewportViewModel_HPP_INCLUDED
// <copyright file="CViewportViewModel.hpp" company="3Dconnexion">
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
#include "CApertureRay.hpp"
#include "CCamera3D.hpp"
#include "CModel3DGroup.hpp"
#include "CMouseButtonEventArgs.hpp"
#include "CPivot3D.hpp"
#include "CViewport3D.hpp"
#include "IViewModelNavigation.hpp"
#include "Point2D.hpp"

// stdlib
#include <memory>

namespace TDx {
namespace TraceNL {
/// <summary>
/// Contains the view-models.
/// </summary>
namespace ViewModels {
namespace Media3D = TDx::TraceNL::Media3D;
namespace Input = TDx::TraceNL::Input;

/// <summary>
/// View and camera projection enumeration.
/// </summary>
enum class Projection {
  /// <summary>
  /// Perspective projection.
  /// </summary>
  Perspective,
  /// <summary>
  /// Orthographic projection.
  /// </summary>
  Orthographic
};

/// <summary>
/// View-model for the <see cref="CViewport"/>.
/// </summary>
class CViewportViewModel : public Navigation::IViewModelNavigation {
public:

  /// <summary>
  /// Initializes a new instance of the CViewportViewModel class.
  /// </summary>
  CViewportViewModel() : m_projection(ViewModels::Projection::Perspective) {
    m_camera3D.PutIsPerspective(m_projection == ViewModels::Projection::Perspective);

    Media3D::CModel3DGroup model;
    model.GetChildren().emplace_back(Media3D::CModel3D());
    PutModel(std::move(model));
  }

  //
  // Summary:
  //     Gets or sets a value that specifies the projection of the camera.
  //
  ViewModels::Projection GetProjection() const { return m_projection; }
  void PutProjection(ViewModels::Projection value) {
    m_projection = value;
    m_camera3D.PutIsPerspective(value == ViewModels::Projection::Perspective);
  }

  //
  // Summary:
  //     Gets or sets the model displayed in the viewport.
  //
  void PutModel(Media3D::CModel3DGroup value) { m_model = std::move(value); }

  //
  // Summary:
  //     Clear the current selection.
  //
  void ClearSelection() { m_selectedGroup = Media3D::CModel3DGroup(); }

  //
  // Summary:
  //    The MouseButtonUpAction action for the middle mouse button sets or
  //    resets the user defined pivot.
  //
  // Parameters:
  //    e:
  //        The mouse button event data.
  //
  void MouseButtonUpAction(Input::CMouseButtonEventArgs &e);

  //
  // Summary:
  //     Select the complete model.
  //
  void SelectAll() { m_selectedGroup = m_model; }
  //
  // Summary:
  //     Sets the view controlled by the ViewModel
  //
  // Parameters:
  //     viewport:
  //            Shared pointer of the view used by the view model
  void SetView(const std::shared_ptr<Media3D::CViewport3D> &viewport) { m_viewport = viewport; }

private:
  //////////////////////////////////////////////////////////////////////////////////////
  // IViewModelNavigation interface implementation
  //////////////////////////////////////////////////////////////////////////////////////
  //
  // Summary:
  //     Gets the Viewport
  //
  // Returns:
  //     A shared pointer to the viewport used by the view model
  const std::shared_ptr<Media3D::CViewport3D> &GetViewport() const override { return m_viewport; }
  //
  // Summary:
  //     Gets the viewmodel's camera
  //
  // Parameters:
  //
  // Returns:
  //     The camera used by the view model
  Media3D::CCamera3D &GetCamera() override { return m_camera3D; }
  //
  // Summary:
  //     Gets the viewmodel's camera
  //
  // Parameters:
  //
  // Returns:
  //     The model used by the view model
  const Media3D::CModel3DGroup &GetModel() const override { return m_model; }
  //
  // Summary:
  //     Gets the group of selected model parts.
  //
  // Parameters:
  //
  // Returns:
  //     The current selection set
  const Media3D::CModel3DGroup &GetSelectedModel() const override { return m_selectedGroup; }
  //
  // Summary:
  //     Gets the Pivot position value.
  //
  // Parameters:
  //
  // Returns:
  //     The current pivot position
  Media3D::Point3D GetPivotPosition() const override { return m_pivot.GetPosition(); }
  //
  // Summary:
  //    Sets the Pivot position value.
  //
  // Parameters:
  //    position
  //        The position to set
  //
  void SetPivotPosition(Media3D::Point3D position) override { m_pivot.PutPosition(position); }
  //
  // Summary:
  //     Gets a value indicating whether a user pivot is set.
  //
  // Returns:
  //     true - a user pivot is set
  bool IsUserPivot() const override { return m_userPivot; }

  //
  // Summary:
  //     Gets a value indicating whether the Pivot is visible.
  //
  // Returns:
  //     true if the pivot is visible otherwise false.
  bool GetPivotVisible() const override { return m_pivot.IsVisible(); }

  //
  // Summary:
  //     Sets a value indicating whether the Pivot is visible.
  //
  // Parameters:
  //   visible:
  //     true if the pivot is visible otherwise false.
  void SetPivotVisible(bool visible) override { m_pivot.Visible(visible); }

  //
  // Summary:
  //     The start of a navigation transaction.
  //
  void BeginTransaction() override {
    // Inhibit the viewport from refreshing.
  }

  //
  // Summary:
  //     The end of a navigation transaction
  //
  void EndTransaction() override {
    // Enable viewport refresh and refresh.
  }

  //
  // Summary:
  //     Performs hit testing on the model.
  //
  // Parameters:
  //    hitRay
  //        The ray to use for the hit-testing.
  //    selection
  //        Filter the hits to the selection.
  //    hit
  //        The position of the hit in world coordinates.
  //
  // Returns:
  //     true if something was hit, false otherwise.
  bool HitTest(const Media3D::CApertureRay &hitRay, bool selection,
               Media3D::Point3D &hit) const override;
  //
  // Summary:
  //     Convert a 2D viewport CPoint to world coordinates
  //
  // Parameters:
  //    p2D
  //        The point on the viewport
  //
  // Returns:
  //     The Point3D in world coordinates
  Media3D::Point3D ToWorldCoordinates(const Media2D::Point2D &p2D) const override;

private:
  ViewModels::Projection m_projection;
  bool m_userPivot = false;
  Media3D::CPivot3D m_pivot;
  Media3D::CCamera3D m_camera3D;
  Media3D::CModel3DGroup m_model;
  Media3D::CModel3DGroup m_selectedGroup;
  std::shared_ptr<Media3D::CViewport3D> m_viewport;
};
} // namespace ViewModels
} // namespace TraceNL
} // namespace TDx
#endif // CViewportViewModel_HPP_INCLUDED