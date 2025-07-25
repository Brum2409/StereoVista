// <copyright file="CViewportViewModel.cpp" company="3Dconnexion">
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
#include "CViewportViewModel.hpp"

namespace TDx {
namespace TraceNL {
namespace ViewModels {
//
// Summary:
//    The MouseButtonUpAction action for the middle mouse button sets or resets
//    the user defined pivot.
//
// Parameters:
//    e:
//        The mouse button event data.
//
void CViewportViewModel::MouseButtonUpAction(Input::CMouseButtonEventArgs &e) {
  using namespace Input;
  using namespace Media3D;
  using namespace Media2D;
  if (e.GetChangedButton() != MouseButton::Middle) {
    return;
  }

  Point2D mousePosition = e.GetPosition();
  Point3D clickPosition = ToWorldCoordinates(mousePosition);
  Vector3D direction;
  Point3D origin;
  if (m_camera3D.IsPerspective()) {
    direction = clickPosition - m_camera3D.Position;
    direction.Normalize();
    origin = m_camera3D.Position;
  } else {
    direction = m_camera3D.LookDirection;
    origin = clickPosition - m_camera3D.LookDirection;
  }

  CApertureRay ray(origin, direction,
                   m_camera3D.GetWidth() / m_viewport->GetActualWidth());

  Point3D hitPoint;
  bool hit = HitTest(ray, false, hitPoint);
  if (hit) {
    m_pivot.PutPosition(hitPoint);
  }

  m_userPivot = hit;
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
bool CViewportViewModel::HitTest(const Media3D::CApertureRay &hitRay,
                                 bool selection, Media3D::Point3D &hit) const {
  return false;
}
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
Media3D::Point3D
CViewportViewModel::ToWorldCoordinates(const Media2D::Point2D &p2D) const {
  using Media3D::Point3D;
  using Media3D::Vector3D;

  // Normalize the 2D point to the center of the viewport in normalized
  // coordinates [-0.5,0.5]
  Point3D normalized((p2D.x / m_viewport->GetActualWidth()) - 0.5,
                     0.5 - (p2D.y / m_viewport->GetActualHeight()), 0);

  double aspectRatio = m_viewport->GetActualWidth() / m_viewport->GetActualHeight();

  // Offset from the center of the screen to the pointer position on the near
  // plane
  Vector3D offset = (normalized.x * m_camera3D.GetWidth() *
                         Vector3D::CrossProduct(m_camera3D.LookDirection,
                                                m_camera3D.UpDirection) +
                     (normalized.y * (m_camera3D.GetWidth() / aspectRatio) *
                      m_camera3D.UpDirection));

  // Instead of the near plane it might be better to use either the projection
  // plane distance or possibly the target distance
  Point3D center = m_camera3D.Position +
                   (m_camera3D.NearPlaneDistance * m_camera3D.LookDirection);
  Point3D point = center + offset;
  return point;
}
} // namespace ViewModels
} // namespace TraceNL
} // namespace TDx