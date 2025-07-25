// Matrix3d.cpp: implementation of the Matrix3d class.
/*
 * Copyright (c) 2009-2018 3Dconnexion. All rights reserved.
 *
 * This file and source code are an integral part of the "3Dconnexion
 * Software Developer Kit", including all accompanying documentation,
 * and is protected by intellectual property laws. All use of the
 * 3Dconnexion Software Developer Kit is subject to the License
 * Agreement found in the "LicenseAgreementSDK.txt" file.
 * All rights not expressly granted by 3Dconnexion are reserved.
 */
///////////////////////////////////////////////////////////////////////////////
///
/// \file
/// This file contains the definition of the Matrix3d class.
///
#include "stdafx.h"
///////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: Matrix3d.cpp 18539 2021-07-08 11:39:11Z mbonk $
//
//
#include "Matrix3d.h"

// stdlib
#include <cmath>

// atl
#include <atltrace.h>

Matrix3d::Matrix3d() {
  Identity();
}

Matrix3d::~Matrix3d() {
}

Matrix3d Matrix3d::Inverse() const {
  Matrix3d result = *this;

  // Orientation is just a transpose
  result.m[1][0] = m[0][1];
  result.m[0][1] = m[1][0];
  result.m[2][0] = m[0][2];
  result.m[0][2] = m[2][0];
  result.m[2][1] = m[1][2];
  result.m[1][2] = m[2][1];

  // Position is negative of original position dotted with corresponding
  // orientation vector.
  result.m[3][0] =
      (-m[3][0] * m[0][0]) + (-m[3][1] * m[0][1]) + (-m[3][2] * m[0][2]);
  result.m[3][1] =
      (-m[3][0] * m[1][0]) + (-m[3][1] * m[1][1]) + (-m[3][2] * m[1][2]);
  result.m[3][2] =
      (-m[3][0] * m[2][0]) + (-m[3][1] * m[2][1]) + (-m[3][2] * m[2][2]);

  // Zero out last column
  result.m[0][3] = result.m[1][3] = result.m[2][3] = 0.;

  return result;
}

Matrix3d &Matrix3d::Invert() // inverts inplace
{
  *this = this->Inverse();
  return *this;
}

void Matrix3d::DumpMatrix(char *s) {
  _RPT1(_CRT_WARN, "\n%s\n", s);
  _RPT4(_CRT_WARN, "%f %f %f %f\n", m[0][0], m[0][1], m[0][2], m[0][3]);
  _RPT4(_CRT_WARN, "%f %f %f %f\n", m[1][0], m[1][1], m[1][2], m[1][3]);
  _RPT4(_CRT_WARN, "%f %f %f %f\n", m[2][0], m[2][1], m[2][2], m[2][3]);
  _RPT4(_CRT_WARN, "%f %f %f %f\n", m[3][0], m[3][1], m[3][2], m[3][3]);
}

Matrix3d Matrix3d::operator*(const Matrix3d &mat) const {
  Matrix3d result = *this;
  result *= mat;
  return result;
}

Matrix3d &Matrix3d::operator*=(const Matrix3d &mat) {
  Matrix3d tmp;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      tmp.m[i][j] = m[i][0] * mat.m[0][j] + m[i][1] * mat.m[1][j] +
                       m[i][2] * mat.m[2][j] + m[i][3] * mat.m[3][j];

  *this = tmp;
  return *this;
}

bool Matrix3d::operator==(const Matrix3d &mat) const {
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      if (fabs(m[i][j] - mat.m[i][j]) > kEpsilon5)
        return false;
  return true;
}

Matrix3d &Matrix3d::TranslateBy(const Vector3d &v) {
  m[3][0] += v.v[0];
  m[3][1] += v.v[1];
  m[3][2] += v.v[2];
  return *this;
}

Matrix3d &Matrix3d::SetPosition(const Point3d &p) {
  m[3][0] = p.p[0] * p.p[3] / m[3][3];
  m[3][1] = p.p[1] * p.p[3] / m[3][3];
  m[3][2] = p.p[2] * p.p[3] / m[3][3];
  return *this;
}

Vector3d Matrix3d::GetRow(size_t i) {
  Vector3d row;
  if (i < 4) {
    row.v[0] = m[i][0];
    row.v[1] = m[i][1];
    row.v[2] = m[i][2];
    row.v[3] = m[i][3];
  }
  return row;
}

Point3d Matrix3d::GetPosition() {
  return Point3d(m[3][0], m[3][1], m[3][2], m[3][3]);
}

Point3d operator*(const Point3d &p, const Matrix3d &m) {
  Point3d result;
  result.p[0] = m.m[0][0] * p.p[0] + m.m[1][0] * p.p[1] + m.m[2][0] * p.p[2] +
                m.m[3][0] * p.p[3];
  result.p[1] = m.m[0][1] * p.p[0] + m.m[1][1] * p.p[1] + m.m[2][1] * p.p[2] +
                m.m[3][1] * p.p[3];
  result.p[2] = m.m[0][2] * p.p[0] + m.m[1][2] * p.p[1] + m.m[2][2] * p.p[2] +
                m.m[3][2] * p.p[3];
  result.p[3] = m.m[0][3] * p.p[0] + m.m[1][3] * p.p[1] + m.m[2][3] * p.p[2] +
                m.m[3][3] * p.p[3];
  return result;
}

Vector3d operator*(const Vector3d &v, const Matrix3d &m) {
  Vector3d result;
  result.v[0] =
      m.m[0][0] * v.v[0] + m.m[1][0] * v.v[1] + m.m[2][0] * v.v[2] + m.m[3][0] * v.v[3];
  result.v[1] =
      m.m[0][1] * v.v[0] + m.m[1][1] * v.v[1] + m.m[2][1] * v.v[2] + m.m[3][1] * v.v[3];
  result.v[2] =
      m.m[0][2] * v.v[0] + m.m[1][2] * v.v[1] + m.m[2][2] * v.v[2] + m.m[3][2] * v.v[3];
  result.v[3] =
      m.m[0][3] * v.v[0] + m.m[1][3] * v.v[1] + m.m[2][3] * v.v[2] + m.m[3][3] * v.v[3];
  return result;
}
