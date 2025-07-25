// <copyright file="CNavigationModel.cpp" company="3Dconnexion">
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
#include "CNavigationModel.hpp"

// Application Framework
#include "CModel3D.hpp"
#include "CModel3DGroup.hpp"
#include "Rect3D.hpp"

// stdlib
#include <cmath>
#include <iomanip>
#include <iostream>

namespace TDx {
namespace TraceNL {
namespace Navigation {
using namespace Media3D;

/// <summary>
/// Initializes a new instance of the CNavigationModel class.
/// </summary>
/// <param name="vm"></param>
/// <param name="vm">The <see cref="IViewModelNavigation"/> interface to use.</param>
/// <remarks>The 3dxtracenl sample requires the multi-threaded navlib.</remarks>
CNavigationModel::CNavigationModel(IViewModelNavigation *vm) : base_type(true, false), m_ivm(vm) {}

/// <summary>
/// IEvents overrides
/// </summary>
long CNavigationModel::SetActiveCommand(std::string commandId) {
  CCommandEventArgs e(std::move(commandId));

  if (m_enableRaisingEvents) {
    OnExecuteCommand(e);
    if (!e.Handled) {
      return navlib::make_result_code(navlib::navlib_errc::invalid_function);
    }
  }

  if (!e.Handled) {
    return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
  }

  return 0;
}

long CNavigationModel::SetSettingsChanged(long change) {
  if (change > m_settingsCount || change < 0 && m_settingsCount > 0) {
    m_settingsCount = change;
    if (m_enableRaisingEvents) {
      OnSettingsChanged();
    }
  }
  return 0;
}

long CNavigationModel::SetKeyPress(long vkey) {
  CKeyEventArgs e(true, vkey);

  if (m_enableRaisingEvents) {
    OnKeyDown(e);
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

long CNavigationModel::SetKeyRelease(long vkey) {
  CKeyEventArgs e(true, vkey);

  if (m_enableRaisingEvents) {
    OnKeyUp(e);
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// IHit overrides
/// </summary>
long CNavigationModel::GetHitLookAt(navlib::point_t &position) const {
  const CModel3DGroup &model = m_ivm->GetModel();
  if (model.empty()) {
    // We don't have a model
    return navlib::make_result_code(navlib::navlib_errc::no_data_available);
  }
  Rect3D bounds = model.GetBounds();
  position = {bounds.x + bounds.sizeX / 2.0, bounds.y + bounds.sizeY / 2.0,
              bounds.z + bounds.sizeZ / 2.0};
  return 0;
}
long CNavigationModel::SetHitAperture(double aperture) { return 0; }
long CNavigationModel::SetHitDirection(const navlib::vector_t &direction) { return 0; }
long CNavigationModel::SetHitLookFrom(const navlib::point_t &eye) { return 0; }
long CNavigationModel::SetHitSelectionOnly(bool onlySelection) { return 0; }

/// <summary>
/// IPivot overrides
/// </summary>
long CNavigationModel::GetPivotPosition(navlib::point_t &position) const {
  Point3D p = m_ivm->GetPivotPosition();
  position = {p.x, p.y, p.z};
  return 0;
}
long CNavigationModel::IsUserPivot(navlib::bool_t &userPivot) const {
  userPivot = m_ivm->IsUserPivot();
  return 0;
}
long CNavigationModel::SetPivotPosition(const navlib::point_t &position) {
  m_ivm->SetPivotPosition({position.x, position.y, position.z});
  return 0;
}
long CNavigationModel::GetPivotVisible(navlib::bool_t &visible) const {
  visible = m_ivm->GetPivotVisible();
  return 0;
}
long CNavigationModel::SetPivotVisible(bool visible) {
  m_ivm->SetPivotVisible(visible);
  return 0;
}

/// <summary>
/// ISpace3D overrides
/// </summary>

// Left-handed coordinate system (in this case x-right, y-out, z-up)
const Matrix3D cs(1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1);

// Front view (in this case the back)
const Matrix3D fv(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1);

long CNavigationModel::GetCoordinateSystem(navlib::matrix_t &matrix) const {
  matrix = {cs.m11, cs.m12, cs.m13, cs.m14, cs.m21,     cs.m22,     cs.m23,     cs.m24,
            cs.m31, cs.m32, cs.m33, cs.m34, cs.offsetX, cs.offsetY, cs.offsetZ, cs.m44};
  return 0;
}
long CNavigationModel::GetFrontView(navlib::matrix_t &matrix) const {
  matrix = {fv.m11, fv.m12, fv.m13, fv.m14, fv.m21,     fv.m22,     fv.m23,     fv.m24,
            fv.m31, fv.m32, fv.m33, fv.m34, fv.offsetX, fv.offsetY, fv.offsetZ, fv.m44};
  return 0;
}

/// <summary>
/// IState overrides
/// </summary>
long CNavigationModel::SetTransaction(long transaction) {
  using namespace std::chrono;

  if (transaction) {
    m_ivm->BeginTransaction();
  } else {
    m_ivm->EndTransaction();
    if (m_time == 0) {
      m_frame = 0;
      m_time = static_cast<double>(
                   duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch())
                       .count()) /
               1000000.;
    } else {
      ++m_frame;
#if TRACE_NAVLIB
      bool traceFPS = true;
#else
      bool traceFPS = std::fmod(m_frame, 60) == 0;
#endif
      if (traceFPS) {
        double now = static_cast<double>(duration_cast<microseconds>(
                                             high_resolution_clock::now().time_since_epoch())
                                             .count()) /
                     1000000.;

        double fps = m_frame / (now - m_time);
        std::cout << std::setprecision(4);
        std::cout << "Average FPS " << fps << std::endl;
      }
    }
  }
  return 0;
}
long CNavigationModel::SetMotionFlag(bool motion) {
  if (motion) {
    std::cout << "Navigation started..." << std::endl;
  } else {
    std::cout << "Navigation ended!" << std::endl;
  }

  m_time = 0;
  return 0;
}

/// <summary>
/// IView overrides
/// </summary>
long CNavigationModel::GetCameraMatrix(navlib::matrix_t &matrix) const {
  CCamera3D &camera = m_ivm->GetCamera();
  Vector3D up = camera.UpDirection;
  Vector3D forward = camera.LookDirection;
  Vector3D right = Vector3D::CrossProduct(forward, up);

  Point3D offset = camera.Position;

  matrix = {right.x,    right.y,    right.z,    0, up.x,     up.y,     up.z,     0,
            -forward.x, -forward.y, -forward.z, 0, offset.x, offset.y, offset.z, 1};

  return 0;
}
long CNavigationModel::GetCameraTarget(navlib::point_t &point) const {
  // We don't have a target
  return navlib::make_result_code(navlib::navlib_errc::no_data_available);
}
long CNavigationModel::GetPointerPosition(navlib::point_t &position) const {
  // For the purposes of this sample return a point somewhere in the viewport
  std::shared_ptr<CViewport3D const> viewport = m_ivm->GetViewport();

  // The mouse pointer position relative to the viewport
  Media2D::Point2D p2D(viewport->GetActualWidth() * 0.48, viewport->GetActualHeight() * (1.0 - 0.48));

  Point3D p3D = m_ivm->ToWorldCoordinates(p2D);
  position = {p3D.x, p3D.y, p3D.z};
  return 0;
}
long CNavigationModel::GetViewConstructionPlane(navlib::plane_t &plane) const {
  // We haven't got a construction plane that should be kept parallel to the
  // viewport
  return navlib::make_result_code(navlib::navlib_errc::no_data_available);
}
long CNavigationModel::GetViewExtents(navlib::box_t &extents) const {
  CCamera3D &camera = m_ivm->GetCamera();
  double halfWidth = camera.GetWidth() * 0.5;
  double aspectRatio = m_ivm->GetViewport()->GetActualWidth() / m_ivm->GetViewport()->GetActualHeight();
  double halfHeight = halfWidth / aspectRatio;
  extents = {-halfWidth, -halfHeight, -camera.FarPlaneDistance,
             halfWidth,  halfHeight,  camera.NearPlaneDistance};
  return 0;
}
long CNavigationModel::GetViewFOV(double &fov) const {
  CCamera3D &camera = m_ivm->GetCamera();
  if (!camera.IsPerspective()) {
    return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
  }
  fov = camera.GetFov();
  return 0;
}
long CNavigationModel::GetViewFrustum(navlib::frustum_t &frustum) const {
  CCamera3D &camera = m_ivm->GetCamera();
  if (!camera.IsPerspective()) {
    return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
  }

  double frustumHalfHeight = camera.NearPlaneDistance * tan(camera.GetFov() * 0.5);
  double aspect = m_ivm->GetViewport()->GetActualWidth() / m_ivm->GetViewport()->GetActualHeight();
  double frustumHalfWidth = frustumHalfHeight * aspect;
  frustum = {-frustumHalfWidth, frustumHalfWidth,         -frustumHalfHeight,
             frustumHalfHeight, camera.NearPlaneDistance, camera.FarPlaneDistance};

  return 0;
}
long CNavigationModel::GetIsViewPerspective(navlib::bool_t &perspective) const {
  perspective = m_ivm->GetCamera().IsPerspective();
  return 0;
}
long CNavigationModel::GetIsViewRotatable(navlib::bool_t &isRotatable) const {
  // Its a 3D camera so return true. For 2D (paper) return false
  isRotatable = true;
  return 0;
}
long CNavigationModel::SetCameraMatrix(const navlib::matrix_t &matrix) {
  CCamera3D &camera = m_ivm->GetCamera();
  camera.UpDirection = Vector3D(matrix.m10, matrix.m11, matrix.m12);
  camera.LookDirection = Vector3D(-matrix.m20, -matrix.m21, -matrix.m22);
  camera.Position = Point3D(matrix.m30, matrix.m31, matrix.m32);
  return 0;
}
long CNavigationModel::SetCameraTarget(const navlib::point_t &target) {
  // We don't have a camera target
  return navlib::make_result_code(navlib::navlib_errc::function_not_supported);
}
long CNavigationModel::SetPointerPosition(const navlib::point_t &position) {
  return navlib::make_result_code(navlib::navlib_errc::function_not_supported);
}
long CNavigationModel::SetViewExtents(const navlib::box_t &extents) {
  if (m_ivm->GetCamera().IsPerspective()) {
    return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
  }
  m_ivm->GetCamera().PutWidth(extents.max.x - extents.min.x);
  return 0;
}
long CNavigationModel::SetViewFOV(double fov) {
  if (!m_ivm->GetCamera().IsPerspective()) {
    return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
  }
  m_ivm->GetCamera().PutFov(fov);
  return 0;
}
long CNavigationModel::SetViewFrustum(const navlib::frustum_t &frustum) {
  return navlib::make_result_code(navlib::navlib_errc::function_not_supported);
}
/// <summary>
/// IModel overrides
/// </summary>
long CNavigationModel::GetModelExtents(navlib::box_t &extents) const {
  const CModel3DGroup &model = m_ivm->GetModel();
  if (model.empty()) {
    // We don't have a model
    return navlib::make_result_code(navlib::navlib_errc::no_data_available);
  }

  Media3D::Rect3D bounds = model.GetBounds();
  extents = {bounds.x,
             bounds.y,
             bounds.z,
             bounds.x + bounds.sizeX,
             bounds.y + bounds.sizeY,
             bounds.z + bounds.sizeZ};
  return 0;
}
long CNavigationModel::GetSelectionExtents(navlib::box_t &extents) const {
  const CModel3DGroup &model = m_ivm->GetSelectedModel();
  if (model.empty()) {
    // We don't have a selection
    return navlib::make_result_code(navlib::navlib_errc::no_data_available);
  }

  Media3D::Rect3D bounds = model.GetBounds();
  extents = {bounds.x,
             bounds.y,
             bounds.z,
             bounds.x + bounds.sizeX,
             bounds.y + bounds.sizeY,
             bounds.z + bounds.sizeZ};
  return 0;
}
long CNavigationModel::GetSelectionTransform(navlib::matrix_t &transform) const {
  const CModel3DGroup &model = m_ivm->GetSelectedModel();
  if (model.empty()) {
    // We don't have a selection
    return navlib::make_result_code(navlib::navlib_errc::no_data_available);
  }

  Matrix3D m = m_selectionTransform.GetMatrix();
  transform = {m.m11, m.m12, m.m13, m.m14, m.m21,     m.m22,     m.m23,     m.m24,
               m.m31, m.m32, m.m33, m.m34, m.offsetX, m.offsetY, m.offsetZ, m.m44};
  return 0;
}
long CNavigationModel::GetIsSelectionEmpty(navlib::bool_t &empty) const {
  const CModel3DGroup &model = m_ivm->GetSelectedModel();
  empty = model.empty();
  return 0;
}
long CNavigationModel::SetSelectionTransform(const navlib::matrix_t &matrix) {
  const CModel3DGroup &model = m_ivm->GetSelectedModel();
  if (model.empty()) {
    return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
  }
  Matrix3D transform(matrix.m00, matrix.m01, matrix.m02, matrix.m03, matrix.m10, matrix.m11,
                     matrix.m12, matrix.m13, matrix.m20, matrix.m21, matrix.m22, matrix.m23,
                     matrix.m30, matrix.m31, matrix.m32, matrix.m33);
  Matrix3D inverseTransform = m_selectionTransform.GetMatrix();
  inverseTransform.Invert();

  m_selectionTransform.PutMatrix(transform);

  Matrix3D delta = inverseTransform * transform;
  auto children = model.GetChildren();
  for (auto iter = children.begin(); iter != model.GetChildren().end(); ++iter) {
    iter->PutTransform(MatrixTransform3D(iter->GetTransform().GetMatrix() * delta));
  }

  return 0;
}

/// <summary>
/// Raises the CommandExecuted signal
/// </summary>
/// <param name="e">A <see cref="CommandEventArgs"/> that contains the event data.</param>
void CNavigationModel::OnExecuteCommand(CCommandEventArgs &e) { ExecuteCommand(e); }
/// <summary>
/// Raises the SettingsChanged event.
/// </summary>
void CNavigationModel::OnSettingsChanged() { SettingsChanged(); }
/// <summary>
/// Raises the KeyDown event.
/// </summary>
/// <param name="e">A <see cref="CKeyEventArgs"/> that contains the event data.</param>
void CNavigationModel::OnKeyDown(CKeyEventArgs &e) { KeyDown(e); }
/// <summary>
/// Raises the KeyUp event.
/// </summary>
/// <param name="e">A <see cref="CKeyEventArgs"/> that contains the event data.</param>
void CNavigationModel::OnKeyUp(CKeyEventArgs &e) { KeyUp(e); }
} // namespace Navigation
} // namespace TraceNL
} // namespace TDx