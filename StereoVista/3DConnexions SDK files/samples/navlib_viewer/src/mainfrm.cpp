// mainfrm.cpp : implementation of the CMainFrame class
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
/// This file contains the definition of the CMainFrame class, see
/// https://docs.microsoft.com/en-us/cpp/mfc/frame-windows.
/// The class implements the accessors and mutators for the navlib as well as
/// opening the connection and exporting the application commands. Definitions
/// for application commands are also provided.
///
#include "stdafx.h"
///////////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: mainfrm.cpp 20917 2024-05-07 06:57:41Z mbonk $
//

#include "capplicationcommand.hpp"
#include "mainfrm.h"
#include "mcaddoc.h"
#include "mcadview.h"
#include "navlib_viewer.h"

// stdlib
#include <cmath>
#include <errno.h>
#include <map>
#include <memory>
#include <string>

#define _TRACE_ONIDLE 0
#define _TRACE_PIVOT 0
#define _TRACE_SYNC_TIME 0

const wchar_t *strProductName = L"3Dconnexion Viewer";

/////////////////////////////////////////////////////////////////////////////
// CMainFrame
IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
//{{AFX_MSG_MAP(CMainFrame)
ON_WM_CREATE()
ON_WM_PALETTECHANGED()
ON_WM_QUERYNEWPALETTE()
ON_WM_ACTIVATEAPP()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMainFrame construction/destruction

CMainFrame::CMainFrame()
    // The demo is single threaded and uses row vectors which is the same as OpenGL.
    : CNavigation3D(), m_isUserPivot(false) {
}

CMainFrame::~CMainFrame() {
}

void CMainFrame::OnActivateApp(BOOL bActive, DWORD dwThreadID) {
  __super::OnActivateApp(bActive, dwThreadID);

#ifdef WAMP_CLIENT
  // Notify the navlib that the application now has focus.
  nav3d::Write(navlib::focus_k, bActive != FALSE);
#endif
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
    return -1;

  if (!Enable3DNavigation()) {
    return -1;
  }

#ifdef WAMP_CLIENT
  // If the sample has already been activated.
  nav3d::Write(navlib::focus_k, IsTopParentActive() != FALSE);
#endif

  return 0;
}

void CMainFrame::OnAppExit(void) {
  Disable3DNavigation();

  CFrameWnd::OnClose();
}

/////////////////////////////////////////////////////////////////////////////
// CMainFrame diagnostics

#ifdef _DEBUG
void CMainFrame::AssertValid() const {
  CFrameWnd::AssertValid();
}

void CMainFrame::Dump(CDumpContext &dc) const {
  CFrameWnd::Dump(dc);
}

#endif //__DEBUG

/////////////////////////////////////////////////////////////////////////////
// CMainFrame message handlers
void CMainFrame::OnPaletteChanged(CWnd *pFocusWnd) {
  CFrameWnd::OnPaletteChanged(pFocusWnd);

  if (pFocusWnd != this)
    OnQueryNewPalette();
}

BOOL CMainFrame::OnQueryNewPalette() {
  WORD i;
  CPalette *pOldPal;
  CMCADView *pView = (CMCADView *)GetActiveView();
  CClientDC dc(pView);

  pOldPal = dc.SelectPalette(&pView->m_cPalette, FALSE);
  i = static_cast<WORD>(dc.RealizePalette());
  dc.SelectPalette(pOldPal, FALSE);

  if (i > 0)
    InvalidateRect(NULL);

  return CFrameWnd::OnQueryNewPalette();
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT &cs) {
  // remove this flag to remove " - Untitled" from the frame's caption

  cs.style &= ~FWS_ADDTOTITLE;

  return CFrameWnd::PreCreateWindow(cs);
}

void CMainFrame::OpenFile_Function(CMainFrame *pthis) {
  CS3DMApp *pApp = dynamic_cast<CS3DMApp *>(AfxGetApp());
  if (pApp)
    pApp->OnFileOpen();
}

void CMainFrame::Perspective_Function(CMainFrame *pthis) {
  if (pthis) {
    CMCADView *pView = dynamic_cast<CMCADView *>(pthis->GetActiveView());
    if (pView)
      pView->OnProjectionPerspective();
  }
}

