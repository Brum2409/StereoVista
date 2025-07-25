#ifndef MatrixTransform3D_HPP_INCLUDED
#define MatrixTransform3D_HPP_INCLUDED
// <copyright file="MatrixTransform3D.hpp" company="3Dconnexion">
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
#include "Transform3D.hpp"

// stdlib
#include <memory>

namespace TDx {
namespace TraceNL {
namespace Media3D {
/// <summary>
/// Represents an affine transform define by a matrix in 3-D space.
/// </summary>
class MatrixTransform3D : public Transform3D {
  typedef Transform3D base_type;

public:
  //
  // Summary:
  //    Initializes a new instance of the MatrixTransform3D class.
  //
  MatrixTransform3D() {}
  //
  // Summary:
  //    Initializes a new instance of the MatrixTransform3D class
  //    using the specified Matrix3D.
  //
  explicit MatrixTransform3D(Matrix3D matrix) { m_value = matrix; }

#if defined(_MSC_EXTENSIONS)
  //
  // Summary:
  //     Gets or sets a Matrix3D that specifies a 3-D transformation.
  //
  // Returns:
  //     A Matrix3D that specifies a 3-D transformation.
  __declspec(property(get = GetMatrix, put = PutMatrix)) Matrix3D Matrix;
#endif

  //
  // Summary:
  //     Gets or sets a Matrix3D that specifies a 3-D transformation.
  //
  // Returns:
  //     A Matrix3D that specifies a 3-D transformation.
  Matrix3D GetMatrix() const { return m_value; }
  void PutMatrix(Matrix3D matrix) { m_value = matrix; }
};
} // namespace Media3D
} // namespace TraceNL
} // namespace TDx
#endif // MatrixTransform3D_HPP_INCLUDED