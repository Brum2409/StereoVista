#ifndef mcadview_HPP_INCLUDED_
#define mcadview_HPP_INCLUDED_
// mcadview.hpp : interface of the CMCADView class
/*
 * Copyright (c) 2010-2020 3Dconnexion. All rights reserved.
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
/// This file contains the declaration of the CMCADView CView derived class.
/// see https://docs.microsoft.com/en-us/cpp/mfc/document-view-architecture
///
///////////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: mcadview.h 20977 2024-06-21 08:55:48Z mbonk $
//

#include "mainfrm.h"
#include "Matrix3d.h" // Added by ClassView
#include "gl/gl.h"

// navlib
#include <navlib/navlib.h>
#include <navlib/navlib_defines.h>

// atl
#include <atlimage.h>

// stdlib
#include <array>
#include <cmath>
#include <mutex>
#include <vector>

// Forward declarations
class CMCADDoc;
class CGeomObj;
class CMainFrame;
enum eRenderStyle;

enum eProjection { ePerspective, eParallel, e2D };

/* SceneAndNodeHitTestFlags Scene and Node Hit Testing Flags
The following describes hit testing flags that can be sent to the node and
scene hit testing methods */
// Hit test selected items only.
#define HIT_SELONLY (1 << 0)
// Hit test unselected items only.
#define HIT_UNSELONLY (1 << 2)

#define WM_FRAMETIMER WM_USER + 01
class CMCADView : public CView {
protected: // create from serialization only
  CMCADView();
  DECLARE_DYNCREATE(CMCADView)

  // Attributes
public:
  CMCADDoc *GetDocument();
  CMCADDoc const *GetDocument() const;
  CPalette m_cPalette;
  CPalette *m_pOldPalette;

private:
  CRect m_clientRect;
  CClientDC *m_pDC;
  HGLRC m_hglrc;

  // Operations
public:
  void GetZoomExtents(Matrix3d &cameraToWorldTM);
  void ZoomExtents(void);
  void SelectNone(void);

  // Overrides
  // ClassWizard generated virtual function overrides
  //{{AFX_VIRTUAL(CMCADView)
protected:
  virtual void OnDraw(CDC *pDC); // overridden to draw this view
  virtual void OnInitialUpdate();
  //}}AFX_VIRTUAL

  // Implementation
private:
  bool m_ControlKeyDepressed;
  bool m_ShiftKeyDepressed;

  virtual ~CMCADView();
  virtual BOOL PreCreateWindow(CREATESTRUCT &cs);
#ifdef _DEBUG
  virtual void AssertValid() const;
  virtual void Dump(CDumpContext &dc) const;
#endif

  CMainFrame *m_pMainFrame;

  /////////////////////////////////////////////////////////////////////////////////////////
  // Navlib properties
private:
  // Glorified structures
  struct Pivot {
  public:
    Pivot() : Position(0, 0, 0), IsVisible(false) {
      Image.LoadFromResource(AfxGetResourceHandle(), NAVLIB_IDB_AutoPivot);
    }
    ATL::CImage Image;
    Point3d Position;
    bool IsVisible;
  };

  Pivot m_pivot;

  struct HitTest {
    Point3d LookingAt;
    Point3d LookFrom;
    Vector3d Direction;
    double Aperture;
    bool SelectionOnly;
  };

  HitTest m_hitTest;

public:
  __declspec(property(get = GetCameraAffine, put = PutCameraAffine)) Matrix3d CameraAffine;
  Matrix3d GetCameraAffine(void) const;
  void PutCameraAffine(const Matrix3d &value);

  __declspec(property(get = GetFrontAffine)) Matrix3d FrontAffine;
  Matrix3d GetFrontAffine(void) const;

  __declspec(property(get = GetIsRotatable)) bool IsRotatable;
  bool GetIsRotatable() const;

  __declspec(property(get = GetPointerPosition, put = PutPointerPosition)) Point3d PointerPosition;
  void PutPointerPosition(const Point3d &value);
  Point3d GetPointerPosition() const;

  __declspec(property(get = GetLookPosition, put = PutHitFromPosition)) Point3d LookPosition;
  void PutHitFromPosition(const Point3d &value) {
    m_hitTest.LookFrom = value;
  }
  const Point3d &GetLookPosition() const {
    return m_hitTest.LookFrom;
  }

  __declspec(property(get = GetLookDirection, put = PutHitDirection)) Vector3d LookDirection;
  void PutHitDirection(const Vector3d &value) {
    m_hitTest.Direction = value;
  }
  const Vector3d &GetLookDirection() const {
    return m_hitTest.Direction;
  }

  __declspec(property(get = GetAperture, put = PutAperture)) double LookAperture;
  void PutAperture(double value) {
    m_hitTest.Aperture = value;
  }
  double GetAperture() const {
    return m_hitTest.Aperture;
  }

