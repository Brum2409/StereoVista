// MCADview.cpp : implementation of the CMCADView class
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
/// This file contains the definition of the CMCADView CView derived class.
/// see https://docs.microsoft.com/en-us/cpp/mfc/document-view-architecture
///
#include "stdafx.h"
///////////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: mcadview.cpp 19813 2022-11-22 08:02:16Z mbonk $
//

#include "mcaddoc.h"
#include "mcadview.h"
#include "Matrix3d.h"
#include "navlib_viewer.h"

#include "gl/wglext.h"
#include <gl/gl.h>
#include <gl/glu.h>

// c runtime
#include <math.h>

// stdlib
#include <algorithm>
#include <iomanip>
#include <iostream>

#define _PROFILE_AUTOPIVOT 0
#define _TRACE_PICK 0
#define _TRACE_PIVOT 0
#define _PIVOT_BITMAP_TEST 0
#define _TRACE_TIMER 0
#define _TRACE_COV 0
#define _TRACE_HIT 0
#define _TRACE_DEPTH_BITS 0
#define _TRACE_glViewport 1

unsigned char threeto8[8] = {0,         0111 >> 1, 0222 >> 1, 0333 >> 1,
                             0444 >> 1, 0555 >> 1, 0666 >> 1, 0377};

unsigned char twoto8[4] = {0, 0x55, 0xaa, 0xff};

unsigned char oneto8[2] = {0, 255};

static int defaultOverride[13] = {0, 3, 24, 27, 64, 67, 88, 173, 181, 236, 247, 164, 91};

static PALETTEENTRY defaultPalEntry[20] = {
    {0, 0, 0, 0},          {0x80, 0, 0, 0},    {0, 0x80, 0, 0},    {0x80, 0x80, 0, 0},
    {0, 0, 0x80, 0},       {0x80, 0, 0x80, 0}, {0, 0x80, 0x80, 0}, {0xC0, 0xC0, 0xC0, 0},

    {192, 220, 192, 0},    {166, 202, 240, 0}, {255, 251, 240, 0}, {160, 160, 164, 0},

    {0x80, 0x80, 0x80, 0}, {0xFF, 0, 0, 0},    {0, 0xFF, 0, 0},    {0xFF, 0xFF, 0, 0},
    {0, 0, 0xFF, 0},       {0xFF, 0, 0xFF, 0}, {0, 0xFF, 0xFF, 0}, {0xFF, 0xFF, 0xFF, 0}};

// wgl extension functions
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = nullptr;
PFNWGLGETSWAPINTERVALEXTPROC wglGetSwapIntervalEXT = nullptr;
bool WGLExtensionSupported(const char *extension_name);

/////////////////////////////////////////////////////////////////////////////
// CMCADView

IMPLEMENT_DYNCREATE(CMCADView, CView)

BEGIN_MESSAGE_MAP(CMCADView, CView)
//{{AFX_MSG_MAP(CMCADView)
ON_WM_CREATE()
ON_WM_DESTROY()
ON_WM_SIZE()
ON_WM_ERASEBKGND()
ON_WM_LBUTTONDOWN()
ON_WM_MBUTTONDOWN()
ON_WM_KEYDOWN()
ON_WM_KEYUP()
//}}AFX_MSG_MAP
#if APPLICATION_HAS_ANIMATION_LOOP
ON_MESSAGE(WM_FRAMETIMER, &CMCADView::OnFrameTimer)
#endif
ON_COMMAND(ID_PROJECTION_PERSPECTIVE, &CMCADView::OnProjectionPerspective)
ON_COMMAND(ID_PROJECTION_PARALLEL, &CMCADView::OnProjectionParallel)
ON_COMMAND(ID_PROJECTION_2D, &CMCADView::OnProjection2d)
ON_COMMAND(ID_VIEW_SHOWGRID, &CMCADView::OnToggleGrid)
ON_UPDATE_COMMAND_UI(ID_PROJECTION_PERSPECTIVE, &CMCADView::OnUpdateProjectionPerspective)
ON_UPDATE_COMMAND_UI(ID_PROJECTION_PARALLEL, &CMCADView::OnUpdateProjectionParallel)
ON_UPDATE_COMMAND_UI(ID_PROJECTION_2D, &CMCADView::OnUpdateProjection2d)
ON_UPDATE_COMMAND_UI(ID_VIEW_SHOWGRID, &CMCADView::OnUpdateShowGrid)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMCADView construction/destruction

#pragma warning(push)
#pragma warning(disable : 4355)
CMCADView::CMCADView()
    : m_pOldPalette(NULL), m_pDC(NULL), m_hglrc(NULL), m_ControlKeyDepressed(FALSE),
      m_ShiftKeyDepressed(FALSE), m_bRedrawFrustum(false), m_projection(ePerspective),
      m_renderStyle(eSmoothShaded), m_bShowGrid(true) {
}
#pragma warning(pop)

CMCADView::~CMCADView() {
}

/////////////////////////////////////////////////////////////////////////////
// CMCADView drawing

void CMCADView::OnDraw(CDC *pDC) {
  UpdateScene();
}

#ifdef _DEBUG
/////////////////////////////////////////////////////////////////////////////
// CMCADView diagnostics

void CMCADView::AssertValid() const {
  CView::AssertValid();
}

void CMCADView::Dump(CDumpContext &dc) const {
  CView::Dump(dc);
}

CMCADDoc *CMCADView::GetDocument() // non-debug version is inline
{
  return STATIC_DOWNCAST(CMCADDoc, m_pDocument);
}

CMCADDoc const *CMCADView::GetDocument() const {
  return static_cast<CMCADDoc const *>(m_pDocument);
}

#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CMCADView message handlers
BOOL CMCADView::PreCreateWindow(CREATESTRUCT &cs) {
  // An OpenGL window must be created with the following flags and must not
  // include CS_PARENTDC for the class style. Refer to SetPixelFormat
  // documentation in the "Comments" section for further information.
  cs.lpszClass = ::AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_OWNDC, NULL,
                                       (HBRUSH)GetStockObject(BLACK_BRUSH), NULL);
  cs.style |= WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

  return CView::PreCreateWindow(cs);
}

