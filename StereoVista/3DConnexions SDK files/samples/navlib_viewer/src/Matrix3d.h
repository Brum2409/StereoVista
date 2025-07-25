#ifndef Matrix3d_H_INCLUDED_
#define Matrix3d_H_INCLUDED_
// Matrix3d.h
/*
 * Copyright (c) 2010-2018 3Dconnexion. All rights reserved.
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
/// This file contains the definition of the Point3, Vector3 template as well as
/// the Matrix3d classes.
///
///////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: Matrix3d.h 19813 2022-11-22 08:02:16Z mbonk $
//

// stdlib
#include <algorithm>
#include <iostream>
#include <memory>

static const double kEpsilon5 = 1.0e-5;

template <typename T> class Point3;

template <typename T> T _abs(const T &x) {
  return x > static_cast<T>(0) ? x : -x;
}

template <typename T> class Vector3 {
public:
  Vector3() {
    v[0] = v[1] = v[2] = 0.;
    v[3] = 0.;
  }
  Vector3(T x, T y, T z) {
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 0;
  }
  Vector3(T (&_v)[3]) {
    v[0] = _v[0];
    v[1] = _v[1];
    v[2] = _v[2];
    v[3] = 0;
  }

  virtual ~Vector3(){};

public:
  T Length() const {
    double result = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return static_cast<T>(result);
  }

  Vector3<T> &Negate() {
    v[0] = -v[0];
    v[1] = -v[1];
    v[2] = -v[2];
    return *this;
  }

  Vector3<T> &Normalize() {
    double len = sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));

    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
    return *this;
  }

  Vector3<T> UnitVector() const {
    Vector3<T> result(*this);
    return result.Normalize();
  }

  void Zero() {
    v[0] = v[1] = v[2] = 0.;
    v[3] = 0.;
  }
  void Set(T x, T y, T z) {
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 0;
  }

  T DotProduct(const Vector3<T> &vec) const {
    T result = (v[0] * vec.v[0]) + (v[1] * vec.v[1]) + (v[2] * vec.v[2]);
    return result;
  }

  void Dump(char *s) const {
    std::cout << s << " [" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << "]"
              << std::endl;
  }

  Vector3<T> CrossProduct(const Vector3<T> &vec) const {
    Vector3<T> result;
    result.v[0] = (v[1] * vec.v[2]) - (v[2] * vec.v[1]);
    result.v[1] = (v[2] * vec.v[0]) - (v[0] * vec.v[2]);
    result.v[2] = (v[0] * vec.v[1]) - (v[1] * vec.v[0]);
    return result;
  }

  Vector3<T> operator+(const Vector3<T> &vec) const {
    Vector3<T> result = *this;
    result += vec;
    return result;
  }

  Vector3<T> &operator+=(const Vector3<T> &vec) {
    v[0] += vec.v[0];
    v[1] += vec.v[1];
    v[2] += vec.v[2];
    return *this;
  }

  Vector3<T> operator-(const Vector3<T> &vec) const {
    Vector3<T> result = *this;
    result -= vec;
    return result;
  }

  Vector3<T> operator-() const {
    Vector3<T> result(-v[0], -v[1], -v[2]);
    return result;
  }

  Vector3<T> &operator-=(const Vector3<T> &vec) {
    v[0] -= vec.v[0];
    v[1] -= vec.v[1];
    v[2] -= vec.v[2];
    return *this;
  }

  Vector3<T> &operator/=(T factor) {
    v[0] /= factor;
    v[1] /= factor;
    v[2] /= factor;
    return *this;
  }

  Vector3<T> operator/(T factor) const {
    Vector3<T> result(*this);
    result /= factor;
    return result;
  }

  Vector3<T> &operator*=(T factor) {
    v[0] *= factor;
    v[1] *= factor;
    v[2] *= factor;
    return *this;
  }

  Vector3<T> operator*(T factor) const {
    Vector3<T> result(*this);
    result *= factor;
    return result;
  }

  bool operator==(const Vector3<T> &rhs) const {
    return (v[0] == rhs.v[0] && v[1] == rhs.v[1] && v[2] == rhs.v[2]);
  }

  bool operator!=(const Vector3<T> &rhs) const {
    return (v[0] != rhs.v[0] || v[1] != rhs.v[1] || v[2] != rhs.v[2]);
  }

  Vector3<T> &SingleAxisFilter() {
    T *plargest = &v[0];
    for (int i = 1; i < 3; i++)
      if (_abs(v[i]) > _abs(*plargest))
        plargest = &v[i];

    for (int i = 0; i < 3; i++)
      if (plargest != &v[i])
        v[i] = 0;

    return *this;
  }

  size_t MaxComponent() const {
    size_t largest = 0;
    for (size_t i = 1; i < 3; ++i)
      if (_abs(v[i]) > _abs(v[largest]))
        largest = i;

    return largest;
  }

  __declspec(property(get = GetX, put = PutX)) T x;
  T GetX() const {
    return v[0];
  }
  void PutX(T x) {
    v[0] = x;
  }

  __declspec(property(get = GetY, put = PutY)) T y;
  T GetY() const {
    return v[1];
  }
  void PutY(T y) {
    v[1] = y;
  }

  __declspec(property(get = GetZ, put = PutZ)) T z;
  T GetZ() const {
    return v[2];
  }
  void PutZ(T z) {
    v[2] = z;
  }

  __declspec(property(get = GetW, put = PutW)) T w;
  T GetW() const {
    return v[3];
  }
  void PutW(T w) {
    v[3] = w;
  }

  T v[4];
};

template <typename T> class Point3 {
public:
  Point3() {
    p[0] = p[1] = p[2] = 0.;
    p[3] = 1.;
  }

  Point3(T x, T y, T z, T w = T(1.)) {
    p[0] = x;
    p[1] = y;
    p[2] = z;
    p[3] = w;
  }

  Point3(T (&_p)[3]) {
    p[0] = _p[0];
    p[1] = _p[1];
    p[2] = _p[2];
    p[3] = T(1);
  }

  Point3(T (&_p)[4]) {
    p[0] = _p[0];
    p[1] = _p[1];
    p[2] = _p[2];
    p[3] = _p[3];
  }

  __declspec(property(get = GetX, put = PutX)) T x;
  T GetX() const {
    return p[0];
  }
  void PutX(T x) {
    p[0] = x;
  }

  __declspec(property(get = GetY, put = PutY)) T y;
  T GetY() const {
    return p[1];
  }
  void PutY(T y) {
    p[1] = y;
  }

  __declspec(property(get = GetZ, put = PutZ)) T z;
  T GetZ() const {
    return p[2];
  }
  void PutZ(T z) {
    p[2] = z;
  }

  __declspec(property(get = GetW, put = PutW)) T w;
  T GetW() const {
    return p[3];
  }
  void PutW(T w) {
    p[3] = w;
  }

  Vector3<T> operator-(const Point3<T> &rhs) const {
    return Vector3<T>(p[0] * p[3] - rhs.p[0] * rhs.p[3], p[1] * p[3] - rhs.p[1] * rhs.p[3],
                      p[2] * p[3] - rhs.p[2] * rhs.p[3]);
  }

  Point3<T> operator+(const Point3<T> &rhs) const {
    return Point3<T>(p[0] * p[3] + rhs.p[0] * rhs.p[3], p[1] * p[3] + rhs.p[1] * rhs.p[3],
                     p[2] * p[3] + rhs.p[2] * rhs.p[3]);
  }

  Point3<T> &operator+=(const Point3<T> &rhs) {
    if (p[3] == rhs.p[3]) {
      p[0] += rhs.p[0];
      p[1] += rhs.p[1];
      p[2] += rhs.p[2];
    } else {
      p[0] = p[0] * p[3] + rhs.p[0] * rhs.p[3];
      p[1] = p[1] * p[3] + rhs.p[1] * rhs.p[3];
      p[2] = p[2] * p[3] + rhs.p[2] * rhs.p[3];
      p[3] = T(1.);
    }
    return *this;
  }

  Point3<T> operator+(const Vector3<T> &rhs) const {
    return Point3<T>(p[0] * p[3] + rhs.v[0], p[1] * p[3] + rhs.v[1], p[2] * p[3] + rhs.v[2]);
  }

  Point3<T> &operator+=(const Vector3<T> &rhs) {
    if (p[3] == T(1)) {
      p[0] += rhs.v[0];
      p[1] += rhs.v[1];
      p[2] += rhs.v[2];
    } else {
      p[0] = p[0] * p[3] + rhs.v[0];
      p[1] = p[1] * p[3] + rhs.v[1];
      p[2] = p[2] * p[3] + rhs.v[2];
      p[3] = T(1.);
    }
    return *this;
  }

  Point3<T> operator-(const Vector3<T> &rhs) const {
    return Point3<T>(p[0] * p[3] - rhs.v[0], p[1] * p[3] - rhs.v[1], p[2] * p[3] - rhs.v[2]);
  }

  Point3<T> operator/(T f) const {
    return Point3<T>(p[0] / f, p[1] / f, p[2] / f, p[3]);
  }

  Point3<T> &operator/=(T f) {
    p[0] /= f;
    p[1] /= f;
    p[2] /= f;
    return *this;
  }

  bool operator==(const Point3<T> &rhs) const {
    return (p[0] * p[3] == rhs.p[0] * rhs.p[3] && p[1] * p[3] == rhs.p[1] * rhs.p[3] &&
            p[2] * p[3] == rhs.p[2] * rhs.p[3]);
  }

  bool operator!=(const Point3<T> &rhs) const {
    return (p[0] * p[3] != rhs.p[0] * rhs.p[3] || p[1] * p[3] != rhs.p[1] * rhs.p[3] ||
            p[2] * p[3] != rhs.p[2] * rhs.p[3]);
  }

  void Dump(char *s) const {
    std::cout << s << " [" << p[0] << ", " << p[1] << ", " << p[2] << ", " << p[3] << "]"
              << std::endl;
  }

  T p[4];
};

typedef Vector3<double> Vector3d;
typedef Point3<double> Point3d;

typedef double const* matrix16_t;

/// <summary>
/// Matrix class for affine transformations.
/// </summary>
/// <remarks>The matrix class stores the affine data as row vectors.</remarks>
class Matrix3d {
public:
  Vector3d GetRow(size_t row);
  Point3d GetPosition();
  Matrix3d &SetPosition(const Point3d &pos);
  Matrix3d &TranslateBy(const Vector3d &v);
  Matrix3d();

  Matrix3d(double m00, double m01, double m02, double m03, double m10, double m11, double m12,
           double m13, double m20, double m21, double m22, double m23, double m30, double m31,
           double m32, double m33)
      : m{{m00, m01, m02, m03}, {m10, m11, m12, m13}, {m20, m21, m22, m23}, {m30, m31, m32, m33}} {
  }

  Matrix3d(const double matrix[16]) {
    std::copy(&matrix[0], &matrix[16], &m[0][0]);
  }

  virtual ~Matrix3d();

  void Identity() {
    double m_[16] = {1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.};
    std::copy(&m_[0], &m_[16], &m[0][0]);
  }

  Matrix3d Inverse() const;
  Matrix3d &Invert(); // inverts in place.
  void DumpMatrix(char *s);

  Matrix3d operator*(const Matrix3d &mat) const;
  Matrix3d &operator*=(const Matrix3d &mat);
  bool operator==(const Matrix3d &mat) const;

  operator matrix16_t () const {
    return reinterpret_cast<double const *>(m);
  }

  double m[4][4];
};

Point3d operator*(const Point3d &p, const Matrix3d &);
Vector3d operator*(const Vector3d &p, const Matrix3d &);

class MatrixStack {
public:
  MatrixStack() : m_nsize(32), m_topOfStack(0), m_pStack(new Matrix3d[m_nsize]) {
    m_pStack[m_topOfStack].Identity();
  }
  MatrixStack(int size) : m_nsize(size), m_topOfStack(0), m_pStack(new Matrix3d[m_nsize]) {
    m_pStack[m_topOfStack].Identity();
  }
  virtual ~MatrixStack() {
  }
  bool IsFull() {
    return m_topOfStack >= m_nsize - 1;
  }
  const Matrix3d &TopMatrix() {
    return m_pStack[m_topOfStack];
  }
  void LoadMatrix(const Matrix3d &m) {
    m_pStack[m_topOfStack] = m;
  }
  void PushMatrix() {
    if (m_topOfStack <= m_nsize - 2) {
      m_pStack[m_topOfStack + 1] = m_pStack[m_topOfStack];
      m_topOfStack++;
    }
  }
  void PopMatrix() {
    if (m_topOfStack != 0)
      m_topOfStack--;
  }
  void MultMatrix(const Matrix3d &m) {
    m_pStack[m_topOfStack] = m * m_pStack[m_topOfStack];
  }

private:
  int m_nsize;
  int m_topOfStack; // (has something in it or is -1 if stack is empty)
  std::unique_ptr<Matrix3d[]> m_pStack;
};

#include <ostream>

template <class T> std::ostream &operator<<(std::ostream &stream, const Point3<T> &value) {
  stream << "[" << value.x << ", " << value.y << ", " << value.z << "]";
  return stream;
}

#endif // Matrix3d_H_INCLUDED_
