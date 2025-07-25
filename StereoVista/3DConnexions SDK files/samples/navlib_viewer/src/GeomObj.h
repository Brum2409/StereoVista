#if !defined(AFX_GEOMOBJ_H__1D1F0D61_D24F_11D3_941D_D10394075A73__INCLUDED_)
#define AFX_GEOMOBJ_H__1D1F0D61_D24F_11D3_941D_D10394075A73__INCLUDED_
// GeomObj.h: interface for the CGeomObj class.
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
/// This file contains the declaration of the classes used to hold geometry
/// data.
///
///////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: GeomObj.h 19813 2022-11-22 08:02:16Z mbonk $
//
// 17.09.10 MSB  Added selection filter to the CExtents
//               Added helper functions to Vector3d
// 20.10.09 MSB  Based on code from Jim Wick's BycycleDI sample
//

#include "Matrix3d.h"
#include "float.h"
#include "gl/gl.h"

// stdlib
#include <array>
#include <memory>
#include <vector>
typedef std::array<Point3d, 3> triangle_t;
typedef std::array<Vector3d, 3> trivector_t;

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CMCADView;

enum eRenderStyle {
  eBoundingBox,
  eWireFrame,
  eHighlightWireFrame,
  eSmoothShaded
};

class CExtents {
public:
  CExtents(const Point3d &min, const Point3d &max)
      : m_minPt(min), m_maxPt(max) {}

  CExtents()
      : m_minPt(DBL_MAX, DBL_MAX, DBL_MAX),
        m_maxPt(-DBL_MAX, -DBL_MAX, -DBL_MAX) {}
  Point3d m_minPt;
  Point3d m_maxPt;

  bool IsEmpty() const {
    return (m_maxPt.x < m_minPt.x || m_maxPt.y < m_minPt.y ||
            m_maxPt.z < m_minPt.z);
  }

  Point3d Center() const { return m_minPt + (m_maxPt - m_minPt) / 2.; }

  Vector3d Diagonal() const { return (m_maxPt - m_minPt); }

  Point3d operator[](size_t i) const {
    switch (i) {
    case 0:
      return m_minPt;
    case 1:
      return Point3d(m_maxPt.x, m_minPt.y, m_minPt.z);
    case 2:
      return Point3d(m_minPt.x, m_maxPt.y, m_minPt.z);
    case 3:
      return Point3d(m_maxPt.x, m_maxPt.y, m_minPt.z);
    case 4:
      return Point3d(m_minPt.x, m_minPt.y, m_maxPt.z);
    case 5:
      return Point3d(m_maxPt.x, m_minPt.y, m_maxPt.z);
    case 6:
      return Point3d(m_minPt.x, m_maxPt.y, m_maxPt.z);
    case 7:
      return m_maxPt;
    default:
      AfxThrowMemoryException();
    }
  }
};

class CGeometry {
public:
  Point3d m_minPt;
  Point3d m_maxPt;
  Point3d m_sum;
  Point3d m_centroid;
  Point3d m_center;
  virtual void MoveVertices(Vector3d v){};
  virtual void CalculateStatistics(){};
  CString m_strName;
  CGeometry();
  virtual ~CGeometry(){};

  virtual void Draw(eRenderStyle renderStyle) { };
  GLuint m_pickName;

private:
  static GLuint m_pickNameCount;
};

class CBlock : public CGeometry {
public:
  CBlock();
  CBlock(double sx, double sy, double sz);
  virtual ~CBlock(){};

  void Draw(eRenderStyle renderStyle);

private:
  double m_sx, m_sy, m_sz; // size along x, y, z
  Vector3d m_fur, m_flr, m_fll, m_ful, m_rur, m_rlr, m_rll, m_rul;
};

class COctahedron : public CGeometry {
public:
  COctahedron();
  COctahedron(double sx, double sy, double sz);
  virtual ~COctahedron(){};

  void Draw(eRenderStyle renderStyle);

private:
  double m_sx, m_sy, m_sz; // size along x, y, z
  Vector3d m_front, m_back, m_ll, m_lr, m_ur, m_ul;
};

class CTriad : public CGeometry {
public:
  CTriad();
  CTriad(double sx, double sy, double sz);
  virtual ~CTriad(){};

  void Draw(eRenderStyle renderStyle);

private:
  double m_sx, m_sy, m_sz; // size along x, y, z
};

