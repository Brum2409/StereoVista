// GeomObj.cpp: implementation of the CGeomObj class.
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
/// This file contains the definition of the classes used to hold geometry
/// data.
///
#include "stdafx.h"
///////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: GeomObj.cpp 19960 2023-01-31 13:28:09Z mbonk $
//
#include "GeomObj.h"
#include "Visitor.h"
#include "float.h"
#include "gl/gl.h"
#include "navlib_viewer.h"

// stdlib
#include <algorithm>
#include <cmath>
#include <memory>

// Matrix stack used for walking the tree
static MatrixStack matrixStack;

GLuint CGeometry::m_pickNameCount = 0;

CGeometry::CGeometry() : m_strName("unknown"), m_pickName(++m_pickNameCount) {
}

CBlock::CBlock() {
  m_sx = m_sy = m_sz = 1.0;

  m_fur.v[0] = m_sx / 2;
  m_fur.v[1] = m_sy / 2;
  m_fur.v[2] = m_sz;
  m_flr.v[0] = m_sx / 2;
  m_flr.v[1] = -m_sy / 2;
  m_flr.v[2] = m_sz;
  m_fll.v[0] = -m_sx / 2;
  m_fll.v[1] = -m_sy / 2;
  m_fll.v[2] = m_sz;
  m_ful.v[0] = -m_sx / 2;
  m_ful.v[1] = m_sy / 2;
  m_ful.v[2] = m_sz;
  m_rur.v[0] = m_sx / 2;
  m_rur.v[1] = m_sy / 2;
  m_rur.v[2] = 0;
  m_rlr.v[0] = m_sx / 2;
  m_rlr.v[1] = -m_sy / 2;
  m_rlr.v[2] = 0;
  m_rll.v[0] = -m_sx / 2;
  m_rll.v[1] = -m_sy / 2;
  m_rll.v[2] = 0;
  m_rul.v[0] = -m_sx / 2;
  m_rul.v[1] = m_sy / 2;
  m_rul.v[2] = 0;
}

CBlock::CBlock(double sx, double sy, double sz) {
  m_sx = sx;
  m_sy = sy;
  m_sz = sz;

  m_fur.v[0] = m_sx / 2;
  m_fur.v[1] = m_sy / 2;
  m_fur.v[2] = m_sz;
  m_flr.v[0] = m_sx / 2;
  m_flr.v[1] = -m_sy / 2;
  m_flr.v[2] = m_sz;
  m_fll.v[0] = -m_sx / 2;
  m_fll.v[1] = -m_sy / 2;
  m_fll.v[2] = m_sz;
  m_ful.v[0] = -m_sx / 2;
  m_ful.v[1] = m_sy / 2;
  m_ful.v[2] = m_sz;
  m_rur.v[0] = m_sx / 2;
  m_rur.v[1] = m_sy / 2;
  m_rur.v[2] = 0;
  m_rlr.v[0] = m_sx / 2;
  m_rlr.v[1] = -m_sy / 2;
  m_rlr.v[2] = 0;
  m_rll.v[0] = -m_sx / 2;
  m_rll.v[1] = -m_sy / 2;
  m_rll.v[2] = 0;
  m_rul.v[0] = -m_sx / 2;
  m_rul.v[1] = m_sy / 2;
  m_rul.v[2] = 0;
}

void CBlock::Draw(eRenderStyle renderStyle) {
  // Make OpenGL calls to draw this geometry

  glLoadName(m_pickName); // for OpenGL picking name stack

  // Have to do individual sides to get picking of them

  float red[3] = {1.0, 0.0, 0.0};
  glColor3f(red[0], red[1], red[2]);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, red);

  // Front
  glPushName(1);
  glBegin(GL_QUADS);
  glNormal3f(0.0f, 0.0f, 1.0f);
  glVertex3d(m_ful.v[0], m_ful.v[1], m_ful.v[2]); // ful - front upper left
  glVertex3d(m_fll.v[0], m_fll.v[1], m_fll.v[2]); // fll
  glVertex3d(m_flr.v[0], m_flr.v[1], m_flr.v[2]); // flr
  glVertex3d(m_fur.v[0], m_fur.v[1], m_fur.v[2]); // fur
  glEnd();
  glPopName();

  // Right side
  glPushName(2);
  glBegin(GL_QUADS);
  glNormal3f(1.0f, 0.0f, 0.0f);
  glVertex3d(m_fur.v[0], m_fur.v[1], m_fur.v[2]); // fur
  glVertex3d(m_flr.v[0], m_flr.v[1], m_flr.v[2]); // flr
  glVertex3d(m_rlr.v[0], m_rlr.v[1], m_rlr.v[2]); // rlr
  glVertex3d(m_rur.v[0], m_rur.v[1], m_rur.v[2]); // rur
  glEnd();
  glPopName();

  // Back
  glPushName(3);
  glBegin(GL_QUADS);
  glNormal3f(0.0f, 0.0f, -1.0f);
  glVertex3d(m_rur.v[0], m_rur.v[1], m_rur.v[2]); // rur
  glVertex3d(m_rlr.v[0], m_rlr.v[1], m_rlr.v[2]); // rlr
  glVertex3d(m_rll.v[0], m_rll.v[1], m_rll.v[2]); // rll
  glVertex3d(m_rul.v[0], m_rul.v[1], m_rul.v[2]); // rul
  glEnd();
  glPopName();

  // Left side
  glPushName(4);
  glBegin(GL_QUADS);
  glNormal3f(-1.0f, 0.0f, 0.0f);
  glVertex3d(m_rul.v[0], m_rul.v[1], m_rul.v[2]); // rul
  glVertex3d(m_rll.v[0], m_rll.v[1], m_rll.v[2]); // rll
  glVertex3d(m_fll.v[0], m_fll.v[1], m_fll.v[2]); // fll
  glVertex3d(m_ful.v[0], m_ful.v[1], m_ful.v[2]); // ful
  glEnd();
  glPopName();

  // Top
  glPushName(5);
  glBegin(GL_QUADS);
  glNormal3f(0.0f, 1.0f, 0.0f);
  glVertex3d(m_ful.v[0], m_ful.v[1], m_ful.v[2]); // ful
  glVertex3d(m_fur.v[0], m_fur.v[1], m_fur.v[2]); // fur
  glVertex3d(m_rur.v[0], m_rur.v[1], m_rur.v[2]); // rur
  glVertex3d(m_rul.v[0], m_rul.v[1], m_rul.v[2]); // rul
  glEnd();
  glPopName();

  // Bottom
  glPushName(6);
  glBegin(GL_QUADS);
  glNormal3f(0.0f, -1.0f, 0.0f);
  glVertex3d(m_fll.v[0], m_fll.v[1], m_fll.v[2]); // fll
  glVertex3d(m_flr.v[0], m_flr.v[1], m_flr.v[2]); // flr
  glVertex3d(m_rlr.v[0], m_rlr.v[1], m_rlr.v[2]); // rlr
  glVertex3d(m_rll.v[0], m_rll.v[1], m_rll.v[2]); // rll
  glEnd();
  glPopName();
}