void CMainFrame::Parallel_Function(CMainFrame *pthis) {
  if (pthis) {
    CMCADView *pView = dynamic_cast<CMCADView *>(pthis->GetActiveView());
    if (pView)
      pView->OnProjectionParallel();
  }
}

void CMainFrame::_2D_Function(CMainFrame *pthis) {
  if (pthis) {
    CMCADView *pView = dynamic_cast<CMCADView *>(pthis->GetActiveView());
    if (pView)
      pView->OnProjection2d();
  }
}

void CMainFrame::ToggleGrid_Function(CMainFrame *pthis) {
  if (pthis) {
    CMCADView *pView = dynamic_cast<CMCADView *>(pthis->GetActiveView());
    if (pView)
      pView->OnToggleGrid();
  }
}

void CMainFrame::SelectNone_Function(CMainFrame *pthis) {
  if (pthis) {
    CMCADView *pView = dynamic_cast<CMCADView *>(pthis->GetActiveView());
    if (pView)
      pView->SelectNone();
  }
}

void CMainFrame::About_Function(CMainFrame *pthis) {
  if (pthis) {
    CS3DMApp *pApp = dynamic_cast<CS3DMApp *>(AfxGetApp());
    if (pApp)
      pApp->OnAppAbout();
  }
}

/////////////////////////////////////////////////////////////////////////////
// navlib

/// <summary>
/// Shutdown the connection to the navlib
/// </summary>
void CMainFrame::Disable3DNavigation() {
  nav3d::Enable = false;
}

/// <summary>
/// Open the connection to the navlib and expose the property interface
/// functions
/// </summary>
bool CMainFrame::Enable3DNavigation() {
  try {
    // Set the hint/title for the '3Dconnexion Settings' Utility.
    nav3d::Profile = YOUR_PROGRAM_NAME_GOES_HERE;

    // Enable input from / output to the Navigation3D controller.
    nav3d::Enable = true;

#if APPLICATION_HAS_ANIMATION_LOOP
    // Use the application render loop as the timing source for the frames
    nav3d::FrameTiming = TimingSource::Application;
#else
    // Use the SpaceMouse as the timing source for the frames.
    nav3d::FrameTiming = TimingSource::SpaceMouse;
#endif

    // Export the command images
    ExportImages();

    // Export the commands
    ExportCommands();
  } catch (const std::exception &) {
    return false;
  }

  return true;
}

void CacheMenu(TDx::CCommandTreeNode &parent, CMenu *menu) {
  using TDx::SpaceMouse::CCategory;
  using TDx::SpaceMouse::CCommand;

  if (menu) {
    int i;
    for (i = 0; i < menu->GetMenuItemCount(); ++i) {
      MENUITEMINFO mii = {sizeof(MENUITEMINFO), MIIM_ID | MIIM_STRING | MIIM_SUBMENU | MIIM_FTYPE};
      if (!menu->GetMenuItemInfo(i, &mii, TRUE))
        continue;

      if (mii.hSubMenu) {
        CString itemString;
        menu->GetMenuString(i, itemString, MF_BYPOSITION);
        itemString.Replace(_T("&"), _T(""));
        CCategory node(static_cast<const char *>(CT2U8(itemString)),
                       static_cast<const char *>(CT2U8(itemString)));

        CacheMenu(node, menu->GetSubMenu(i));
        parent.push_back(std::move(node));
      } else if (mii.fType == MFT_SEPARATOR)
        continue;
      else {
        CString itemString;
        menu->GetMenuString(i, itemString, MF_BYPOSITION);
        if (itemString.IsEmpty())
          continue;

        itemString.Replace(_T("&"), _T(""));
        if (itemString.GetLength() == 0)
          continue;

        itemString.Replace(_T("..."), _T(""));
        if (itemString.GetLength() == 0)
          continue;

        CApplicationCommand buttonAction(mii.wID, itemString, CApplicationCommand::menuItem);
        std::ostringstream stream;
        stream << buttonAction;

        parent.push_back(
            CCommand(stream.str(), static_cast<const char *>(CT2U8(buttonAction.GetText()))));
      }
    }
  }
}