int CMCADView::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CView::OnCreate(lpCreateStruct) == -1)
    return -1;

  m_pMainFrame = dynamic_cast<CMainFrame *>(EnsureParentFrame());

  // Link up the ViewObj in the Doc to this view.
  GetDocument()->Camera->m_plinkedView = this;

  m_pDC = new CClientDC(this);

  ASSERT(m_pDC != NULL);

  if (!SetupPixelFormat(m_pDC->GetSafeHdc()))
    return -1;

  CreateRGBPalette();

  m_frustumNearDistance = 0.01;
  m_frustumRight = 0.005;
  m_frustumLeft = -m_frustumRight;
  m_frustumFarDistance = 1000.;
  m_frustumTop = m_frustumRight * 0.75; // Assume 4:3 aspect for the moment
  m_frustumBottom = -m_frustumTop;

  m_animating = false;
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  m_exitRenderThread = false;
  m_render = false;
  CWinThread *pThread =
      ::AfxBeginThread(RenderThread, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
  if (!::DuplicateHandle(::GetCurrentProcess(), pThread->m_hThread, ::GetCurrentProcess(),
                         &m_renderThread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
    return -1;
  }
  pThread->ResumeThread();
#else
  if (::wglGetCurrentContext() == NULL) {
    // create a rendering context
    HGLRC hglrc = ::wglCreateContext(m_pDC->GetSafeHdc());
    // make it the calling thread's current rendering context
    ::wglMakeCurrent(m_pDC->GetSafeHdc(), hglrc);

    m_hglrc = hglrc;

    InitGL();
  }
#endif

  return 0;
}

void CMCADView::OnInitialUpdate() {
  Matrix3d iso( sqrt(0.5),      0.,           -sqrt(0.5),       0.,
                -sqrt(1. / 6.), sqrt(2. / 3.), -sqrt(1. / 6.),  0.,
                sqrt(1. / 3.),  sqrt(1. / 3.), sqrt(1. / 3.),   0.,
                2.,             2.,             2.,             1.);
  CameraAffine = iso;

  // Get the extents of the model
  CExtents extents = GetDocument()->Model->GetExtents(CameraAffine);

  // Calculate the z position relative to the center of the
  // bounding box
  Vector3d boundingbox = extents.m_maxPt - extents.m_minPt;
  if (boundingbox.Length() < kEpsilon5) {
    Matrix3d worldToCameraTM = CameraAffine.Inverse();
    m_extentsGrid = 10.;
    // use the grid
    Vector3d extentsMax, extentsMin;
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 2; ++j) {
        Vector3d corner(i ? m_extentsGrid : -m_extentsGrid, 0., j ? m_extentsGrid : -m_extentsGrid);
        corner = corner * worldToCameraTM;
        if (i == 0 && j == 0)
          extentsMin = extentsMax = corner;
        for (int k = 0; k < 3; ++k) {
          if (corner.v[k] < extentsMin.v[k])
            extentsMin.v[k] = corner.v[k];
          else if (corner.v[k] > extentsMax.v[k])
            extentsMax.v[k] = corner.v[k];
        }
      }
    }
    boundingbox = extentsMax - extentsMin;
  } else {
    int power = static_cast<int>(log10(boundingbox.Length()) + 1.);
    m_extentsGrid = exp(static_cast<double>(power) * log(10.));
  }
  m_frustumFarDistance = 10. * m_extentsGrid;

  FOV = kDefaultFOV;
  Vector3d zoomCC;
  if (fabs(boundingbox.x / (m_frustumRight - m_frustumLeft)) >
      fabs(boundingbox.y / (m_frustumTop - m_frustumBottom)))
    zoomCC.z = fabs(boundingbox.x / (m_frustumRight - m_frustumLeft)) * m_frustumNearDistance +
               boundingbox.z / 2.;
  else
    zoomCC.z = fabs(boundingbox.y / (m_frustumTop - m_frustumBottom)) * m_frustumNearDistance +
               boundingbox.z / 2.;

  m_frustumOrthoProjectionPlaneDistance = zoomCC.z;

  if (m_frustumFarDistance < zoomCC.z * 100.)
    m_frustumFarDistance = zoomCC.z * 100.;

  // clear the selection list
  m_selection.clear();

  ZoomExtents();

  m_pMainFrame->OnLoadModel();
}

void CMCADView::OnDestroy() {
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  std::unique_lock<std::mutex> lock(m_cv_m);
  m_exitRenderThread = true;
  lock.unlock();
  m_cv.notify_all();

  ::WaitForSingleObject(m_renderThread, INFINITE);
#endif

  HGLRC hglrc = ::wglGetCurrentContext();
  ::wglMakeCurrent(NULL, NULL);

  if (hglrc) {
    ::wglDeleteContext(hglrc);
  }

  if (m_pOldPalette) {
    m_pDC->SelectPalette(m_pOldPalette, FALSE);
  }

  if (m_pDC)
    delete m_pDC;

  CView::OnDestroy();
}

void CMCADView::OnSize(UINT nType, int cx, int cy) {
  CView::OnSize(nType, cx, cy);

  if (cy > 0) {
    { // scope for the guard
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
      std::unique_lock<std::mutex> guard(m_graphics);
#endif

      m_clientRect.right = cx;
      m_clientRect.bottom = cy;
      double aspectRatio =
          static_cast<double>(m_clientRect.bottom) / static_cast<double>(m_clientRect.right);
      m_frustumTop = (m_frustumRight - m_frustumLeft) * aspectRatio / 2.;
      m_frustumBottom = -m_frustumTop;
      m_bRedrawFrustum = true;
    }

    UpdateScene();
  }
}


/////////////////////////////////////////////////////////////////////////////
// GL helper functions
void CMCADView::InitGL() {
  glShadeModel(GL_SMOOTH);          // enables smooth shading
  glClearColor(0.0, 0.0, 0.0, 1.0); // clear color
  glClearDepth(1.0f);               // depth buffer setup
  glEnable(GL_DEPTH_TEST);          // enables depth testing
  glDepthFunc(GL_LEQUAL);           // type of depth test to do
  glEnable(GL_DITHER);              // dither color components

  glHint(GL_PERSPECTIVE_CORRECTION_HINT,
         GL_NICEST);                                 // really nice perspective calculations
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // blend function to use
  glEnable(GL_BLEND);                                // enable blending

  // define materials and lights
  GLfloat mat_specular[] = {.35f, .35f, .35f, 1.f};
  GLfloat mat_shininess[] = {50.0};
  GLfloat light_model_ambient[] = {0.2f, 0.2f, 0.2f, 1.f};
  GLfloat light_model_two_side[] = {1.0};
  GLfloat light_model_local_viewer[] = {1.0};
  GLfloat light0_diffuse[] = {.8f, .8f, .8f, 1.f};
  GLfloat light0_position[] = {0.f, 0.f, 1.f, 1.f};

  glDisable(GL_CULL_FACE); // Disable if we have bad normals (ordering)
  glCullFace(GL_BACK);

  glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);

  glLightModelfv(GL_LIGHT_MODEL_LOCAL_VIEWER, light_model_local_viewer);
  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, light_model_ambient);
  glLightModelfv(GL_LIGHT_MODEL_TWO_SIDE,
                 light_model_two_side); // for .obj files
  glLightfv(GL_LIGHT0, GL_POSITION, light0_position);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_diffuse);
  glLightfv(GL_LIGHT0, GL_AMBIENT, light_model_ambient);

  glDrawBuffer(GL_BACK);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  if (::WGLExtensionSupported("WGL_EXT_swap_control")) {
    // Extension is supported, initialize the pointers.
    ::wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)::wglGetProcAddress("wglSwapIntervalEXT");

    // this is another function from WGL_EXT_swap_control extension
    ::wglGetSwapIntervalEXT =
        (PFNWGLGETSWAPINTERVALEXTPROC)::wglGetProcAddress("wglGetSwapIntervalEXT");

    // synchronize the buffer swap with v-sync.
    ::wglSwapIntervalEXT(1);
  }
}