CTriad::CTriad() { m_sx = m_sy = m_sz = 1.0; }

CTriad::CTriad(double sx, double sy, double sz) {
  m_sx = sx;
  m_sy = sy;
  m_sz = sz;
}

void CTriad::Draw(eRenderStyle renderStyle) {
  // Make OpenGL calls to draw this geometry

  glLoadName(m_pickName); // for OpenGL picking name stack

  // Have to do individual sides to get picking of them

  // X
  float col[3] = {1.0, 0.0, 0.0};
  glColor3f(col[0], col[1], col[2]);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, col);

  glPushName(1);
  glBegin(GL_LINES);
  glVertex3d(0, 0, 0);
  glVertex3d(m_sx, 0, 0);
  glEnd();
  glPopName();

  // Y
  col[0] = 0;
  col[1] = 1;
  col[2] = 0;
  glColor3f(col[0], col[1], col[2]);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, col);

  glPushName(2);
  glBegin(GL_LINES);
  glVertex3d(0, 0, 0);
  glVertex3d(0, m_sy, 0);
  glEnd();
  glPopName();

  // Z
  col[0] = 0;
  col[1] = 0;
  col[2] = 1;
  glColor3f(col[0], col[1], col[2]);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, col);

  glPushName(3);
  glBegin(GL_LINES);
  glVertex3d(0, 0, 0);
  glVertex3d(0, 0, m_sz);
  glEnd();
  glPopName();
}

COctahedron::COctahedron() {
  // This is built pointing along the Z axis.  Front is the point pointing
  // down the positive z axis. Back is the point facing in the negative
  // Z direction.
  m_sx = m_sy = m_sz = 1.0;

  m_front.v[0] = 0;
  m_front.v[1] = 0;
  m_front.v[2] = m_sz;
  m_back.v[0] = 0;
  m_back.v[1] = 0;
  m_back.v[2] = 0;
  m_ll.v[0] = -m_sx / 2;
  m_ll.v[1] = -m_sy / 2;
  m_ll.v[2] = m_sz / 2;
  m_lr.v[0] = m_sx / 2;
  m_lr.v[1] = -m_sy / 2;
  m_lr.v[2] = m_sz / 2;
  m_ur.v[0] = m_sx / 2;
  m_ur.v[1] = m_sy / 2;
  m_ur.v[2] = m_sz / 2;
  m_ul.v[0] = -m_sx / 2;
  m_ul.v[1] = m_sy / 2;
  m_ul.v[2] = m_sz / 2;
}

COctahedron::COctahedron(double sx, double sy, double sz) {
  m_sx = sx;
  m_sy = sy;
  m_sz = sz;

  // This is built pointing along the Z axis.  Front is the point pointing
  // down the positive z axis. Back is the point facing in the negative
  // Z direction.
  m_sx = m_sy = m_sz = 1.0;

  m_front.v[0] = 0;
  m_front.v[1] = 0;
  m_front.v[2] = m_sz;
  m_back.v[0] = 0;
  m_back.v[1] = 0;
  m_back.v[2] = 0;
  m_ll.v[0] = -m_sx / 2;
  m_ll.v[1] = -m_sy / 2;
  m_ll.v[2] = m_sz / 2;
  m_lr.v[0] = m_sx / 2;
  m_lr.v[1] = -m_sy / 2;
  m_lr.v[2] = m_sz / 2;
  m_ur.v[0] = m_sx / 2;
  m_ur.v[1] = m_sy / 2;
  m_ur.v[2] = m_sz / 2;
  m_ul.v[0] = -m_sx / 2;
  m_ul.v[1] = m_sy / 2;
  m_ul.v[2] = m_sz / 2;
}