  __declspec(property(get = GetLookingAt, put = PutLookingAt)) Point3d LookingAt;
  void PutLookingAt(const Point3d &value) {
    m_hitTest.LookingAt = value;
  }
  const Point3d &GetLookingAt() {
    double distance = GetZBufferDepth(m_hitTest.LookFrom, m_hitTest.Direction, m_hitTest.Aperture,
                                      m_hitTest.SelectionOnly ? HIT_SELONLY : 0);

    if (distance < m_frustumFarDistance - kEpsilon5)
      m_hitTest.LookingAt = m_hitTest.LookFrom + m_hitTest.Direction * distance;
    else
      return m_hitTest.LookFrom;
    return m_hitTest.LookingAt;
  }

  __declspec(property(get = GetHitSelectionOnly, put = PutHitSelectionOnly)) bool HitSelectionOnly;
  void PutHitSelectionOnly(bool value) {
    m_hitTest.SelectionOnly = value;
  }
  bool GetHitSelectionOnly() {
    return m_hitTest.SelectionOnly;
  }

  __declspec(property(get = GetPivotVisible, put = PutPivotVisible)) bool PivotVisible;
  void PutPivotVisible(bool value) {
    m_pivot.IsVisible = value;
    UpdateScene();
  }
  bool GetPivotVisible() const {
    return m_pivot.IsVisible;
  }

  __declspec(property(get = GetPivotPosition, put = PutPivotPosition)) Point3d PivotPosition;
  void PutPivotPosition(const Point3d &value) {
    m_pivot.Position = value;
    if (m_pivot.IsVisible) {
      UpdateScene();
    }
  }
  const Point3d &GetPivotPosition() const {
    return m_pivot.Position;
  }

  __declspec(property(get = GetIsMoving, put = PutIsMoving)) bool IsMoving;
  void PutIsMoving(bool value) {
    if (m_animating != value) {
      m_animating = value;
#if APPLICATION_HAS_ANIMATION_LOOP
      if (value) {
        RequestSceneRender();
      }
#endif
    }
  }

  bool GetIsMoving() {
    return m_animating;
  }

  __declspec(property(put = PutTransaction)) int Transaction;
  void PutTransaction(int transaction) {
    if (transaction == 0) {
      RequestSceneRender();
    }
  }

  __declspec(property(get = GetFOV, put = PutFOV)) double FOV;
  double GetFOV() const {
    return 2. * atan((m_frustumRight - m_frustumLeft) / 2. / m_frustumNearDistance);
  }

  void PutFOV(double fov) {
    if (m_projection != ePerspective)
      return;

#if APPLICATION_HAS_EXTRA_RENDER_THREAD
    std::unique_lock<std::mutex> camera(m_camera);
#endif

    m_frustumRight = m_frustumNearDistance * tan(fov / 2.);
    m_frustumLeft = -m_frustumRight;
    double aspectRatio =
        static_cast<double>(m_clientRect.bottom) / static_cast<double>(m_clientRect.right);
    m_frustumTop = (m_frustumRight - m_frustumLeft) * aspectRatio / 2.;
    m_frustumBottom = -m_frustumTop;
    m_bRedrawFrustum = true;
  }

  typedef navlib::box_t box_t;
  __declspec(property(get = GetViewExtents, put = PutViewExtents)) box_t ViewExtents;
  box_t GetViewExtents() const {
    double scale = m_frustumOrthoProjectionPlaneDistance / m_frustumNearDistance;

    box_t extents = {{m_frustumLeft * scale, m_frustumBottom * scale, -m_frustumFarDistance},
                     {m_frustumRight * scale, m_frustumTop * scale, m_frustumFarDistance}};

    return extents;
  }
  void PutViewExtents(const box_t &value) {
    if (m_projection == ePerspective)
      return;

    double scale = m_frustumOrthoProjectionPlaneDistance / m_frustumNearDistance;

#if APPLICATION_HAS_EXTRA_RENDER_THREAD
    std::unique_lock<std::mutex> camera(m_camera);
#endif
    m_frustumLeft = value.min.x / scale;
    m_frustumBottom = value.min.y / scale;
    m_frustumRight = value.max.x / scale;
    m_frustumTop = value.max.y / scale;
    m_bRedrawFrustum = true;
  }

  typedef navlib::frustum_t frustum_t;
  __declspec(property(get = GetFrustum)) frustum_t Frustum;
  frustum_t GetFrustum() {
    frustum_t frustum = {m_frustumLeft, m_frustumRight, m_frustumBottom, m_frustumTop,
                           m_frustumNearDistance, m_frustumFarDistance};

    return frustum;
  }

  __declspec(property(get = GetProjection, put = PutProjection)) eProjection Projection;
  eProjection GetProjection() {
    return m_projection;
  }
  void PutProjection(eProjection projection) {
    if (m_projection != projection) {
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
      std::unique_lock<std::mutex> camera(m_camera);
#endif
      m_bRedrawFrustum = true;
      m_projection = projection;
    }
  }

