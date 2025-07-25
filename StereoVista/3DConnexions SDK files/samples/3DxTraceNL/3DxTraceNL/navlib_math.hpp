#ifndef navlib_math_HPP_INCLUDED
#define navlib_math_HPP_INCLUDED
// <copyright file="navlib_math.hpp" company="3Dconnexion">
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
#include <navlib/navlib_types.h>
#if __clang__
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif
namespace navlib {
inline matrix_t Inverse(const matrix_t &m) {
  // Assumes an affine matrix with just translation and rotation
  matrix_t result = {m.m00, m.m10, m.m20, 0, m.m01, m.m11, m.m21, 0, m.m02, m.m12, m.m22, 0, 0, 0, 0, 1};

  // Position is negative of original position dotted with corresponding
  // orientation vector.
  result.m30 = (-m.m30 * m.m00) + (-m.m31 * m.m01) + (-m.m32 * m.m02);
  result.m31 = (-m.m30 * m.m10) + (-m.m31 * m.m11) + (-m.m32 * m.m12);
  result.m32 = (-m.m30 * m.m20) + (-m.m31 * m.m21) + (-m.m32 * m.m22);

  return result;
}

inline matrix_t operator*(const matrix_t &lhs, const matrix_t &rhs) {
  matrix_t result;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      result(i, j) = lhs(i, 0) * rhs(0, j) + lhs(i, 1) * rhs(1, j) +
                          lhs(i, 2) * rhs(2, j) + lhs(i, 3) * rhs(3, j);
    }
  }

  return result;
}
} // namespace navlib
#endif // navlib_math_HPP_INCLUDED