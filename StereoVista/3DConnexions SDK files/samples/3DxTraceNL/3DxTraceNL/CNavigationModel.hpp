#ifndef CNavigationModel_HPP_INCLUDED
#define CNavigationModel_HPP_INCLUDED
// <copyright file="CNavigationModel.hpp" company="3Dconnexion">
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

#include "CCommandEventArgs.hpp"
#include "CKeyEventArgs.hpp"
#include "CViewportViewModel.hpp"
#include "ISignals.hpp"
#include "MatrixTransform3D.hpp"
#include "nod/nod.hpp"
#include "IViewModelNavigation.hpp"

// SpaceMouse
#include <SpaceMouse/CNavigation3D.hpp>

namespace TDx {
namespace TraceNL {
namespace Navigation {
/// <summary>
/// Implements the Navigation3D interface.
/// </summary>
class CNavigationModel : public SpaceMouse::Navigation3D::CNavigation3D, private ISignals {
  typedef SpaceMouse::Navigation3D::CNavigation3D base_type;
public:
  CNavigationModel() = delete;

  /// <summary>
  /// Initializes a new instance of the CNavigationModel class.
  /// </summary>
  /// <param name="vm">The <see cref="IViewModelNavigation"/> interface to use.</param>
  explicit CNavigationModel(IViewModelNavigation *vm);

  // The signals
  nod::signal<void(CCommandEventArgs &e)> ExecuteCommand;
  nod::signal<void(CKeyEventArgs &e)> KeyDown;
  nod::signal<void(CKeyEventArgs &e)> KeyUp;
  nod::signal<void(void)> SettingsChanged;

  /// <summary>
  /// IAccessors overrides
  /// </summary>
protected:
  // IEvents overrides
  long SetActiveCommand(std::string commandId) override;
  long SetSettingsChanged(long change) override;
  long SetKeyPress(long vkey) override;
  long SetKeyRelease(long vkey) override;
  // IHit overrides
  long GetHitLookAt(navlib::point_t &position) const override;
  long SetHitAperture(double aperture) override;
  long SetHitDirection(const navlib::vector_t& direction) override;
  long SetHitLookFrom(const navlib::point_t& eye) override;
  long SetHitSelectionOnly(bool onlySelection) override;
  // IModel overrides
  long GetModelExtents(navlib::box_t &extents) const override;
  long GetSelectionExtents(navlib::box_t &extents) const override;
  long GetSelectionTransform(navlib::matrix_t &transform) const override;
  long GetIsSelectionEmpty(navlib::bool_t &empty) const override;
  long SetSelectionTransform(const navlib::matrix_t& matrix) override;
  // IPivot overrides
  long GetPivotPosition(navlib::point_t &position) const override;
  long IsUserPivot(navlib::bool_t &userPivot) const override;
  long SetPivotPosition(const navlib::point_t& position) override;
  long GetPivotVisible(navlib::bool_t &visible) const override;
  long SetPivotVisible(bool visible) override;
  // ISpace3D overrides.
  /// <inheritdoc/>
  long GetCoordinateSystem(navlib::matrix_t &matrix) const override;
  /// <inheritdoc/>
  long GetFrontView(navlib::matrix_t &matrix) const override;
  // IState overrides
  long SetTransaction(long transaction) override;
  long SetMotionFlag(bool motion) override;
  // IView overrides
  long GetCameraMatrix(navlib::matrix_t &matrix) const override;
  long GetCameraTarget(navlib::point_t &point) const override;
  long GetPointerPosition(navlib::point_t &position) const override;
  long GetViewConstructionPlane(navlib::plane_t &plane) const override;
  long GetViewExtents(navlib::box_t &extents) const override;
  long GetViewFOV(double &fov) const override;
  long GetViewFrustum(navlib::frustum_t &frustum) const override;
  long GetIsViewPerspective(navlib::bool_t &perspective) const override;
  long GetIsViewRotatable(navlib::bool_t &isRotatable) const override;
  long SetCameraMatrix(const navlib::matrix_t& matrix) override;
  long SetCameraTarget(const navlib::point_t& target) override;
  long SetPointerPosition(const navlib::point_t& position) override;
  long SetViewExtents(const navlib::box_t& extents) override;
  long SetViewFOV(double fov) override;
  long SetViewFrustum(const navlib::frustum_t& frustum) override;

  /// <summary>
  ///  ISignals overrides
  /// </summary>
protected:
  /// <summary>
  /// Raises the CommandExecuted signal
  /// </summary>
  /// <param name="e">A <see cref="CommandEventArgs"/> that contains the event data.</param>
  void OnExecuteCommand(CCommandEventArgs &e) override;
  /// <summary>
  /// Raises the SettingsChanged event.
  /// </summary>
  void OnSettingsChanged() override;
  /// <summary>
  /// Raises the KeyDown event.
  /// </summary>
  /// <param name="e">A <see cref="CKeyEventArgs"/> that contains the event data.</param>
  void OnKeyDown(CKeyEventArgs &e) override;
  /// <summary>
  /// Raises the KeyUp event.
  /// </summary>
  /// <param name="e">A <see cref="CKeyEventArgs"/> that contains the event data.</param>
  void OnKeyUp(CKeyEventArgs &e) override;

private:
  // interface to the ViewModel
  Navigation::IViewModelNavigation *m_ivm;
  std::mutex m_mutex;
  bool m_enableRaisingEvents = true;
  long m_settingsCount = -1;
  double m_time = 0;
  int m_frame = 0;

  Media3D::MatrixTransform3D m_selectionTransform;
};
} // namespace Navigation
} // namespace TraceNL
} // namespace TDx
#endif // CNavigationModel_HPP_INCLUDED