BOOL CMCADView::SetupPixelFormat(HDC hdc) {
  static PIXELFORMATDESCRIPTOR pfd = {
      sizeof(PIXELFORMATDESCRIPTOR), // size of this pfd
      1,                             // version number
      PFD_DRAW_TO_WINDOW |           // support window
          PFD_SUPPORT_OPENGL |       // support OpenGL
          PFD_DOUBLEBUFFER,          // double buffered
      PFD_TYPE_RGBA,                 // RGBA type
      24,                            // 24-bit color depth
      0,
      0,
      0,
      0,
      0,
      0, // color bits ignored
      0, // no alpha buffer
      0, // shift bit ignored
      0, // no accumulation buffer
      0,
      0,
      0,
      0,              // accum bits ignored
      32,             // 32-bit z-buffer
      0,              // no stencil buffer
      0,              // no auxiliary buffer
      PFD_MAIN_PLANE, // main layer
      0,              // reserved
      0,
      0,
      0 // layer masks ignored
  };
  int pixelformat;

  if ((pixelformat = ::ChoosePixelFormat(hdc, &pfd)) == 0) {
    MessageBox(TEXT("ChoosePixelFormat failed"));
    return FALSE;
  }

  if (SetPixelFormat(hdc, pixelformat, &pfd) == FALSE) {
    MessageBox(TEXT("SetPixelFormat failed"));
    return FALSE;
  }

  return TRUE;
}

unsigned char CMCADView::ComponentFromIndex(int i, UINT nbits, UINT shift) {
  unsigned char val;

  val = (unsigned char)(i >> shift);
  switch (nbits) {
  case 1:
    val &= 0x1;
    return oneto8[val];
  case 2:
    val &= 0x3;
    return twoto8[val];
  case 3:
    val &= 0x7;
    return threeto8[val];

  default:
    return 0;
  }
}

void CMCADView::CreateRGBPalette() {
  PIXELFORMATDESCRIPTOR pfd;
  LOGPALETTE *pPal;
  int n, i;

  n = ::GetPixelFormat(m_pDC->GetSafeHdc());
  ::DescribePixelFormat(m_pDC->GetSafeHdc(), n, sizeof(pfd), &pfd);

  if (pfd.dwFlags & PFD_NEED_PALETTE) {
    n = 1 << pfd.cColorBits;
    pPal = (PLOGPALETTE) new char[sizeof(LOGPALETTE) + n * sizeof(PALETTEENTRY)];

    ASSERT(pPal != NULL);

    pPal->palVersion = 0x300;
    pPal->palNumEntries = n;
    for (i = 0; i < n; i++) {
      pPal->palPalEntry[i].peRed = ComponentFromIndex(i, pfd.cRedBits, pfd.cRedShift);
      pPal->palPalEntry[i].peGreen = ComponentFromIndex(i, pfd.cGreenBits, pfd.cGreenShift);
      pPal->palPalEntry[i].peBlue = ComponentFromIndex(i, pfd.cBlueBits, pfd.cBlueShift);
      pPal->palPalEntry[i].peFlags = 0;
    }

    /* fix up the palette to include the default GDI palette */
    if ((pfd.cColorBits == 8) && (pfd.cRedBits == 3) && (pfd.cRedShift == 0) &&
        (pfd.cGreenBits == 3) && (pfd.cGreenShift == 3) && (pfd.cBlueBits == 2) &&
        (pfd.cBlueShift == 6)) {
      for (i = 1; i <= 12; i++)
        pPal->palPalEntry[defaultOverride[i]] = defaultPalEntry[i];
    }

    m_cPalette.CreatePalette(pPal);
    delete[] pPal;

    m_pOldPalette = m_pDC->SelectPalette(&m_cPalette, FALSE);
    m_pDC->RealizePalette();
  }
}

void CMCADView::DrawGrid() {
  if (m_bShowGrid) {
    float ka[4] = {1.f, 1.f, 1.f, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ka);
    float kd[4] = {.2f, .2f, .2f, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, kd);

    glBegin(GL_LINES);
    glNormal3f(0.f, 1.f, 0.f);
    for (int i = -10; i <= 10; i++) {
      if (i == 0) {
        glColor3f(.6f, .3f, .3f);
      } else {
        glColor3f(.25f, .25f, .25f);
      };
      glVertex3f(static_cast<float>(i) * static_cast<float>(m_extentsGrid / 10.), 0.,
                 -static_cast<float>(m_extentsGrid));
      glVertex3f(static_cast<float>(i) * static_cast<float>(m_extentsGrid / 10.), 0.,
                 static_cast<float>(m_extentsGrid));
      if (i == 0) {
        glColor3f(.3f, .3f, .6f);
      } else {
        glColor3f(.25f, .25f, .25f);
      };
      glVertex3f(-static_cast<float>(m_extentsGrid), 0.,
                 static_cast<float>(i) * static_cast<float>(m_extentsGrid / 10.));
      glVertex3f(static_cast<float>(m_extentsGrid), 0.,
                 static_cast<float>(i) * static_cast<float>(m_extentsGrid / 10.));
    }
    glEnd();
  }
}

void CMCADView::UpdateScene(void) {
  if (m_animating) {
    // Nothing to do, the scene will be updated during the animation.
    return;
  }

  RequestSceneRender();
}

void CMCADView::RequestSceneRender() {
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  std::unique_lock<std::mutex> lock(m_cv_m);
  m_render = true;
  lock.unlock();
  m_cv.notify_all();
#else
#if APPLICATION_HAS_ANIMATION_LOOP
  if (m_animating) {
    // This PeekMessage is required because the posted WM_FRAMETIMER will effectively inhibit any
    // input (hardware) messages, system internal events and WM_TIMER messages from being processed.
    // See the documentation to PeekMessage and the order messages are processed.
    // The following installs a message pump for QS_INPUT, QS_HOTKEY & QS_TIMER messages in the
    // animation loop
    MSG msg;
    while (::PeekMessage(&msg, NULL, NULL, NULL,
                         PM_REMOVE | PM_NOYIELD | ((QS_INPUT | QS_HOTKEY | QS_TIMER) << 16))) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
      // If there are posted messages in the queue not originating from us then let the main pump
      // handle them.
      if ((::GetQueueStatus(QS_POSTMESSAGE) >> 16) & QS_POSTMESSAGE) {
        break;
      }
    }
    PostMessage(WM_FRAMETIMER, 0L, 0L);
  }
#endif
  RenderScene();
#endif
}

void CMCADView::RenderScene(void) {
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  // Lock both the graphics system and the camera
  std::unique_lock<std::mutex> graphics(m_graphics, std::defer_lock);
  std::unique_lock<std::mutex> camera(m_camera, std::defer_lock);
  std::lock(graphics, camera);
#endif
#if TRACE_
  typedef std::chrono::high_resolution_clock clock;
  typedef std::chrono::duration<double, std::milli> milliseconds;
  double startTime =
      std::chrono::duration_cast<milliseconds>(clock::now().time_since_epoch()).count();
  std::stringstream s;
  s << std::setprecision(std::numeric_limits<double>::digits10 + 1);
  s << startTime << " ms: DrawScene() start\n";
  OutputDebugStringA(s.str().c_str());
#endif

  Matrix3d worldToCameraTM = CameraAffine.Inverse();
  if (m_bRedrawFrustum) {
    m_bRedrawFrustum = false;
    ::glViewport(0, 0, m_clientRect.Width(), m_clientRect.Height());
    ::glMatrixMode(GL_PROJECTION);
    ::glLoadIdentity();
    SetFrustum();
  }

#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  // Finished reading the camera values.
  camera.unlock();
#endif

  ::glMatrixMode(GL_MODELVIEW);
  ::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  ::glLoadIdentity();

  // Multiply by the view matrix first (worldToEye)
  ::glMultMatrixd(worldToCameraTM);

  DrawGrid();

  DrawObjects(GetDocument()->Model, m_renderStyle, 0);

  DrawPivot();

  ::glFlush();

  ::glFinish();

  ::SwapBuffers(wglGetCurrentDC());

#if TRACE_
  double frameTime =
      std::chrono::duration_cast<milliseconds>(clock::now().time_since_epoch()).count();
  s.str("");
  s << std::setprecision(std::numeric_limits<double>::digits10 + 1);
  s << frameTime << " ms: Draw Duration : " << frameTime - startTime
    << "  ms, Frame rate : " << frameTime - m_frameTime << " ms\n";
  OutputDebugStringA(s.str().c_str());
  m_frameTime = frameTime;
#endif
}