/// <summary>
/// Expose the images of the application commands to the 3Dconnexion UI
/// </summary>
/// Note images embedded in a resource dll (e_resource_file type) use the
/// "#DecimalNumber" Microsoft string notation i.e. RT_BITMAP = "#2", RT_ICON =
/// "#3", resource id with a value of MAKEINTRESOURCE(216) = "#216". The
/// SiImage_t::id is used as the key to associate the image with the
/// corresponding command
bool CMainFrame::ExportImages() {
  std::vector<SiImage_t> images;
  // Use some images from a resource file
  images.push_back({sizeof(SiImage_t),
                    e_resource_file,
                    "MenuItem 57601" /*ID_FILE_OPEN */,
                    {{"c:/windows/system32/shell32.dll", "#16772", "#3", 0}}});
  images.push_back({sizeof(SiImage_t),
                    e_resource_file,
                    "MenuItem 57665" /*ID_APP_EXIT */,
                    {{"c:/windows/system32/shell32.dll", "#16770", "#3", 0}}});
  images.push_back({sizeof(SiImage_t),
                    e_resource_file,
                    "MenuItem 57664" /*ID_APP_ABOUT*/,
                    {{"c:/windows/system32/ieframe.dll", "#697", "#2", 8}}});
  if (images.size()) {
    nav3d::AddImages(images);
  }
  return true;
}

/// <summary>
/// Expose the application commands to the 3Dconnexion UI
/// </summary>
bool CMainFrame::ExportCommands() {
  using TDx::SpaceMouse::CCommandSet;

  // The root action set node
  CCommandSet commandSet("Default", "Modeling");

  // Activate the command set
  nav3d::ActiveCommands = commandSet.Id;

  // Add the menu(s) to the action set
  CacheMenu(commandSet, GetMenu());

  // Add the command set to the commands available for assigning to 3DMouse buttons
  nav3d::AddCommandSet(commandSet);

  return true;
}

//////////////////////////////////////////////////////////////////////////////
// The get property assessors
long CMainFrame::GetModelExtents(navlib::box_t &bbox) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    std::vector<int> *pselection = NULL;
    CExtents extents = pView->GetDocument()->Model->GetExtents(Matrix3d(), pselection);

#if (_MSC_VER >= 1800)
    bbox.min = {extents.m_minPt.x, extents.m_minPt.y, extents.m_minPt.z};
    bbox.max = {extents.m_maxPt.x, extents.m_maxPt.y, extents.m_maxPt.z};
#else
    bbox.min.x = extents.m_minPt.x;
    bbox.min.y = extents.m_minPt.y;
    bbox.min.z = extents.m_minPt.z;
    bbox.max.x = extents.m_maxPt.x;
    bbox.max.y = extents.m_maxPt.y;
    bbox.max.z = extents.m_maxPt.z;
#endif

    return 0;
  }
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

long CMainFrame::GetUnitsToMeters(double &meters) const {
  meters = 1.;
  return 0;
}

long CMainFrame::GetFloorPlane(navlib::plane_t &floor) const {
  // For the purposes of this sample simply return the first floor plane of the 3dxHouse model.
  // The correct way to implement this in an architectoral application is to hit test for the floor,
  // which has the side effect that will allow the walk navigation mode to ascend stairs.
#if (_MSC_VER >= 1800)
  floor = {{0, 1, 0}, -3.1};
#else
  floor.n.x = 0;
  floor.n.y = 1;
  floor.n.z = 0;
  floor.d = -3.1;
#endif
  return 0;
}

long CMainFrame::GetIsSelectionEmpty(navlib::bool_t &empty) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    empty = pView->Selection.size() == 0;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

long CMainFrame::GetSelectionExtents(navlib::box_t &bbox) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    if (pView->Selection.size() == 0) {
      return navlib::make_result_code(navlib::navlib_errc::no_data_available);
    }

    std::vector<int> *pselection = &pView->Selection;

    CExtents extents = pView->GetDocument()->Model->GetExtents(Matrix3d(), pselection);

#if (_MSC_VER >= 1800)
    bbox.min = {extents.m_minPt.x, extents.m_minPt.y, extents.m_minPt.z};
    bbox.max = {extents.m_maxPt.x, extents.m_maxPt.y, extents.m_maxPt.z};