void COctahedron::Draw(eRenderStyle renderStyle) {
  // Make OpenGL calls to draw this geometry

  glLoadName(m_pickName); // for OpenGL picking name stack

  // Have to do individual sides to get picking of them

  float green[3] = {0.0, 1.0, 0.0};
  glColor3f(green[0], green[1], green[2]);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, green);

  // Front top face (CCW vertex winding order)
  glPushName(1);
  glBegin(GL_TRIANGLES);
  glNormal3f(0.0f, 1.0f, 1.0f); // ?
  glVertex3d(m_front.v[0], m_front.v[1], m_front.v[2]);
  glVertex3d(m_ur.v[0], m_ur.v[1], m_ur.v[2]);
  glVertex3d(m_ul.v[0], m_ul.v[1], m_ul.v[2]);
  glEnd();
  glPopName();

  // Front right face
  glPushName(2);
  glBegin(GL_TRIANGLES);
  glNormal3f(1.0f, 0.0f, 1.0f); // ?
  glVertex3d(m_front.v[0], m_front.v[1], m_front.v[2]);
  glVertex3d(m_lr.v[0], m_lr.v[1], m_lr.v[2]);
  glVertex3d(m_ur.v[0], m_ur.v[1], m_ur.v[2]);
  glEnd();
  glPopName();

  // Front bottom face
  glPushName(3);
  glBegin(GL_TRIANGLES);
  glNormal3f(0.0f, -1.0f, 1.0f); // ?
  glVertex3d(m_front.v[0], m_front.v[1], m_front.v[2]);
  glVertex3d(m_lr.v[0], m_lr.v[1], m_lr.v[2]);
  glVertex3d(m_ll.v[0], m_ll.v[1], m_ll.v[2]);
  glEnd();
  glPopName();

  // Front left face
  glPushName(4);
  glBegin(GL_TRIANGLES);
  glNormal3f(-1.0f, 0.0f, 1.0f); // ?
  glVertex3d(m_front.v[0], m_front.v[1], m_front.v[2]);
  glVertex3d(m_ul.v[0], m_ul.v[1], m_ul.v[2]);
  glVertex3d(m_ll.v[0], m_ll.v[1], m_ll.v[2]);
  glEnd();
  glPopName();

  // Back top face (CCW vertex winding order)
  glPushName(5);
  glBegin(GL_TRIANGLES);
  glNormal3f(0.0f, 1.0f, -1.0f); // ?
  glVertex3d(m_back.v[0], m_back.v[1], m_back.v[2]);
  glVertex3d(m_ul.v[0], m_ul.v[1], m_ul.v[2]);
  glVertex3d(m_ur.v[0], m_ur.v[1], m_ur.v[2]);
  glEnd();
  glPopName();

  // Back right face
  glPushName(6);
  glBegin(GL_TRIANGLES);
  glNormal3f(1.0f, 0.0f, -1.0f); // ?
  glVertex3d(m_back.v[0], m_back.v[1], m_back.v[2]);
  glVertex3d(m_ur.v[0], m_ur.v[1], m_ur.v[2]);
  glVertex3d(m_lr.v[0], m_lr.v[1], m_lr.v[2]);
  glEnd();
  glPopName();

  // Back bottom face
  glPushName(7);
  glBegin(GL_TRIANGLES);
  glNormal3f(0.0f, -1.0f, -1.0f); // ?
  glVertex3d(m_back.v[0], m_back.v[1], m_back.v[2]);
  glVertex3d(m_ll.v[0], m_ll.v[1], m_ll.v[2]);
  glVertex3d(m_lr.v[0], m_lr.v[1], m_lr.v[2]);
  glEnd();
  glPopName();

  // Back left face
  glPushName(8);
  glBegin(GL_TRIANGLES);
  glNormal3f(-1.0f, 0.0f, -1.0f); // ?
  glVertex3d(m_back.v[0], m_back.v[1], m_back.v[2]);
  glVertex3d(m_ll.v[0], m_ll.v[1], m_ll.v[2]);
  glVertex3d(m_ul.v[0], m_ul.v[1], m_ul.v[2]);
  glEnd();
  glPopName();
}

CGeomObj::CGeomObj() : m_pParent(NULL), m_constraint(CS_EverythingAllowed) {}

CGeomObj::CGeomObj(CGeometry *pGeom)
    : m_pParent(NULL), m_constraint(CS_EverythingAllowed) {
  m_pGeometry.reset(pGeom);
}

CGeomObj::~CGeomObj() {}

CViewObj::CViewObj() : m_pGeomObjects(NULL) {}

CViewObj::~CViewObj() { m_pGeomObjects = NULL; }

bool GetAffineVisitor(CGeomObj *pnode, const Matrix3d &accumMatrix,
                      void *puserData, const std::vector<int> *pnodeFilter) {
  Matrix3d *pAffineTM = reinterpret_cast<Matrix3d *>(puserData);
  if (!pAffineTM)
    return true;

  CGeometry *pG = dynamic_cast<CGeometry *>(pnode->m_pGeometry.get());
  if (pG) {
    if (pnodeFilter && !pnodeFilter->empty()) {
      if (pG->m_pickName != pnodeFilter->at(0))
        return false;
    }

    *pAffineTM = accumMatrix.Inverse();
    return true;
  }
  return false;
}

Matrix3d CGeomObj::getAffineTM(unsigned int pickName) {
  matrixStack.PushMatrix();
  matrixStack.LoadMatrix(Matrix3d());

  std::vector<int> nodeFilter;
  nodeFilter.push_back(pickName);
  Matrix3d affineTM;

  CVisitor visitor(GetAffineVisitor, &affineTM, &nodeFilter);
  WalkTree(visitor);

  matrixStack.PopMatrix();

  return affineTM;
}

Constraint CGeomObj::GetConstraint() { return m_constraint; }

void CGeomObj::getExtents(const Matrix3d &frame, CExtents &extents,
                          const std::vector<int> *selection) {
  extents.m_maxPt = extents.m_minPt =
      Point3d() * m_positionInParent * frame.Inverse();
}