// Helper class to automatically set and restore OpenGL settings
class OpenGLPivotSettings {
public:
  OpenGLPivotSettings() {
    // Enable blend (use alpha channel to render bitmaps)
    m_BlendEnabled = glIsEnabled(GL_BLEND);
    if (m_BlendEnabled == GL_FALSE)
      glEnable(GL_BLEND);

    // Disable the z-buffer (is that true?)
    glGetBooleanv(GL_DEPTH_WRITEMASK, &m_DepthMask);
    if (m_DepthMask == GL_TRUE)
      glDepthMask(GL_FALSE);
    // Disable depth test
    m_DepthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (m_DepthTestEnabled == GL_TRUE)
      glDisable(GL_DEPTH_TEST);
    // Disable all clipping planes
    for (int i = 0; i < 6; i++) {
      m_ClipPlaneEnabled[i] = glIsEnabled(GL_CLIP_PLANE0 + i);
      if (m_ClipPlaneEnabled[i] == GL_TRUE)
        glDisable(GL_CLIP_PLANE0 + i);
    }
  }

  ~OpenGLPivotSettings() {
    // Restore settings
    for (int i = 0; i < 6; i++) {
      if (m_ClipPlaneEnabled[i] == GL_TRUE)
        glEnable(GL_CLIP_PLANE0 + i);
    }

    if (m_DepthTestEnabled == GL_TRUE)
      glEnable(GL_DEPTH_TEST);

    if (m_DepthMask == GL_TRUE)
      glDepthMask(GL_TRUE);

    if (m_BlendEnabled == GL_FALSE)
      glDisable(GL_BLEND);
  }

private:
  GLboolean m_DepthMask;
  GLboolean m_BlendEnabled;
  GLboolean m_DepthTestEnabled;
  GLboolean m_ClipPlaneEnabled[6];
};

void CMCADView::DrawPivot() {
  if (!m_pivot.IsVisible)
    return;

  const CImage *pimagePivot = &m_pivot.Image;
  Point3d pivotWC = PivotPosition;

  float pixelZoom = 1.f;
  std::unique_ptr<OpenGLPivotSettings> ogls(new OpenGLPivotSettings);

  // Draw the pivot
  glPushMatrix();

  glRasterPos3d(pivotWC.x, pivotWC.y, pivotWC.z);
  if (pimagePivot->GetPitch() > 0) {
    glPixelZoom(pixelZoom, -pixelZoom);
    glBitmap(0, 0, 0, 0, -static_cast<float>(pimagePivot->GetWidth() >> 1),
             static_cast<float>(pimagePivot->GetHeight() >> 1), NULL);
    glDrawPixels(pimagePivot->GetWidth(), pimagePivot->GetHeight(), GL_BGRA_EXT, GL_UNSIGNED_BYTE,
                 pimagePivot->GetPixelAddress(0, 0));
  } else { // bottom up image
    glPixelZoom(pixelZoom, pixelZoom);
    glBitmap(0, 0, 0, 0, -static_cast<float>(pimagePivot->GetWidth() >> 1),
             -static_cast<float>(pimagePivot->GetHeight() >> 1), NULL);
    glDrawPixels(pimagePivot->GetWidth(), pimagePivot->GetHeight(), GL_BGRA_EXT, GL_UNSIGNED_BYTE,
                 pimagePivot->GetPixelAddress(0, pimagePivot->GetHeight() - 1));
  }

  glPopMatrix();

  ogls.reset();
}

// This method draws one level of objects.  I.e., all objects that are siblings.
// If it encounters a node that has children, it recursively calls itself.
void CMCADView::DrawObjects(CGeomObj *pGeomObj, eRenderStyle renderStyle) {
  DrawObjects(pGeomObj, renderStyle, 0);
}

void CMCADView::DrawObjects(CGeomObj *pGeomObj, eRenderStyle renderStyle, int flags) {
  for (; pGeomObj; pGeomObj = pGeomObj->m_pNext.get()) {
    // Draw geometry at this level of the hierarchy tree
    glPushMatrix();
    glMultMatrixd(pGeomObj->m_positionInParent);
    glMultMatrixd(pGeomObj->m_localXformToObj);
    if (pGeomObj->m_pGeometry.get()) {
      if ((flags & HIT_UNSELONLY) && m_selection.size() > 0) {
        if (std::find(m_selection.begin(), m_selection.end(), pGeomObj->m_pGeometry->m_pickName) ==
            m_selection.end())
          pGeomObj->m_pGeometry->Draw(renderStyle);
      } else if ((flags & HIT_SELONLY) && m_selection.size()) {
        if (std::find(m_selection.begin(), m_selection.end(), pGeomObj->m_pGeometry->m_pickName) !=
            m_selection.end())
          pGeomObj->m_pGeometry->Draw(renderStyle);
      } else {
        pGeomObj->m_pGeometry->Draw(renderStyle);
        if (std::find(m_selection.begin(), m_selection.end(), pGeomObj->m_pGeometry->m_pickName) !=
            m_selection.end())
          pGeomObj->m_pGeometry->Draw(eHighlightWireFrame);
      }
    }

    // If there are children, push the matrix then draw them
    // Obviously could run out of room on the gl matrix stack here.
    // Won't worry about it now; it's just a demo.  If we find that
    // we are running out of room, we'll have to do the math (push
    // and pop), at least for the extra levels, ourselves.
    if (pGeomObj->m_pChildren.get())
      DrawObjects(pGeomObj->m_pChildren.get(), renderStyle, flags);
    glPopMatrix();
  }
}

void CMCADView::SetFrustum() {
  double zNear = m_frustumNearDistance;
  double left = m_frustumLeft;
  double right = m_frustumRight;
  double top = m_frustumTop;
  double bottom = m_frustumBottom;
  double zFar = m_frustumFarDistance;

  if (m_projection == ePerspective) {
    // Move the near plane onto where we want the clipping plane to be
    glFrustum(left, right, bottom, top, zNear, zFar);
  } else {
    glOrtho(left * m_frustumOrthoProjectionPlaneDistance / zNear,
            right * m_frustumOrthoProjectionPlaneDistance / zNear,
            bottom * m_frustumOrthoProjectionPlaneDistance / zNear,
            top * m_frustumOrthoProjectionPlaneDistance / zNear, zNear, zFar);
  }
}