class Material {
public:
  Material() : m_Ns(0), m_illum(1) {
    m_Kd.fill(0.8f);
    m_Kd[3] = 1.0f;
    m_Ka.fill(0.2f);
    m_Ka[3] = 1.0f;
    m_Ks.fill(0.f);
    m_Ks[3] = 1.0f;
  }

public:
#ifdef UNICODE
  std::wstring m_name;
#else
  std::string m_name;
#endif
  std::array<float, 4> m_Kd;
  std::array<float, 4> m_Ka;
  std::array<float, 4> m_Ks;
  int m_Ns;
  int m_illum;
};

enum Constraint {
  CS_NoMovementAllowed = 0,
  CS_RxAllowed = 1,
  CS_RyAllowed = 2,
  CS_RzAllowed = 4,
  CS_TxAllowed = 8,
  CS_TyAllowed = 16,
  CS_TzAllowed = 32,
  CS_EverythingAllowed = 0xffffffff
};

class CVisitor;

class CGeomObj {
public:
  virtual bool VertexPick(CGeomObj *pworldobj, int dcx, int dcy,
                          Matrix3d &worldToDC);
  bool WalkTree(CVisitor &visitor);
  virtual void Constrain(Vector3d &rotVec, Vector3d &transVec);
  void SetConstraint(Constraint c);
  Constraint GetConstraint();
  Matrix3d m_localXformToObj;
  Matrix3d m_positionInParent;
  Matrix3d m_resetPosition;
  CGeomObj *m_pParent;
  CGeomObj(CGeometry *pGeom);
  CGeomObj();
  virtual ~CGeomObj();

  CExtents GetExtents(const Matrix3d &frame,
                      const std::vector<int> *selection = NULL) {
    CExtents extents;
    getExtents(frame, extents, selection);
    return extents;
  }

  Matrix3d GetAffineTM(unsigned int pickName) { return getAffineTM(pickName); }

  std::unique_ptr<CGeomObj> m_pNext; // siblings of this node (not children)
  std::unique_ptr<CGeometry> m_pGeometry;
  std::unique_ptr<CGeomObj> m_pChildren; // children in the hierarchy

private:
  virtual void getExtents(const Matrix3d &frame, CExtents &extents,
                          const std::vector<int> *selection);
  virtual Matrix3d getAffineTM(unsigned int pickName);
  Constraint m_constraint;

protected:
};

class CWavefrontObj : public CGeomObj {
public:
  CWavefrontObj();
  CWavefrontObj(const TCHAR *filename);
  virtual ~CWavefrontObj();

public:
  int m_currentMaterialIndex;
  std::vector<Material> m_materials;

  void Serialize(CArchive &ar);

private:
  bool ObjFileRead(const TCHAR *filename);
  bool MtlFileRead(const TCHAR *filename);
  void getExtents(const Matrix3d &frame, CExtents &extents,
                  const std::vector<int> *selection);
  static bool ExtentsVisitor(CGeomObj *pnode, const Matrix3d &accumMatrix,
                             void *puserData,
                             const std::vector<int> *pnodeFilter = NULL);
};

class CWavefrontGeom : public CGeometry {
public:
  CWavefrontGeom(const TCHAR *name, const std::vector<triangle_t> &faces,
                 const std::vector<trivector_t> &vertex_normals,
                 const std::vector<int> &materials,
                 const std::vector<Vector3d> &face_normals,
                 CWavefrontObj *pparent);

  virtual ~CWavefrontGeom();
  void Draw(eRenderStyle renderStyle);

  __declspec(property(get = GetNumberOfFaces)) size_t NumberOfFaces;
  size_t GetNumberOfFaces() const { return m_faces.size(); }

  __declspec(property(get = GetFace)) triangle_t Face[];
  const triangle_t &GetFace(size_t index) const {
    return (index < m_faces.size()) ? m_faces[index] : m_faces[0];
  }

private:
  void CalculateStatistics();
  CWavefrontObj *m_pParentObj;
  std::vector<triangle_t> m_faces;
  std::vector<int> m_materialIndices;
  std::vector<Vector3d> m_faceNormals;
  std::vector<trivector_t> m_vertexNormals;

private:
  CWavefrontGeom();
};

class CViewObj {
public:
  CMCADView *m_plinkedView;
  CViewObj();
  virtual ~CViewObj();

  Matrix3d m_positionInParent;
  CGeomObj *m_pGeomObjects;
};

#endif // !defined(AFX_GEOMOBJ_H__1D1F0D61_D24F_11D3_941D_D10394075A73__INCLUDED_)