// Copied from CArchive so that we can read in ascii files
LPSTR ReadString(CArchive &ar, LPSTR lpsz, UINT nMax) {
  // if nMax is negative (such a large number doesn't make sense given today's
  // 2gb address space), then assume it to mean "keep the newline".
  int nStop = (int)nMax < 0 ? -(int)nMax : (int)nMax;
  ASSERT(AfxIsValidAddress(lpsz, (nStop + 1) * sizeof(char)));

  if (lpsz == NULL)
    return NULL;

  UCHAR ch;
  int nRead = 0;

  TRY {
    while (nRead < nStop) {
      ar >> ch;

      // stop and end-of-line (trailing '\n' is ignored)
      if (ch == '\n' || ch == '\r') {
        if (ch == '\r')
          ar >> ch;
        // store the newline when called with negative nMax
        if ((int)nMax != nStop)
          lpsz[nRead++] = ch;
        break;
      }
      lpsz[nRead++] = ch;
    }
  }
  CATCH(CArchiveException, e) {
    if (e && e->m_cause == CArchiveException::endOfFile) {
      //			e->Delete();
      if (nRead == 0)
        return NULL;
    } else {
      THROW_LAST();
    }
  }
  END_CATCH

  lpsz[nRead] = '\0';
  return lpsz;
}

void CGeomObj::SetConstraint(Constraint c) { m_constraint = c; }

CWavefrontGeom::CWavefrontGeom(const TCHAR *name,
                               const std::vector<triangle_t> &triangles,
                               const std::vector<trivector_t> &vertex_normals,
                               const std::vector<int> &materials,
                               const std::vector<Vector3d> &face_normals,
                               CWavefrontObj *pparent) {
  m_strName = name;
  m_pParentObj = pparent;
  m_faces = triangles;
  m_materialIndices = materials;
  m_faceNormals = face_normals;
  m_vertexNormals = vertex_normals;
  CalculateStatistics();
}

CWavefrontGeom::~CWavefrontGeom() {}

void CWavefrontGeom::Draw(eRenderStyle renderStyle) {
  // Make OpenGL calls to draw this geometry

  glLoadName(m_pickName); // for OpenGL picking name stack
  glPushName(m_pickName);
  m_pParentObj->m_currentMaterialIndex = -1;
  if (renderStyle == eBoundingBox) {
    float kd[4] = {1.f, 1.f, 1.f, 1.0f};
    glColor3f(kd[0], kd[1], kd[2]);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, kd);

    glBegin(GL_LINE_LOOP);

    glNormal3d(-sqrt(1. / 3.), -sqrt(1. / 3.), -sqrt(1. / 3.));
    glVertex3d(m_minPt.x, m_minPt.y, m_minPt.z);
    glNormal3d(-sqrt(1. / 3.), -sqrt(1. / 3.), sqrt(1. / 3.));
    glVertex3d(m_minPt.x, m_minPt.y, m_maxPt.z);
    glNormal3d(-sqrt(1. / 3.), sqrt(1. / 3.), sqrt(1. / 3.));
    glVertex3d(m_minPt.x, m_maxPt.y, m_maxPt.z);
    glNormal3d(-sqrt(1. / 3.), sqrt(1. / 3.), -sqrt(1. / 3.));
    glVertex3d(m_minPt.x, m_maxPt.y, m_minPt.z);

    glEnd();

    glBegin(GL_LINE_LOOP);

    glNormal3d(sqrt(1. / 3.), -sqrt(1. / 3.), -sqrt(1. / 3.));
    glVertex3d(m_maxPt.x, m_minPt.y, m_minPt.z);
    glNormal3d(sqrt(1. / 3.), -sqrt(1. / 3.), sqrt(1. / 3.));
    glVertex3d(m_maxPt.x, m_minPt.y, m_maxPt.z);
    glNormal3d(sqrt(1. / 3.), sqrt(1. / 3.), sqrt(1. / 3.));
    glVertex3d(m_maxPt.x, m_maxPt.y, m_maxPt.z);
    glNormal3d(sqrt(1. / 3.), sqrt(1. / 3.), -sqrt(1. / 3.));
    glVertex3d(m_maxPt.x, m_maxPt.y, m_minPt.z);

    glEnd();

    glBegin(GL_LINES);

    glNormal3d(-sqrt(1. / 3.), -sqrt(1. / 3.), -sqrt(1. / 3.));
    glVertex3d(m_minPt.x, m_minPt.y, m_minPt.z);
    glNormal3d(sqrt(1. / 3.), -sqrt(1. / 3.), -sqrt(1. / 3.));
    glVertex3d(m_maxPt.x, m_minPt.y, m_minPt.z);

    glNormal3d(-sqrt(1. / 3.), sqrt(1. / 3.), -sqrt(1. / 3.));
    glVertex3d(m_minPt.x, m_maxPt.y, m_minPt.z);
    glNormal3d(sqrt(1. / 3.), sqrt(1. / 3.), -sqrt(1. / 3.));
    glVertex3d(m_maxPt.x, m_maxPt.y, m_minPt.z);

    glNormal3d(-sqrt(1. / 3.), sqrt(1. / 3.), sqrt(1. / 3.));
    glVertex3d(m_minPt.x, m_maxPt.y, m_maxPt.z);
    glNormal3d(sqrt(1. / 3.), sqrt(1. / 3.), sqrt(1. / 3.));
    glVertex3d(m_maxPt.x, m_maxPt.y, m_maxPt.z);

    glNormal3d(-sqrt(1. / 3.), -sqrt(1. / 3.), sqrt(1. / 3.));
    glVertex3d(m_minPt.x, m_minPt.y, m_maxPt.z);
    glNormal3d(sqrt(1. / 3.), -sqrt(1. / 3.), -sqrt(1. / 3.));
    glVertex3d(m_maxPt.x, m_minPt.y, m_maxPt.z);

    glEnd();
  } else if (renderStyle == eSmoothShaded) {
    for (size_t i = 0; i < m_faces.size(); i++) {
      if (m_pParentObj->m_currentMaterialIndex != m_materialIndices[i]) {
        int materialIndex = m_materialIndices[i];
        Material m;
        m_pParentObj->m_currentMaterialIndex = materialIndex;
        if (static_cast<int>(m_pParentObj->m_materials.size()) > materialIndex)
          m = m_pParentObj->m_materials[materialIndex];

        glColor3f(m.m_Kd[0], m.m_Kd[1], m.m_Kd[2]);
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, m.m_Ka.data());
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, m.m_Kd.data());
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, m.m_Ks.data());
      }

      glBegin(GL_TRIANGLES);
      if (m_vertexNormals.size() > i)
        glNormal3dv(m_vertexNormals[i][0].v);
      else
        glNormal3dv(m_faceNormals[i].v);
      glVertex3dv(m_faces[i][0].p);

      if (m_vertexNormals.size() > i)
        glNormal3dv(m_vertexNormals[i][1].v);
      glVertex3dv(m_faces[i][1].p);

      if (m_vertexNormals.size() > i)
        glNormal3dv(m_vertexNormals[i][2].v);
      glVertex3dv(m_faces[i][2].p);
      glEnd();
    }
  } else {
    for (size_t i = 0; i < m_faces.size(); i++) {
      if (renderStyle == eHighlightWireFrame) {
        float kd[4] = {1.f, 1.f, 1.f, 1.f};
        m_pParentObj->m_currentMaterialIndex = -1;
        glColor3fv(kd);
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, kd);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, kd);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, kd);
      } else if (m_pParentObj->m_currentMaterialIndex != m_materialIndices[i]) {
        int materialIndex = m_materialIndices[i];
        Material m;
        m_pParentObj->m_currentMaterialIndex = materialIndex;
        if (static_cast<int>(m_pParentObj->m_materials.size()) > materialIndex)
          m = m_pParentObj->m_materials[materialIndex];

        glColor3f(m.m_Kd[0], m.m_Kd[1], m.m_Kd[2]);
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, m.m_Ka.data());
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, m.m_Kd.data());
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, m.m_Ks.data());
      }
      glBegin(GL_LINE_LOOP);

      if (m_vertexNormals.size() > i)
        glNormal3dv(m_vertexNormals[i][0].v);
      else
        glNormal3dv(m_faceNormals[i].v);
      glVertex3dv(m_faces[i][0].p);

      if (m_vertexNormals.size() > i)
        glNormal3dv(m_vertexNormals[i][1].v);
      glVertex3dv(m_faces[i][1].p);

      if (m_vertexNormals.size() > i)
        glNormal3dv(m_vertexNormals[i][2].v);
      glVertex3dv(m_faces[i][2].p);

      glEnd();
    }
  }
  glPopName();
}