void CMCADView::GetProjectionMatrix(double (&matrix)[16]) const {
  double zNear = m_frustumNearDistance;
  double left = m_frustumLeft;
  double right = m_frustumRight;
  double top = m_frustumTop;
  double bottom = m_frustumBottom;
  double zFar = m_frustumFarDistance;

  size_t i;
  for (i = 0; i < 16; ++i)
    matrix[i] = 0.;

  if (m_projection == ePerspective) {
    matrix[0] = 2. * zNear / (right - left);
    matrix[5] = 2. * zNear / (top - bottom);
    matrix[8] = (right + left) / (right - left);
    matrix[9] = (top + bottom) / (top - bottom);
    matrix[10] = -(zFar + zNear) / (zFar - zNear);
    matrix[11] = -1.;
    matrix[14] = -2. * zFar * zNear / (zFar - zNear);
  } else {
    top *= m_frustumOrthoProjectionPlaneDistance / zNear;
    left *= m_frustumOrthoProjectionPlaneDistance / zNear;
    right *= m_frustumOrthoProjectionPlaneDistance / zNear;
    bottom *= m_frustumOrthoProjectionPlaneDistance / zNear;

    matrix[0] = 2. / (right - left);
    matrix[5] = 2. / (top - bottom);
    matrix[10] = -2. / (zFar - zNear);
    matrix[12] = -(right + left) / (right - left);
    matrix[13] = -(top + bottom) / (top - bottom);
    matrix[14] = -(zFar + zNear) / (zFar - zNear);
    matrix[15] = 1.;
  }
}

BOOL CMCADView::OnEraseBkgnd(CDC *pDC) {
  return TRUE;
}

int CMCADView::PickObject(const CPoint &point, const CSize &size, int flags) {
  std::array<GLuint, 256> selectBuffer;
  GLint hits = HittestObjects(selectBuffer, point, size, flags);

  int pickid = 0;
  float z_depth = 1.0;

  if (hits > 0) {
    int names = 0;
    for (int i = 0; i < hits; ++i) {
      float z_min =
          static_cast<float>(selectBuffer[names + i * 3 + 1]) / static_cast<float>(0xffffffff);
#if _TRACE_PICK
      float z_max =
          static_cast<float>(selectBuffer[names + i * 3 + 2]) / static_cast<float>(0xffffffff);
      TRACE("\tname=%d\tz_min=%g\tz_max=%g\n", selectBuffer[names + i * 3 + 3], z_min, z_max);
#endif
      if (z_min < z_depth) {
        z_depth = z_min;
        pickid = selectBuffer[names + i * 3 + 3];
      }
      names += selectBuffer[names + i * 3];
    }
#if _TRACE_PICK
    TRACE("item %d is being selected\n", pickid);
#endif
  }

  return pickid;
}

///////////////////////////////////////////////////////////////////////////////////////
//
// double GetZDepth(const Point3d& position, const Vector3d& direction, double
// radius, int flags)
//
// position   = look from position      in world coordinates
// direction  = direction looking       in world coordinates
// diameter   = aperture diameter       in world coordinates (units)
// flags = 0, HIT_SELONLY, HIT_UNSELONLY
double CMCADView::GetZBufferDepth(const Point3d &position, const Vector3d &direction,
                                  double diameter, int flags) {
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  std::lock_guard<std::mutex> guard(m_graphics);
#endif

  WGLContext wglContext(m_pDC->GetSafeHdc(), m_hglrc);

  // The Size Of The Viewport. [0] Is <x>, [1] Is <y>, [2] Is <width>, [3] Is
  // <height>
  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
#if _TRACE_glViewport
  std::cout << "viewport is (" << viewport[0] << ", " << viewport[1] << ", " << viewport[2] << ", "
            << viewport[3] << ")" << std::endl;
#endif

  // Put OpenGL into render mode. We will draw to the back buffer but not swap
  // it into foreground
  glRenderMode(GL_RENDER);

  glMatrixMode(GL_PROJECTION); // select the project matrix stack
  glPushMatrix();              // push the current projection matrix
  glLoadIdentity();            // reset the projection matrix

  // position the pick point in the middle of the viewport
  CPoint pickPoint((viewport[2] + 1) / 2, (viewport[3] + 1) / 2);

  // Adjust the diameter for how the frustum is calculated in a parallel
  // projection
  if (m_projection != ePerspective)
    diameter *= m_frustumNearDistance / m_frustumOrthoProjectionPlaneDistance;
#if 1
  // make the size of the picking region squarish
  CSize aperture;
  if (viewport[2] > viewport[3]) {
    aperture.cx = 1 + static_cast<int>(diameter * static_cast<double>(viewport[2]) /
                                       (m_frustumRight - m_frustumLeft));
    if (aperture.cx > viewport[3])
      aperture.cy = viewport[3];
    aperture.cy = aperture.cx;
  } else {
    aperture.cy = 1 + static_cast<int>(diameter * static_cast<double>(viewport[3]) /
                                       (m_frustumTop - m_frustumBottom));
    if (aperture.cy > viewport[2])
      aperture.cx = viewport[2];
    aperture.cx = aperture.cy;
  }
#else
  // make the size of the picking region an oblong
  CSize aperture(1 + static_cast<int>(diameter * static_cast<double>(viewport[2]) /
                                      (m_frustumRight - m_frustumLeft)),
                 1 + static_cast<int>(diameter * static_cast<double>(viewport[3]) /
                                      (m_frustumTop - m_frustumBottom)));
#endif

  // Ensure cx and cy are odd so that we know where the central pixel is
  if (!(aperture.cx & 0x01))
    --aperture.cx;
  if (!(aperture.cy & 0x01))
    --aperture.cy;

  // Limit the region
  gluPickMatrix((GLdouble)pickPoint.x, (GLdouble)(pickPoint.y), aperture.cx, aperture.cy, viewport);

  SetFrustum();

  // retrieve the projection matrix for later
  GLdouble projectionMatrix[16];
  glGetDoublev(GL_PROJECTION_MATRIX, projectionMatrix);

  GLdouble projectionMatrix1[16];
  GetProjectionMatrix(projectionMatrix1);

  glMatrixMode(GL_MODELVIEW); // select the model matrix stack
  glPushMatrix();             // push the current modelview matrix
  glLoadIdentity();           // reset the modelview matrix

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);

  GLint depthBits;
  glGetIntegerv(GL_DEPTH_BITS, &depthBits);
#if _TRACE_DEPTH_BITS
  std::cout << "GL_DEPTH_BITS=" << depthBits << std::endl;
#endif
  // Set the color and depth buffers to pre-defined values
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Multiply by the view matrix first (worldToEye)
  Vector3d yAxis, xAxis, zAxis(-direction);

  // Note look direction is the negative z-axis

  if (fabs(zAxis.x) < 1 - kEpsilon5) {
    yAxis = zAxis.CrossProduct(Vector3d(1., 0., 0.));
    xAxis = yAxis.CrossProduct(zAxis);
  } else {
    xAxis = Vector3d(0., 1., 0.).CrossProduct(zAxis);
    yAxis = zAxis.CrossProduct(xAxis);
  }

  // The camera matrix
  Matrix3d cameraToWorldTM(xAxis.x, xAxis.y, xAxis.z, 0,
                           yAxis.x, yAxis.y, yAxis.z, 0,
                           zAxis.x, zAxis.y, zAxis.z, 0,
                           position.x, position.y, position.z, 1);

  // The world to eye matrix
  Matrix3d worldToCameraTM = cameraToWorldTM.Inverse();
  glMultMatrixd(worldToCameraTM);

  // While we are here grab this for the unproject
  GLdouble modelMatrix[16];
  glGetDoublev(GL_MODELVIEW_MATRIX, modelMatrix);

  DrawObjects(GetDocument()->Model, eSmoothShaded, flags);

  glFlush();
  glReadBuffer(GL_BACK);
  glFinish();

  const long bufferSize = aperture.cx * aperture.cy;
  std::unique_ptr<GLfloat[]> depthBuffer(new GLfloat[bufferSize]);

  CPoint readPoint(pickPoint.x - aperture.cx / 2, pickPoint.y - aperture.cy / 2);
  glReadPixels(readPoint.x, readPoint.y, aperture.cx, aperture.cy, GL_DEPTH_COMPONENT, GL_FLOAT,
               depthBuffer.get());

  glMatrixMode(GL_PROJECTION); // select the project matrix stack
  glPopMatrix();               // pop the previous project matrix

  glMatrixMode(GL_MODELVIEW); // select the project matrix stack
  glPopMatrix();              // pop the previous modelview matrix

  glFinish();

  int i, pos = 0;
  GLfloat depth = depthBuffer[pos];

  for (i = 1; i < bufferSize; ++i) {
    if (depthBuffer[i] < depth) {
      pos = i;
      depth = depthBuffer[pos];
    }
  }

  if (depth == 1.)
    return m_frustumFarDistance;

  size_t y = pos / aperture.cx;
  size_t x = pos - y * aperture.cx;

  GLdouble objx, objy, objz;

  gluUnProject(static_cast<GLdouble>(readPoint.x + x), static_cast<GLdouble>(readPoint.y + y),
               static_cast<GLdouble>(depth), modelMatrix, projectionMatrix, viewport, &objx, &objy,
               &objz);

  Point3d hit(static_cast<double>(objx), static_cast<double>(objy), static_cast<double>(objz));

  double hit_distance = (hit - position).DotProduct(direction);

