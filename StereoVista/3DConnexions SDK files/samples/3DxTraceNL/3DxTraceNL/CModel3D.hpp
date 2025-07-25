#ifndef CModel3D_HPP_INCLUDED
#define CModel3D_HPP_INCLUDED
// <copyright file="CModel3D.hpp" company="3Dconnexion">
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
#include "Rect3D.hpp"
#include "Transform3D.hpp"

// stdlib
#include <memory>
#include <stdexcept>

namespace TDx {
namespace TraceNL {
namespace Media3D {
/// <summary>
/// Provides functionality for 3-D models.
/// </summary>
class CModel3D {
public:
#if defined(_MSC_EXTENSIONS)
  /// <summary>
  /// Gets a <see cref="Rect3D"/> that specifies the axis-aligned bounding box of this model.
  /// </summary>
  __declspec(property(get = GetBounds)) Rect3D Bounds;

  /// <summary>
  /// Gets or sets the <see cref="Transform3D"/> set on the model.
  /// </summary>
  __declspec(property(get = GetTransform, put = PutTransform)) Transform3D Transform;
#endif

  /// <summary>
  /// Gets a <see cref="Rect3D"/> that specifies the axis-aligned bounding box of this model.
  /// </summary>
  Rect3D GetBounds() const { return m_transform.TransformBounds(m_bounds); }

  /// <summary>
  /// Gets or sets the <see cref="Transform3D"/> set on the model.
  /// </summary>
  Transform3D GetTransform() const { return m_transform; }
  void PutTransform(Transform3D value) { m_transform = value; }

private:
  Rect3D m_bounds = {-1, -1, -1, 2, 2, 2};
  Transform3D m_transform;
};
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // CModel3D_HPP_INCLUDED