void CWavefrontGeom::CalculateStatistics() {
  m_sum = Point3d();
  if (m_faces.size()) {
    m_minPt = Point3d(DBL_MAX, DBL_MAX, DBL_MAX);
    m_maxPt = Point3d(-DBL_MAX, -DBL_MAX, -DBL_MAX);
  }

  // Calculate the extents of the geometry
  std::vector<triangle_t>::iterator iterator;
  for (iterator = m_faces.begin(); iterator != m_faces.end(); ++iterator) {
    if ((*iterator)[0].p[0] < m_minPt.p[0])
      m_minPt.p[0] = (*iterator)[0].p[0];
    if ((*iterator)[0].p[0] > m_maxPt.p[0])
      m_maxPt.p[0] = (*iterator)[0].p[0];
    if ((*iterator)[0].p[1] < m_minPt.p[1])
      m_minPt.p[1] = (*iterator)[0].p[1];
    if ((*iterator)[0].p[1] > m_maxPt.p[1])
      m_maxPt.p[1] = (*iterator)[0].p[1];
    if ((*iterator)[0].p[2] < m_minPt.p[2])
      m_minPt.p[2] = (*iterator)[0].p[2];
    if ((*iterator)[0].p[2] > m_maxPt.p[2])
      m_maxPt.p[2] = (*iterator)[0].p[2];

    if ((*iterator)[1].p[0] < m_minPt.p[0])
      m_minPt.p[0] = (*iterator)[1].p[0];
    if ((*iterator)[1].p[0] > m_maxPt.p[0])
      m_maxPt.p[0] = (*iterator)[1].p[0];
    if ((*iterator)[1].p[1] < m_minPt.p[1])
      m_minPt.p[1] = (*iterator)[1].p[1];
    if ((*iterator)[1].p[1] > m_maxPt.p[1])
      m_maxPt.p[1] = (*iterator)[1].p[1];
    if ((*iterator)[1].p[2] < m_minPt.p[2])
      m_minPt.p[2] = (*iterator)[1].p[2];
    if ((*iterator)[1].p[2] > m_maxPt.p[2])
      m_maxPt.p[2] = (*iterator)[1].p[2];

    if ((*iterator)[2].p[0] < m_minPt.p[0])
      m_minPt.p[0] = (*iterator)[2].p[0];
    if ((*iterator)[2].p[0] > m_maxPt.p[0])
      m_maxPt.p[0] = (*iterator)[2].p[0];
    if ((*iterator)[2].p[1] < m_minPt.p[1])
      m_minPt.p[1] = (*iterator)[2].p[1];
    if ((*iterator)[2].p[1] > m_maxPt.p[1])
      m_maxPt.p[1] = (*iterator)[2].p[1];
    if ((*iterator)[2].p[2] < m_minPt.p[2])
      m_minPt.p[2] = (*iterator)[2].p[2];
    if ((*iterator)[2].p[2] > m_maxPt.p[2])
      m_maxPt.p[2] = (*iterator)[2].p[2];

    for (int j = 0; j < 3; j++)
      m_sum += (*iterator)[j];
  }

  m_center = m_minPt + (m_maxPt - m_minPt) / 2.0;
  m_centroid = m_sum / (static_cast<double>(m_faces.size()) * 3.);
}