#if _TRACE_HIT
  double distance = (hit - position).Length();
  std::cout << "CMCADView::GetZBufferDepth() depth=" << hit_distance << " point =" << hit
            << " distance=" << distance << std::endl;
  TRACE("CMCADView::GetZBufferDepth gluUnproject hit_distance = %f distance = "
        "%f\n",
        hit_distance, distance);
#endif

  return hit_distance;
}

int CMCADView::HittestObjects(std::array<GLuint, 256> &selectBuffer, const CPoint &point,
                              const CSize &size, int flags) {
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  std::lock_guard<std::mutex> guard(m_graphics);
#endif

  WGLContext wglContext(m_pDC->GetSafeHdc(), m_hglrc);

  // The Size Of The Viewport. [0] Is <x>, [1] Is <y>, [2] Is <width>, [3] Is
  // <height>
  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
#if _TRACE_glViewport
  std::cout << "viewport is (" << viewport[0] << ", " << viewport[1] << ", " << viewport[2] << ", "
            << viewport[3] << ")" << std::endl;
#endif

  glSelectBuffer(static_cast<GLsizei>(selectBuffer.size()), selectBuffer.data());

  // Put OpenGL into selection mode. Nothing will be drawn. Object ID's and
  // extents are stored in the buffer.
  glRenderMode(GL_SELECT);

  glInitNames(); // Initialize the name stack
  glPushName(0); // Push an extry onto the stack

  glMatrixMode(GL_PROJECTION); // select the project matrix stack
  glPushMatrix();              // push the current projection matrix
  glLoadIdentity();            // reset the projection matrix

  gluPickMatrix((GLdouble)point.x, (GLdouble)(viewport[3] - point.y), size.cx, size.cy, viewport);
  SetFrustum();
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();   // push the current modelview matrix
  glLoadIdentity(); // reset the modelview matrix

  // Multiply by the view matrix first (worldToEye)
  Matrix3d worldToCameraTM = CameraAffine.Inverse();
  glMultMatrixd(worldToCameraTM);

  DrawObjects(GetDocument()->Model, eSmoothShaded, flags);

  glFlush();

  GLint hits = glRenderMode(GL_RENDER); // Switch back to render mode which causes the
                                        // the select buffer to be filled
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();

  glMatrixMode(GL_MODELVIEW);
  glPopMatrix(); // pop the original modelview matrix

#if _TRACE_PICK
  if (abs(hits) > 0) {
    TRACE("CMCADView::PickObject %d hits\n", hits);
    int names = 0;
    for (int i = 0; i < abs(hits); ++i) {
      TRACE("\tname=%d\tz_min=%g\tz_max=%g\n", selectBuffer[names + i * 3 + 3],
            static_cast<float>(selectBuffer[names + i * 3 + 1]) / static_cast<float>(0xffffffff),
            static_cast<float>(selectBuffer[names + i * 3 + 2]) / static_cast<float>(0xffffffff));
      names += selectBuffer[names + i * 3];
    }
  } else
    TRACE("\nNothing picked\n");
#endif
  return abs(hits);
}

void CMCADView::OnLButtonDown(UINT nFlags, CPoint point) {
  int pickid = PickObject(point, CSize(3, 3));

  std::vector<int> selection(m_selection);
  if (!(nFlags & MK_CONTROL))
    m_selection.clear();

  if (pickid) {
    std::vector<int>::iterator iterator;
    for (iterator = m_selection.begin(); iterator != m_selection.end(); ++iterator) {
      if (*iterator == pickid) {
        m_selection.erase(iterator);
        return;
      }
    }
    m_selection.push_back(pickid);
  }

  if (selection != m_selection)
    SelectionChanged();

  UpdateScene();
}

void CMCADView::SelectNone() {
  if (m_selection.size()) {
    m_selection.clear();
    SelectionChanged();

    UpdateScene();
  }
}

int CMCADView::PickPivot(Point3d &pivotWC, const CPoint &point, const CSize &size, int flags) {
  // if an object has been picked then use that as the center of rotation
  // pivot is returned in world coordinates
  CMCADDoc *pDoc = GetDocument();
  if (!pDoc)
    return 0;

  GLint viewport[4];
  double frustum_width, frustum_height;

  { // scope for the guard
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
    std::unique_lock<std::mutex> guard(m_graphics);
#endif

    WGLContext wglContext(m_pDC->GetSafeHdc(), m_hglrc);

    glGetIntegerv(GL_VIEWPORT, viewport);
#if _TRACE_glViewport
    std::cout << "viewport is (" << viewport[0] << ", " << viewport[1] << ", " << viewport[2]
              << ", " << viewport[3] << ")" << std::endl;
#endif

    frustum_width = m_frustumRight - m_frustumLeft;
    frustum_height = m_frustumTop - m_frustumBottom;
  }

  double frustum_x =
      static_cast<double>(point.x) / static_cast<double>(viewport[2]) * frustum_width;
  double frustum_y =
      static_cast<double>(point.y) / static_cast<double>(viewport[3]) * frustum_height;

  // Vector from viewport middle to pick point on the near clipping plane
  // (projection plane) in camera coordinates
  Vector3d middle2pickCC(frustum_x - frustum_width / 2., frustum_height / 2. - frustum_y, 0.);

  Matrix3d camera2WorldTM = CameraAffine;
  Vector3d middle2pickWC = middle2pickCC * camera2WorldTM;

  // Calculate the look at direction
  // the cameras z axis is the negative camera look direction
  Vector3d direction(-camera2WorldTM.GetRow(2));
  // vector to the pick point from camera
  direction += middle2pickWC / m_frustumNearDistance;
  direction.Normalize();

  Point3d position(camera2WorldTM.GetPosition());

  double z_depth = GetZBufferDepth(
      position, direction,
      static_cast<double>(size.cx) / static_cast<double>(viewport[2]) * (frustum_width), flags);

  if (z_depth < m_frustumNearDistance + kEpsilon5)
    return 0;

  if (z_depth > m_frustumFarDistance - kEpsilon5)
    return 0;

  // Calculate new center of rotation from the projection
  // of the cursor onto the object surface

  pivotWC = position + direction * z_depth;
#if _TRACE_PIVOT
  pivotWC.Dump("Pivot");
#endif
  return 1;
}