#else
    bbox.min.x = extents.m_minPt.x;
    bbox.min.y = extents.m_minPt.y;
    bbox.min.z = extents.m_minPt.z;
    bbox.max.x = extents.m_maxPt.x;
    bbox.max.y = extents.m_maxPt.y;
    bbox.max.z = extents.m_maxPt.z;
#endif

    return 0;
  }
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

long CMainFrame::GetViewConstructionPlane(navlib::plane_t &plane) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    // if it is a 2D projection then return the plane parallel to the view
    // at the model position
    CMCADDoc *pDoc = pView->GetDocument();
    Vector3d translation = pDoc->Model->m_positionInParent.GetRow(3);
    if (pView->GetProjection() == e2D) {
      // The camera's z-axis
      Vector3d zAxis = pDoc->Camera->m_positionInParent.GetRow(2);
#if (_MSC_VER >= 1800)
      plane = {{zAxis.x, zAxis.y, zAxis.z}, -zAxis.DotProduct(translation)};
#else
      plane.n.x = zAxis.x;
      plane.n.y = zAxis.y;
      plane.n.z = zAxis.z;
      plane.d = -zAxis.DotProduct(translation);
#endif
    } else {
#if (_MSC_VER >= 1800)
      plane = {{0., 0., 1.}, -translation.z};
#else
      plane.n.x = 0.;
      plane.n.y = 0.;
      plane.n.z = 1.;
      plane.d = -translation.z;
#endif
    }

    return 0;
  }
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

long CMainFrame::GetViewExtents(navlib::box_t &extents) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    extents = pView->ViewExtents;
    return 0;
  }
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

long CMainFrame::GetViewFOV(double &fov) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    fov = pView->FOV;
    return 0;
  }
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

long CMainFrame::GetViewFrustum(navlib::frustum_t &frustum) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    frustum = pView->Frustum;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Get whether the view can be rotated
/// </summary>
/// <param name="rotatable">true if the view can be rotated</param>
/// <returns>0 on success otherwise an error</returns>
long CMainFrame::GetIsViewRotatable(navlib::bool_t &rotatable) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    rotatable = pView->IsRotatable;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <inheritdoc/>
long CMainFrame::IsUserPivot(navlib::bool_t &userPivot) const {
  userPivot = m_isUserPivot;
  return 0;
}

/// <summary>
/// Get the affine of the coordinate system
/// </summary>
/// <param name="affine">the world to navlib transformation</param>
/// <returns>0 on success otherwise an error</returns>
long CMainFrame::GetCoordinateSystem(navlib::matrix_t &affine) const {
#if _Z_UP
  // For Z-up
  navlib::matrix_t cs = {1., 0., 0., 0., 0., 0., -1., 0., 0., 1., 0., 0., 0., 0., 0., 1.};
#else
  // Y-Up rhs
  navlib::matrix_t cs = {1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.};
#endif
  affine = cs;

  return 0;
}

/// <summary>
/// Get the affine of the front view
/// </summary>
/// <param name="affine">the camera to world transformation of the front
/// view</param> <returns>0 on success, otherwise an error.</returns>
long CMainFrame::GetFrontView(navlib::matrix_t &affine) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    std::copy_n(static_cast<matrix16_t const>(pView->GetFrontAffine()), 16, affine.begin());
    return 0;
  }
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Get the affine of the view
/// </summary>
/// <param name="affine">the camera to world transformation</param>
/// <returns>0 on success, otherwise an error.</returns>
long CMainFrame::GetCameraMatrix(navlib::matrix_t &affine) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    std::copy_n(static_cast<matrix16_t const>(pView->CameraAffine), 16, affine.begin());
    return 0;
  }
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

///////////////////////////////////////////////////////////////////////////
// Handle navlib hit testing requests