void CGeomObj::Constrain(Vector3d &rotVec, Vector3d &transVec) {
  if (m_constraint & (CS_RxAllowed | CS_RyAllowed | CS_RzAllowed)) {
    // ROTATIONAL ELEMENT
    transVec.Zero(); // zero out translation

    // ?? Convert up one coordinate system ??
    // rotVec = pDoc->m_pCurrentSelectedObj->m_localXformToObj * rotVec;

    // Only allow one DOF at a time
    rotVec.SingleAxisFilter();

    // Zero out any disabled axes (from the dlg)
    if (!(m_constraint & CS_RxAllowed))
      rotVec.v[0] = 0;
    if (!(m_constraint & CS_RyAllowed))
      rotVec.v[1] = 0;
    if (!(m_constraint & CS_RzAllowed))
      rotVec.v[2] = 0;
  } else if (m_constraint & (CS_TxAllowed | CS_TyAllowed | CS_TzAllowed)) {
    // TRANSLATIONAL ELEMENT
    rotVec.Zero(); // zero out rotation

    // ?? Convert up one coordinate system ??
    // transVec = pDoc->m_pCurrentSelectedObj->m_localXformToObj * transVec;

    // Only allow one DOF at a time
    transVec.SingleAxisFilter();

    // Zero out any disabled axes (from the dlg)
    if (!(m_constraint & CS_TxAllowed))
      transVec.v[0] = 0;
    if (!(m_constraint & CS_TyAllowed))
      transVec.v[1] = 0;
    if (!(m_constraint & CS_TzAllowed))
      transVec.v[2] = 0;
  } else {
    // Nothing allowed, zero it all out
    rotVec.Zero();
    transVec.Zero();
  }
}

bool CGeomObj::WalkTree(CVisitor &visitor) {
  bool retval = false;

  for (CGeomObj *pobj = this; pobj; pobj = pobj->m_pNext.get()) {
    matrixStack.PushMatrix();
    matrixStack.MultMatrix(pobj->m_positionInParent);
    matrixStack.MultMatrix(pobj->m_localXformToObj);

    // Visit this node
    if ((retval = visitor(pobj, matrixStack.TopMatrix())) == true)
      return retval;

    // If there are children, push the matrix then visit them.
    if (pobj->m_pChildren.get()) {
      matrixStack.PushMatrix();
      if ((retval = pobj->m_pChildren->WalkTree(visitor)) == true)
        return retval;
      matrixStack.PopMatrix();
    }
    matrixStack.PopMatrix();
  }
  return retval;
}

bool CGeomObj::VertexPick(CGeomObj *pworldobj, int dcX, int dcY,
                          Matrix3d &worldToDC) {
  return false;
}

int normparse(char *s, int *nnum) {
  int slashcnt;

  slashcnt = 0;
  while (*s) {
    if (*s++ == '/')
      slashcnt++;
    if (slashcnt == 2) {
      *nnum = atoi(s) - 1;
      return 1;
    }
  }
  return 0;
}

int texparse(char *s, int *tnum) {
  int slashcnt;

  slashcnt = 0;
  while (*s) {
    if (*s++ == '/')
      slashcnt++;
    if (slashcnt == 1 && *s != '/') {
      *tnum = atoi(s) - 1;
      return 1;
    }
  }
  return 0;
}

int parse(char *s, char **v) {
  int tokens; /* number of tokens */

  tokens = 0;
  while (1) {
    /* skip spaces or tabs  */
    while ((*s == ' ') || (*s == '\t'))
      s++;
    if (*s && *s != '\n' &&
        *s != '\r') { /* found a token - set ptr and bump counter */
      v[tokens++] = s;
    }
    if (*s == '"') { /* in quoted string */
      v[tokens - 1] += 1;
      s++;
      while (*s != '"' && *s && *s != '\n' && *s != '\r') /* skip over string */
        s++;
      if (*s == '"')
        *s++ = '\0';
    } else /* in ordinary token: skip over it */
      while (*s != ' ' && *s != '\t' && *s && *s != '\n' && *s != '\r')
        s++;
    if (*s == 0 || *s == '\n' ||
        *s == '\r') { /* if at end of input string, leave */
      *s = 0;
      break;
    } else /* else terminate the token string */
      *s++ = 0;
  }
  v[tokens] = 0;
  return tokens;
}

CWavefrontObj::CWavefrontObj() : m_currentMaterialIndex(0) {}

CWavefrontObj::CWavefrontObj(const TCHAR *filename)
    : m_currentMaterialIndex(0) {
  ObjFileRead(filename);
}

CWavefrontObj::~CWavefrontObj() {}