#if APPLICATION_HAS_ANIMATION_LOOP
LRESULT CMCADView::OnFrameTimer(WPARAM, LPARAM) {
  DWORD status = InSendMessageEx(NULL);
  _RPT1(_CRT_WARN, "OnFrameTimer 0x%x\n", status);

  if ((status & (ISMEX_REPLIED | ISMEX_SEND)) == ISMEX_SEND) {
    ::ReplyMessage(0L);
  }

  // Remove any accumulated messages
  MSG msg;
  while (::PeekMessage(&msg, m_hWnd, WM_FRAMETIMER, WM_FRAMETIMER,
                       PM_REMOVE | PM_NOYIELD | PM_QS_POSTMESSAGE)) {
  }

  if (m_animating) {
    m_pMainFrame->FrameTime = std::chrono::high_resolution_clock::now();
  }

  return 0L;
}
#endif


void CMCADView::OnMButtonDown(UINT nFlags, CPoint point) {
  // Set a new center of rotation
  // if an object has been picked then use that as the center of rotation
  // if nothing has been picked then reset the center of rotation to the
  // center of volume of everything
  Point3d pivot;
  bool bUserPivot = (PickPivot(pivot, point) != 0);

  if (bUserPivot) {
    m_pMainFrame->OnManualPivot(pivot);
    PivotPosition = pivot;
  } else {
    m_pMainFrame->ClearManualPivot();
  }
}

void CMCADView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (nChar == VK_SHIFT)
    m_ShiftKeyDepressed = TRUE;
  if (nChar == VK_CONTROL)
    m_ControlKeyDepressed = TRUE;
  CView::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CMCADView::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (nChar == VK_SHIFT)
    m_ShiftKeyDepressed = FALSE;
  if (nChar == VK_CONTROL)
    m_ControlKeyDepressed = FALSE;
  CView::OnKeyUp(nChar, nRepCnt, nFlags);
}

void CMCADView::ZoomExtents(void) {
  Matrix3d cameraToWorldTM = CameraAffine;
  GetZoomExtents(cameraToWorldTM);
  CameraAffine = cameraToWorldTM;
  UpdateScene();
}

void CMCADView::GetZoomExtents(Matrix3d &cameraToWorldTM) {
  // Zoom and correct the horizon
  Matrix3d worldToCameraTM = cameraToWorldTM.Inverse();
  if (fabs(worldToCameraTM.m[1][0]) > kEpsilon5) {
    Vector3d yAxis(0.f, worldToCameraTM.m[1][1], worldToCameraTM.m[1][2]);
    if (yAxis.Length() > kEpsilon5) {
      yAxis.Normalize();
      Vector3d zAxis(worldToCameraTM.m[2][0], worldToCameraTM.m[2][1], worldToCameraTM.m[2][2]);
      Vector3d xAxis = yAxis.CrossProduct(zAxis);
      if (xAxis.Length() < kEpsilon5)
        xAxis = Vector3d(1., 0., 0.);
      else
        xAxis.Normalize();
      zAxis = xAxis.CrossProduct(yAxis);

      worldToCameraTM.m[0][0] = xAxis.x;
      worldToCameraTM.m[0][1] = xAxis.y;
      worldToCameraTM.m[0][2] = xAxis.z;
      worldToCameraTM.m[1][0] = yAxis.x;
      worldToCameraTM.m[1][1] = yAxis.y;
      worldToCameraTM.m[1][2] = yAxis.z;
      worldToCameraTM.m[2][0] = zAxis.x;
      worldToCameraTM.m[2][1] = zAxis.y;
      worldToCameraTM.m[2][2] = zAxis.z;
    } else
      worldToCameraTM.Identity();
  }

  // Get the extents of the model in the view coordinates
  CExtents extents = GetDocument()->Model->GetExtents(cameraToWorldTM, &m_selection);
  Point3d centerOfVolumeCC = extents.Center();

  cameraToWorldTM = worldToCameraTM.Inverse();
  Point3d centerOfVolumeWC = centerOfVolumeCC * cameraToWorldTM;
#if _TRACE_COV
  centerOfVolumeWC.Dump(_T("COV WC"));
#endif

  Matrix3d worldToModelTM = (GetDocument()->Model->m_positionInParent).Inverse();

#if _TRACE_COV
  Point3d centerOfVolumeMC = centerOfVolumeWC * worldToModelTM;
  centerOfVolumeMC.Dump(_T("COV MC"));
#endif

  // Calculate the z position relative to the center of the
  // bounding box
  Vector3d boundingbox = extents.m_maxPt - extents.m_minPt;
  if (boundingbox.Length() < kEpsilon5) {
    // use the grid
    Point3d extentsMax, extentsMin;
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 2; ++j) {
        Point3d corner(i ? m_extentsGrid : -m_extentsGrid, 0., j ? m_extentsGrid : -m_extentsGrid);
        corner = corner * worldToCameraTM;
        if (i == 0 && j == 0) {
          extentsMin = extentsMax = corner;
        }
        for (int k = 0; k < 3; ++k) {
          if (corner.p[k] < extentsMin.p[k])
            extentsMin.p[k] = corner.p[k];
          else if (corner.p[k] > extentsMax.p[k])
            extentsMax.p[k] = corner.p[k];
        }
      }
    }
    boundingbox = extentsMax - extentsMin;
  }

  FOV = kDefaultFOV;
  Vector3d zoomCC;
  if (fabs(boundingbox.x / (m_frustumRight - m_frustumLeft)) >
      fabs(boundingbox.y / (m_frustumTop - m_frustumBottom)))
    zoomCC.z = fabs(boundingbox.x / (m_frustumRight - m_frustumLeft)) * m_frustumNearDistance +
               boundingbox.z / 2.;
  else
    zoomCC.z = fabs(boundingbox.y / (m_frustumTop - m_frustumBottom)) * m_frustumNearDistance +
               boundingbox.z / 2.;

  Vector3d zoomWC = zoomCC * cameraToWorldTM;

  // Move the camera onto the center of volume
  cameraToWorldTM.SetPosition(centerOfVolumeWC);

  // Translate back
  cameraToWorldTM.TranslateBy(zoomWC);
}

void CMCADView::OnProjectionPerspective() {
  Projection = ePerspective;
  FOV = kDefaultFOV;

  m_pMainFrame->OnProjectionChanged();
  UpdateScene();
}

void CMCADView::OnUpdateProjectionPerspective(CCmdUI *pCmdUI) {
  pCmdUI->Enable();
  pCmdUI->SetRadio(m_projection == ePerspective);
}

void CMCADView::OnProjectionParallel() {
  if (Projection == ePerspective) {
    FOV = PerspectiveFOVToParallel();
    CMCADDoc *pDoc = GetDocument();
    if (pDoc) {
      // When we map from a perspective to an orthographic projection we need to
      // decide which plane remains the same size
      Vector3d targetPos =
          pDoc->Model->m_positionInParent.GetPosition() - CameraAffine.GetPosition();
      m_frustumOrthoProjectionPlaneDistance = targetPos.Length();
    }
  }
  Projection = eParallel;

  m_pMainFrame->OnProjectionChanged();

  UpdateScene();
}

double CMCADView::PerspectiveFOVToParallel() {
  return FOV;
}