  __declspec(property(get = GetSelection)) std::vector<int> &Selection;
  std::vector<int> &GetSelection() {
    return m_selection;
  }

private:
  unsigned char ComponentFromIndex(int i, UINT nbits, UINT shift);
  void CreateRGBPalette(void);
  bool m_animating;
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  mutable std::mutex m_graphics;
  std::mutex m_camera;
  std::mutex m_cv_m;
  std::condition_variable m_cv;
  bool m_render;
  bool m_exitRenderThread;
  HANDLE m_renderThread;
  static UINT RenderThread(LPVOID);
  UINT RenderThread();
#endif
  void UpdateScene();

  void RequestSceneRender();
  void RenderScene();
  void DrawObjects(CGeomObj *pGeomObj, eRenderStyle renderStyle);
  void DrawObjects(CGeomObj *pGeomObj, eRenderStyle renderStyle, int flags);
  void DrawGrid();
  void DrawPivot();
  void GetProjectionMatrix(double (&maxtrix)[16]) const;
  void SetFrustum();
  void InitGL();
  BOOL SetupPixelFormat(HDC hdc);

  double PerspectiveFOVToParallel();

  int PickObject(const CPoint &point, const CSize &size = CSize(1, 1), int flags = 0);
  int HittestObjects(std::array<GLuint, 256> &selectBuffer, const CPoint &point, const CSize &size,
                     int flags);

  int PickPivot(Point3d &pivot, const CPoint &point, const CSize &size = CSize(1, 1),
                int flags = 0);
  double GetZBufferDepth(const Point3d &position, const Vector3d &direction, double diameter,
                         int flags);

  GLdouble m_frustumLeft, m_frustumRight, m_frustumBottom, m_frustumTop;
  GLdouble m_frustumNearDistance, m_frustumFarDistance;
  double m_frustumOrthoProjectionPlaneDistance;
  bool m_bRedrawFrustum;
  GLdouble m_extentsGrid;
  eProjection m_projection;
  eRenderStyle m_renderStyle;
  bool m_bShowGrid;

  std::vector<int> m_selection;
  void SelectionChanged();

#if TRACE_
  double m_frameTime = 0.0;
#endif

  // Generated message map functions
protected:
  //{{AFX_MSG(CMCADView)
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnDestroy();
  afx_msg BOOL OnEraseBkgnd(CDC *pDC);
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
#if APPLICATION_HAS_ANIMATION_LOOP
  afx_msg LRESULT OnFrameTimer(WPARAM, LPARAM);
#endif
  afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnSize(UINT nType, int cx, int cy);

  //}}AFX_MSG
  DECLARE_MESSAGE_MAP()
public:
  afx_msg void OnProjectionPerspective();
  afx_msg void OnProjectionParallel();
  afx_msg void OnProjection2d();
  afx_msg void OnToggleGrid();
  afx_msg void OnUpdateProjectionPerspective(CCmdUI *pCmdUI);
  afx_msg void OnUpdateProjectionParallel(CCmdUI *pCmdUI);
  afx_msg void OnUpdateProjection2d(CCmdUI *pCmdUI);
  afx_msg void OnUpdateShowGrid(CCmdUI *pCmdUI);
};

class WGLContext {
public:
  WGLContext(HDC hdc, HGLRC hglrc) : m_deleteCtx(wglGetCurrentContext() ? false : true) {
    if (m_deleteCtx) {
      // create a rendering context
      HGLRC _hglrc = wglCreateContext(hdc);

      if (hglrc) {
        // shared the display lists
        wglShareLists(_hglrc, hglrc);
      }

      // make it the calling thread's current rendering context
      wglMakeCurrent(hdc, _hglrc);
    }
  }

  virtual ~WGLContext() {
    clear();
  }

  void clear() {
    if (m_deleteCtx) {
      m_deleteCtx = false;
      HGLRC hglrc = wglGetCurrentContext();
      wglMakeCurrent(NULL, NULL);
      if (hglrc) {
        wglDeleteContext(hglrc);
      }
    }
  }

private:
  WGLContext(const WGLContext &);
  const WGLContext &operator=(const WGLContext &);

  bool m_deleteCtx;
};

#ifndef _DEBUG // debug version in MCADview.cpp
inline CMCADDoc *CMCADView::GetDocument() {
  return (CMCADDoc *)m_pDocument;
}

inline CMCADDoc const *CMCADView::GetDocument() const {
  return static_cast<CMCADDoc const *>(m_pDocument);
}

#endif

#include <cmath>
static const double kDefaultFOV = 2 * atan(31.3364 / (2. * 50.));

/////////////////////////////////////////////////////////////////////////////
#endif // mcadview_HPP_INCLUDED_