void CWavefrontObj::getExtents(const Matrix3d &frame, CExtents &extents,
                               const std::vector<int> *selection) {
  matrixStack.PushMatrix();
  matrixStack.LoadMatrix(frame.Inverse());
  CVisitor visitor(&CWavefrontObj::ExtentsVisitor, &extents, selection);
  WalkTree(visitor);
  matrixStack.PopMatrix();
}

bool CWavefrontObj::ExtentsVisitor(CGeomObj *pnode, const Matrix3d &accumMatrix,
                                   void *puserData,
                                   const std::vector<int> *pnodeFilter) {
  CExtents *pExtents = reinterpret_cast<CExtents *>(puserData);
  CWavefrontGeom *pG = dynamic_cast<CWavefrontGeom *>(pnode->m_pGeometry.get());
  if (pG) {
    if (pnodeFilter && !pnodeFilter->empty()) {
      if (std::find(pnodeFilter->begin(), pnodeFilter->end(), pG->m_pickName) ==
          pnodeFilter->end())
        return false;
    }
    for (size_t i = 0; i < pG->NumberOfFaces; ++i) {
      triangle_t triangle = pG->Face[i];
      for (int j = 0; j < 3; ++j) {
        Point3d point;
        point = triangle[j] * accumMatrix;
        if (pExtents->m_minPt.x > point.x)
          pExtents->m_minPt.x = point.x;
        else if (pExtents->m_maxPt.x < point.x)
          pExtents->m_maxPt.x = point.x;

        if (pExtents->m_minPt.y > point.y)
          pExtents->m_minPt.y = point.y;
        else if (pExtents->m_maxPt.y < point.y)
          pExtents->m_maxPt.y = point.y;

        if (pExtents->m_minPt.z > point.z)
          pExtents->m_minPt.z = point.z;
        else if (pExtents->m_maxPt.z < point.z)
          pExtents->m_maxPt.z = point.z;
      }
    }
  }
  return false;
}

bool CWavefrontObj::ObjFileRead(const TCHAR *filename) {
  CFile theFile;
  CString strBaseDir(_T(""));
  if (!theFile.Open(filename, CFile::modeRead)) {
    strBaseDir = _T("..\\..\\..\\..\\model\\");
    CString strGeometry(strBaseDir);
    strGeometry += filename;
    if (!theFile.Open(strGeometry.GetString(), CFile::modeRead)) {
      CString strMessage;
      strMessage.FormatMessage(_T("Could not find %1!s!"), filename);
      AfxGetMainWnd()->MessageBox(strMessage.GetString());
      return false;
    }
  }

  CArchive archive(&theFile, CArchive::load);
  Serialize(archive);
  archive.Close();
  theFile.Close();
  return true;
}

void CWavefrontObj::Serialize(CArchive &ar) {
  if (ar.IsLoading()) {
    std::vector<Point3d> vertices;
    std::vector<Vector3d> vertice_normals;
    std::vector<Point3d> texture_vertices;

    std::vector<triangle_t> faces;
    std::vector<trivector_t> triangle_normals;
    std::vector<Vector3d> face_normals;
    std::vector<int> materialIndices;

    CWavefrontObj *pCurrent = this;

    char buf[512];
    try {
      while (ReadString(ar, buf, sizeof(buf)) != NULL) {
        char *token_buf[25];

        size_t tokens = parse(buf, token_buf);
        if (tokens == 0) {
          continue;
        }

        switch (token_buf[0][0]) {
        default:
          break;
        case 'v': {
          char *stop;
          double values[3];
          values[0] = strtod(token_buf[1], &stop);
          values[1] = strtod(token_buf[2], &stop);
          values[2] = strtod(token_buf[3], &stop);
          if (token_buf[0][1] == 'n')
            vertice_normals.push_back(values);
          else {
            // point /= 100.; // Assume file has centimeters
            if (token_buf[0][1] == 't')
              texture_vertices.push_back(values);
            else
              vertices.push_back(values);
          }
        } break;

        case 'f': {
          int vnum;
          int normals = normparse(token_buf[1], &vnum);
          int texverts = texparse(token_buf[1], &vnum);
          std::vector<int> vtxIndex;
          std::vector<int> normIndex;
          std::vector<int> texIndex;
          size_t i;
          for (i = 1; i < tokens; i++) {
            int index;
            vnum = atoi(token_buf[i]) - 1;
            vtxIndex.push_back(vnum);
            if (texverts) {
              texparse(token_buf[i], &index);
              texIndex.push_back(index);
            }
            if (normals) {
              normparse(token_buf[i], &index);
              normIndex.push_back(index);
            }
          }
          if (tokens == 4) // we got a triangle
          {
            triangle_t triangle;
            triangle[0] = vertices[vtxIndex[0]];
            triangle[1] = vertices[vtxIndex[1]];
            triangle[2] = vertices[vtxIndex[2]];

            faces.push_back(triangle);
            materialIndices.push_back(m_currentMaterialIndex);

            // Calculate normal
            Vector3d vec0(triangle[1] - triangle[0]),
                vec1(triangle[2] - triangle[0]);

            Vector3d face_normal = vec0.CrossProduct(vec1);
            face_normal.Normalize();
            face_normals.push_back(face_normal);

            if (normIndex.size()) {
              trivector_t normals;
              normals[0] = vertice_normals[normIndex[0]];
              normals[1] = vertice_normals[normIndex[1]];
              normals[2] = vertice_normals[normIndex[2]];
              triangle_normals.push_back(normals);
            }
          }
        } break;

        case 'g': // facet group
        {
          if (faces.size()) {
            pCurrent->m_pGeometry.reset(
                new CWavefrontGeom(CA2W(token_buf[1]), faces, triangle_normals,
                                   materialIndices, face_normals, this));
          }
          std::unique_ptr<CWavefrontObj> pObj(new CWavefrontObj());
          if (pCurrent == this) {
            m_pChildren = std::move(pObj);
            pCurrent = dynamic_cast<CWavefrontObj *>(m_pChildren.get());
          } else {
            pCurrent->m_pNext = std::move(pObj);
            pCurrent = dynamic_cast<CWavefrontObj *>(pCurrent->m_pNext.get());
          }

          faces.clear();
          triangle_normals.clear();
          face_normals.clear();
          materialIndices.clear();
        } break;

        case 'u': {
          if (strcmp(token_buf[0], "usemtl") == 0) {
            for (size_t i = 0; i < m_materials.size(); i++) {
              if (m_materials[i].m_name.compare(CA2W(token_buf[1])) == 0) {
                m_currentMaterialIndex = static_cast<int>(i);
                break;
              }
            }
          }
        } break;

        case 's': {
        } break;

        case 'm': {
          if (strcmp(token_buf[0], "mtllib") == 0) {
            CString strBaseDir(ar.m_strFileName);
            int index = strBaseDir.ReverseFind('\\');
            if (index == -1)
              index = strBaseDir.ReverseFind('/');
            strBaseDir = strBaseDir.Left(index + 1);

            MtlFileRead(strBaseDir + token_buf[1]);
          }
        } break;
        }
      }

      // Facets leftover
      if (faces.size()) {
        pCurrent->m_pGeometry.reset(
            new CWavefrontGeom(TEXT("mcad_viewer"), faces, triangle_normals,
                               materialIndices, face_normals, this));

        std::unique_ptr<CWavefrontObj> pObj(new CWavefrontObj());
        if (pCurrent == this) {
          m_pChildren = std::move(pObj);
          pCurrent = dynamic_cast<CWavefrontObj *>(m_pChildren.get());
        } else {
          pCurrent->m_pNext = std::move(pObj);
          pCurrent = dynamic_cast<CWavefrontObj *>(pCurrent->m_pNext.get());
        }
        faces.clear();
        triangle_normals.clear();
        face_normals.clear();
        materialIndices.clear();
      }
    } catch (CException *e) {
      e->Delete();
    }
  }

  // Get the extents of the model and position it onto the y=0 plane
  CExtents extents = GetExtents(Matrix3d());
  Vector3d offset = extents.Center() - Point3d();
  offset.y = extents.m_minPt.y;
  m_positionInParent.TranslateBy(-offset);
}