void CMCADView::OnUpdateProjectionParallel(CCmdUI *pCmdUI) {
// A 3D parallel projection where we zoom dependent on the movement and
// rotations are allowed is only possible in object mode
  pCmdUI->SetRadio(m_projection == eParallel);
}

void CMCADView::OnProjection2d() {
  if (Projection == ePerspective) {
    FOV = PerspectiveFOVToParallel();
    CMCADDoc *pDoc = GetDocument();
    if (pDoc) {
      Vector3d targetPos =
          pDoc->Model->m_positionInParent.GetPosition() - CameraAffine.GetPosition();
      m_frustumOrthoProjectionPlaneDistance = targetPos.Length();
    }
  }

  Projection = e2D;

  m_pMainFrame->OnProjectionChanged();
  UpdateScene();
}

void CMCADView::OnToggleGrid() {
  m_bShowGrid = !m_bShowGrid;
  UpdateScene();
}

void CMCADView::OnUpdateProjection2d(CCmdUI *pCmdUI) {
  pCmdUI->Enable();
  pCmdUI->SetRadio(m_projection == e2D);
}

void CMCADView::OnUpdateShowGrid(CCmdUI *pCmdUI) {
  pCmdUI->Enable();
  pCmdUI->SetCheck(m_bShowGrid);
}

void CMCADView::SelectionChanged() {
  m_pMainFrame->OnSelectionChanged(m_selection.size() > 0);
}

/// <summary>
/// Get the view/camera affine.
/// </summary>
/// <returns>A <see cref="Matrix3d"/>.</returns>
Matrix3d CMCADView::GetCameraAffine(void) const {
  return GetDocument()->Camera->m_positionInParent;
}

/// <summary>
/// Set the view/camera affine.
/// </summary>
/// <param name="value"></param>
void CMCADView::PutCameraAffine(const Matrix3d &value) {
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  std::unique_lock<std::mutex> lock(m_camera);
#endif

  GetDocument()->Camera->m_positionInParent = value;
}

/// <summary>
/// Get the affine of the view defined as the front of the model
/// </summary>
Matrix3d CMCADView::GetFrontAffine() const {
  Matrix3d affine(1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.);

  return affine;
}

/// <summary>
/// Get if the view can be rotated
/// </summary>
bool CMCADView::GetIsRotatable() const {
  return true;
}

void CMCADView::PutPointerPosition(const Point3d &value) {
#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  std::lock_guard<std::mutex> guard(m_graphics);
#endif

  WGLContext wglContext(m_pDC->GetSafeHdc(), m_hglrc);

  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
#if _TRACE_glViewport
  std::cout << "viewport is (" << viewport[0] << ", " << viewport[1] << ", " << viewport[2] << ", "
            << viewport[3] << ")" << std::endl;
#endif

  GLdouble projectionMatrix[16];
  GetProjectionMatrix(projectionMatrix);

  Matrix3d worldToCameraTM = CameraAffine.Inverse();

  GLdouble winx, winy, winz;
  int ok = gluProject(value.x, value.y, value.z, worldToCameraTM,
                      projectionMatrix, viewport, &winx, &winy, &winz);

  if (ok) {
    CPoint pt(static_cast<int>(winx), static_cast<int>(winy));
    ClientToScreen(&pt);
    ::SetCursorPos(pt.x, pt.y);
  }
}

/// <summary>
/// Get the position of the mouse cursor on the projection plane in world
/// coordinates
/// </summary>
Point3d CMCADView::GetPointerPosition() const {
  CPoint point;
  GetCursorPos(&point);

  ScreenToClient(&point);

#if APPLICATION_HAS_EXTRA_RENDER_THREAD
  std::lock_guard<std::mutex> guard(m_graphics);
#endif
  WGLContext wglContext(m_pDC->GetSafeHdc(), m_hglrc);

  // The Size Of The Viewport. [0] Is <x>, [1] Is <y>, [2] Is <width>, [3] Is
  // <height>
  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
#if _TRACE_glViewport
  std::cout << "viewport is (" << viewport[0] << ", " << viewport[1] << ", " << viewport[2] << ", "
            << viewport[3] << ")" << std::endl;
#endif

  GLdouble projectionMatrix[16];
  GetProjectionMatrix(projectionMatrix);

  GLdouble winZ = 0;
  if (m_projection == ePerspective) {
    // Use a point in the view frustum instead of on the near plane to increase precision
    // winZ = f * (n + z) / ((f - n) * z);
    GLdouble z = -(m_frustumNearDistance + (m_frustumFarDistance - m_frustumNearDistance) / 100);
    winZ = m_frustumFarDistance * (m_frustumNearDistance + z) /
           ((m_frustumFarDistance - m_frustumNearDistance) * z);
  }

  Matrix3d worldToCameraTM = CameraAffine.Inverse();

  GLdouble objx, objy, objz;
  gluUnProject(static_cast<GLdouble>(point.x), static_cast<GLdouble>(viewport[3] - point.y), winZ,
               worldToCameraTM, projectionMatrix, viewport, &objx, &objy,
               &objz);

  Point3d position(static_cast<double>(objx), static_cast<double>(objy), static_cast<double>(objz));

  return position;
}

#if APPLICATION_HAS_EXTRA_RENDER_THREAD
UINT CMCADView::RenderThread(LPVOID pParam) {
  CMCADView *pObject = reinterpret_cast<CMCADView *>(pParam);

  if (pObject == nullptr || !pObject->IsKindOf(RUNTIME_CLASS(CMCADView))) {
    return 1; // if pObject is not valid
  }

  pObject->RenderThread();

  return 0; // thread completed successfully
}

UINT CMCADView::RenderThread() {
  if (::wglGetCurrentContext() == NULL) {
    // create a rendering context
    HGLRC hglrc = ::wglCreateContext(m_pDC->GetSafeHdc());
    // make it the calling thread's current rendering context
    ::wglMakeCurrent(m_pDC->GetSafeHdc(), hglrc);

    m_hglrc = hglrc;

    InitGL();
  }

  while (!m_exitRenderThread) {
    std::unique_lock<std::mutex> lock(m_cv_m);
    while (!(m_render || m_exitRenderThread)) {
      m_cv.wait(lock);
    }
    if (!m_exitRenderThread) {
      m_render = false;
      lock.unlock();
#if APPLICATION_HAS_ANIMATION_LOOP
      if (m_animating) {
        SendNotifyMessage(WM_FRAMETIMER, 0L, 0L);
      }
#endif
      RenderScene();
    }
  }
  HGLRC hglrc = wglGetCurrentContext();
  wglMakeCurrent(NULL, NULL);
  wglDeleteContext(hglrc);
  m_hglrc = NULL;

  return 0;
}
#endif // APPLICATION_HAS_EXTRA_RENDER_THREAD

bool WGLExtensionSupported(const char *extension_name) {
  // this is pointer to function which returns pointer to string with list of all wgl extensions
  PFNWGLGETEXTENSIONSSTRINGEXTPROC _wglGetExtensionsStringEXT = nullptr;

  // determine pointer to wglGetExtensionsStringEXT function
  _wglGetExtensionsStringEXT =
      (PFNWGLGETEXTENSIONSSTRINGEXTPROC)wglGetProcAddress("wglGetExtensionsStringEXT");

  if (_wglGetExtensionsStringEXT == nullptr) {
    return false;
  }

  if (strstr(_wglGetExtensionsStringEXT(), extension_name) == NULL) {
    // string was not found
    return false;
  }

  // extension is supported
  return true;
}