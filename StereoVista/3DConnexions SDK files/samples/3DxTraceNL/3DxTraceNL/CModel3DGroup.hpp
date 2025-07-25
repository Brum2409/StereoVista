#ifndef CModel3DGroup_HPP_INCLUDED
#define CModel3DGroup_HPP_INCLUDED
// <copyright file="CModel3DGroup.hpp" company="3Dconnexion">
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
#include "CModel3D.hpp"

// stdlib
#include <stdexcept>
#include <vector>

namespace TDx {
namespace TraceNL {
namespace Media3D {

/// <summary>
/// Enables using a number of 3-D models as a unit.
/// </summary>
class CModel3DGroup final : public CModel3D {
public:
  /// <summary>
  ///  Gets the <see cref="std::vector"/> collection of <see cref="Model3D"/> objects.
  /// </summary>
  /// <returns>A const reference to the <see cref=" std::vector"/> <see cref="Model3D"/> collection.</returns>
  const std::vector<CModel3D> &GetChildren() const { return m_children; }

  /// <summary>
  ///  Gets the <see cref="std::vector"/> collection of <see cref="Model3D"/> objects.
  /// </summary>
  /// <returns>A reference to the <see cref=" std::vector"/> <see cref="Model3D"/> collection.</returns>
  std::vector<CModel3D> &GetChildren() { return m_children; }

  /// <summary>
  ///  Sets the <see cref="std::vector"/> collection of <see cref="Model3D"/> objects.
  /// </summary>
  /// <param name="children">A <see cref="std::vector"/> collection of <see cref="Model3D"/> objects.</param>
  /// <returns>A reference to the <see cref=" std::vector"/> <see cref="Model3D"/> collection.</returns>
  std::vector<CModel3D> &PutChildren(std::vector<CModel3D> children) {
    m_children = std::move(children);
    return m_children;
  }

  /// <summary>
  /// Gets a value indicating if the group is empty.
  /// </summary>
  /// <returns>true if the <see cref="CModel3DGroup"/> is empty, otherwise false.</returns>
  bool empty() const { return m_children.empty(); }

  /// <summary>
  /// Gets the axis-aligned bounding box of the <see cref="Model3DGroup"/>.
  /// </summary>
  /// <returns>A <see cref="Rect3D"/> bounding box for the group.</returns>
  Rect3D GetBounds() const {
    Rect3D bounds;
    for (auto iter = m_children.cbegin(); iter != m_children.cend(); ++iter) {
      bounds.Union(iter->GetBounds());
    }
    return bounds;
  }

private:
  std::vector<CModel3D> m_children;
};
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // CModel3DGroup_HPP_INCLUDED