/// <summary>
/// Get the position of the point hit
/// </summary>
/// <param name="position">the hit point</param>
/// <returns>0 when something is hit, otherwise navlib::navlib_errc::no_data_available</returns>
long CMainFrame::GetHitLookAt(navlib::point_t &position) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    Point3d pos = pView->LookingAt;
    position.x = pos.x;
    position.y = pos.y;
    position.z = pos.z;
    if (pos != pView->LookPosition)
      return 0;
    return navlib::make_result_code(navlib::navlib_errc::no_data_available);
  }
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Get the position of the rotation pivot in world coordinates
/// </summary>
/// <param name="position">the position of the pivot</param>
/// <returns>0 on success, otherwise an error.</returns>
long CMainFrame::GetPivotPosition(navlib::point_t &position) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    Point3d pos = pView->PivotPosition;
    position.x = pos.x;
    position.y = pos.y;
    position.z = pos.z;
    return 0;
  }
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <inheritdoc/>
long CMainFrame::GetPivotVisible(navlib::bool_t &visible) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    visible = pView->PivotVisible;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Get the position of the mouse cursor on the projection plane in world
/// coordinates
/// </summary>
/// <param name="position">the position of the mouse cursor</param>
/// <returns>0 on success, otherwise an error.</returns>
long CMainFrame::GetPointerPosition(navlib::point_t &position) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    Point3d pos = pView->PointerPosition;
    position.x = pos.x;
    position.y = pos.y;
    position.z = pos.z;
    return 0;
  }
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Get the whether the view is a perspective projection
/// </summary>
/// <param name="persp">true when the projection is perspective</param>
/// <returns>0 on success, otherwise an error.</returns>
long CMainFrame::GetIsViewPerspective(navlib::bool_t &persp) const {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    persp = pView->GetProjection() == ePerspective;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// set property handlers (mutators)
//

/// <summary>
/// Sets the moving property value.
/// </summary>
/// <param name="value">true when moving.</param>
/// <returns>0 on success, otherwise an error.</returns>
/// <description>The navlib sets this to true at the beginning of navigation.</description>
long CMainFrame::SetMotionFlag(bool value) {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    pView->IsMoving = value;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Sets the transaction property value
/// </summary>
/// <param name="value">!0 at the beginning of a frame;0 at the end of a
/// frame</param> <returns>0 on success, otherwise an error.</returns>
long CMainFrame::SetTransaction(long value) {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    pView->Transaction = value;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Sets the affine of the view
/// </summary>
/// <param name="affine">The camera to world transformation</param>
/// <returns>0 on success, otherwise an error.</returns>
long CMainFrame::SetCameraMatrix(const navlib::matrix_t &affine) {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    const navlib::matrix_t &a = affine;
    pView->CameraAffine = Matrix3d(a.m00, a.m01, a.m02, a.m03, a.m10, a.m11, a.m12, a.m13, a.m20,
                                   a.m21, a.m22, a.m23, a.m30, a.m31, a.m32, a.m33);
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <inheritdoc/>
long CMainFrame::GetSelectionTransform(navlib::matrix_t &affine) const {
  return navlib::make_result_code(navlib::navlib_errc::function_not_supported);
}

/// <inheritdoc/>
long CMainFrame::SetSelectionTransform(const navlib::matrix_t &affine) {
  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Sets the extents of the view
/// </summary>
/// <param name="extents">The view extents</param>
/// <returns>0 on success, otherwise an error.</returns>
long CMainFrame::SetViewExtents(const navlib::box_t &extents) {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    pView->ViewExtents = extents;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Sets the visibility of the pivot
/// </summary>
/// <param name="show">true to display the pivot</param>
/// <returns>0 on success, otherwise an error.</returns>
long CMainFrame::SetPivotVisible(bool show) {
#ifdef _TRACE_PIVOT_VISISBLE
  std::cout << "CMainFrame::SetPivotVisible(" << show << ")" << std::endl;
#endif
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    if (pView->PivotVisible != show)
      pView->PivotVisible = show;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Sets the position of the pivot
/// </summary>
/// <param name="position">The position of the pivot in world
/// coordinates</param> <returns>0 on success, otherwise an error.</returns>
long CMainFrame::SetPivotPosition(const navlib::point_t &position) {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    pView->PivotPosition = Point3d(position.x, position.y, position.z);
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Sets the vertical field of view
/// </summary>
/// <param name="fov">The fov in radians</param>
/// <returns>0 on success, otherwise an error.</returns>
long CMainFrame::SetViewFOV(double fov) {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    pView->FOV = fov;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

long CMainFrame::SetViewFrustum(const navlib::frustum_t &frustum) {
  return navlib::make_result_code(navlib::navlib_errc::function_not_supported);
}

///////////////////////////////////////////////////////////////////////////
// Handle navlib hit testing parameters

/// <summary>
/// Sets the diameter of the aperture in the projection plane to look through.
/// </summary>
/// <param name = "diameter">The diameter of the hole/ray in world units.</param>
long CMainFrame::SetHitAperture(double diameter) {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    pView->LookAperture = diameter;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Sets the direction to look
/// </summary>
/// <param name = "direction">Unit vector in world coordinates</param>
long CMainFrame::SetHitDirection(const navlib::vector_t &direction) {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    pView->LookDirection = Vector3d(direction.x, direction.y, direction.z);
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Sets the position to look from
/// </summary>
/// <param name = "position">Position in world coordinates</param>
long CMainFrame::SetHitLookFrom(const navlib::point_t &position) {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    pView->LookPosition = Point3d(position.x, position.y, position.z);
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Sets the selection only hit filter
/// </summary>
/// <param name = "value">If true then filter non-selected objects</param>
long CMainFrame::SetHitSelectionOnly(bool value) {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    pView->HitSelectionOnly = value;
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

//////////////////////////////////////////////////////////////////////////////////////

/// <summary>
/// Handle when a command is activated by a mouse button press
/// </summary>
long CMainFrame::SetActiveCommand(std::string commandId) {
  if (!commandId.empty()) {
    if (!IsWindowEnabled()) {
      return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
    }

    CApplicationCommand action;
    std::istringstream stream(std::move(commandId));
    stream >> action;
#ifdef _DEBUG
    action(this);
#else
    try {
      action(this);
    } catch (...) {
      return navlib::make_result_code(navlib::navlib_errc::invalid_function);
    }
#endif
  }
  return 0;
}

/// <summary>
/// Handle when a model is loaded and inform the navlib about the changes to the
/// model extents and front view
/// </summary>
void CMainFrame::OnLoadModel() {
  using namespace navlib;
  nav3d::Write(selection_empty_k, true);
  box_t extents;
  if (GetModelExtents(extents) == 0) {
    nav3d::Write(model_extents_k, extents);
  }

  matrix_t affine;
  if (GetFrontView(affine) == 0) {
    nav3d::Write(views_front_k, affine);
  }
}

/// <summary>
/// Handle when a the selection changes and inform the navlib
/// </summary>
void CMainFrame::OnSelectionChanged(bool selection) {
  nav3d::Write(navlib::selection_empty_k, !selection);
}

/// <summary>
/// Handle when a the manual pivot is cleared
/// </summary>
void CMainFrame::ClearManualPivot() {
  m_isUserPivot = false;
  nav3d::Write(navlib::pivot_user_k, m_isUserPivot);
}

/// <summary>
/// Handle when a the projection changes and inform the navlib
/// </summary>
void CMainFrame::OnProjectionChanged() {
  CMCADView *pView = dynamic_cast<CMCADView *>(GetActiveView());
  if (pView) {
    nav3d::Write(navlib::view_perspective_k, pView->Projection == ePerspective);
  }
}

/// <summary>
/// User defined pivot position
/// </summary>
void CMainFrame::OnManualPivot(Point3d p) {
  using namespace navlib;
  m_isUserPivot = true;
  point_t pos = {p.x * p.w, p.y * p.w, p.z * p.w};
  nav3d::Write(pivot_position_k, pos);
}

// The settings we want to read from the settings file
const char *settings_autokeyanimation_k = "settings.AutokeyAnimation";
const char *settings_lockto3dviews_k = "settings.LockTo3dviews";

/// <summary>
/// Handler for settings_changed event
/// </summary>
long CMainFrame::SetSettingsChanged(long change) {
#ifndef WAMP_CLIENT
  using namespace navlib;
  value_t value(false);

  // Query the navlib for the settings_autokeyanimation_k
  long result = nav3d::Read(settings_autokeyanimation_k, value);
  if (result == 0) {
    // Do something with the value
  }

  value = false;
  result = nav3d::Read(settings_lockto3dviews_k, value);
  if (result == 0) {
    // Do something with the value
  }

  return result;
#else
  return 0;
#endif
}