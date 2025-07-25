#ifndef Transform3D_HPP_INCLUDED
#define Transform3D_HPP_INCLUDED
// <copyright file="Transform3D.hpp" company="3Dconnexion">
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
#include "Matrix3D.hpp"
#include "Rect3D.hpp"

namespace TDx {
namespace TraceNL {
namespace Media3D {
  /// <summary>
  /// Represents an affine transform in 3-D space.
  /// </summary>
class Transform3D {
public:
  /// <summary>
  /// Initializes a new instance of the Transform3D class.
  /// </summary>
  Transform3D() {}


  /// <summary>
  /// Gets the value of the current transformation.
  /// </summary>
  /// <returns>A <see cref="Matrix3D"/> that represents the value of the current transformation.</returns>
  Matrix3D GetMatrix() const { return m_value; }

  /// <summary>
  /// Transforms the specified 3-D bounding box.
  /// </summary>
  /// <param name="rect">The 3-D bounding box to transform.</param>
  /// <returns>The transformed 3-D bounding box.</returns>
  Rect3D TransformBounds(const Rect3D &rect) const {
    Rect3D result = rect * m_value;
    return result;
  }

protected:
  /// <summary>
  /// The <see cref="Matrix3D"/> representing the transform.
  /// </summary>
  Matrix3D m_value;
};
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // Transform3D_HPP_INCLUDED