bool CWavefrontObj::MtlFileRead(const TCHAR *filename) {
  FILE *fp = NULL;
  if ((_tfopen_s(&fp, filename, _T("r"))) != 0) {
    CString strMessage;
    strMessage.FormatMessage(_T("Could not find %1!s!"), filename);
    AfxGetMainWnd()->MessageBox(strMessage.GetString());
    return 0;
  }

  char buf[512];
  while (fgets(buf, sizeof(buf), fp) != NULL) {
    char *token_buf[25];

    if (parse(buf, token_buf) == 0) {
      continue;
    }

    switch (token_buf[0][0]) {
    case 'n':
      if (strcmp(token_buf[0], "newmtl") == 0) {
        Material m;
        m.m_name = CA2W(token_buf[1]);
        m_materials.push_back(m);
      }
      break;

    case 'K':
      if (token_buf[0][1] == 'd') {
        if (m_materials.size() > 0) {
          char *stop;
          size_t index = m_materials.size() - 1;
          m_materials[index].m_Kd[0] =
              static_cast<float>(strtod(token_buf[1], &stop));
          m_materials[index].m_Kd[1] =
              static_cast<float>(strtod(token_buf[2], &stop));
          m_materials[index].m_Kd[2] =
              static_cast<float>(strtod(token_buf[3], &stop));
          m_materials[index].m_Kd[3] = 1.f;
        }
      } else if (token_buf[0][1] == 'a') {
        if (m_materials.size() > 0) {
          char *stop;
          size_t index = m_materials.size() - 1;
          m_materials[index].m_Ka[0] =
              static_cast<float>(strtod(token_buf[1], &stop));
          m_materials[index].m_Ka[1] =
              static_cast<float>(strtod(token_buf[2], &stop));
          m_materials[index].m_Ka[2] =
              static_cast<float>(strtod(token_buf[3], &stop));
          m_materials[index].m_Ka[3] = 1.f;
        }
      } else if (token_buf[0][1] == 's') {
        if (m_materials.size() > 0) {
          char *stop;
          size_t index = m_materials.size() - 1;
          m_materials[index].m_Ks[0] =
              static_cast<float>(strtod(token_buf[1], &stop));
          m_materials[index].m_Ks[1] =
              static_cast<float>(strtod(token_buf[2], &stop));
          m_materials[index].m_Ks[2] =
              static_cast<float>(strtod(token_buf[3], &stop));
          m_materials[index].m_Ks[3] = 1.f;
        }
      }
      break;

    case 'N':
      if (token_buf[0][1] == 's') {
        if (m_materials.size() > 0) {
          size_t index = m_materials.size() - 1;
          m_materials[index].m_Ns = atoi(token_buf[1]);
        }
      }
      break;

    case 'i':
      if (strcmp(token_buf[0], "illum") == 0) {
        if (m_materials.size() > 0) {
          size_t index = m_materials.size() - 1;
          m_materials[index].m_illum = atoi(token_buf[1]);
        }
      }
      break;

    default:
      break;
    }
  }

  fclose(fp);

  return true;
}