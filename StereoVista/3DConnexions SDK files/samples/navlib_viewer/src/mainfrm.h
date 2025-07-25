#ifndef Mainfrm_H_INCLUDED_
#define Mainfrm_H_INCLUDED_
// mainfrm.h
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
/// This file contains the declaration of the CMainFrame class, which implements
/// the accessors and mutators for the navlib as well as opening the connection
/// and exporting the application commands.
///
///////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: mainfrm.h 19813 2022-11-22 08:02:16Z mbonk $
//

#include "Matrix3d.h"
#include <SpaceMouse/CNavigation3D.hpp>

// stdlib
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>

//////////////////////////////////////////////////////////////////
// Forward declarations
class CMCADView;

//////////////////////////////////////////////////////////////////
// CMainFrame
class CMainFrame : public CFrameWnd, public TDx::SpaceMouse::Navigation3D::CNavigation3D {
  typedef TDx::SpaceMouse::Navigation3D::CNavigation3D nav3d;

protected: // create from serialization only
  CMainFrame();
  DECLARE_DYNCREATE(CMainFrame)

  // Attributes
public:
  // Operations
public:
  // Overrides
  // ClassWizard generated virtual function overrides
  //{{AFX_VIRTUAL(CMainFrame)
protected:
  BOOL PreCreateWindow(CREATESTRUCT &cs) override;
  //}}AFX_VIRTUAL

public:
  virtual ~CMainFrame();
#ifdef _DEBUG
  void AssertValid() const override;
  void Dump(CDumpContext &dc) const override;
#endif
  void OnLoadModel();
  void OnProjectionChanged();
  void OnSelectionChanged(bool selection);
  void ClearManualPivot();
  void OnManualPivot(Point3d p);

  // Generated message map functions
protected:
  //{{AFX_MSG(CMainFrame)
  afx_msg void OnActivateApp(BOOL bActive, DWORD dwThreadID);
  afx_msg void OnAppExit();
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnPaletteChanged(CWnd *pFocusWnd);
  afx_msg BOOL OnQueryNewPalette();
  //}}AFX_MSG
  DECLARE_MESSAGE_MAP()

private:
  // navlib initialization
  void Disable3DNavigation();
  bool ExportCommands();
  bool ExportImages();
  bool Enable3DNavigation();

  /////////////////////////////////////////////////////////////////////////////////////////////////
  // accessors and mutators used by the navlib
  // These are the functions supplied to the navlib in the accessors structure so that the navlib
  // can query and update our (the clients) values. These functions are used in a similar way as
  // __declspec (property(get,put)) is used
private:
  long GetCoordinateSystem(navlib::matrix_t &affine) const override;
  long GetFrontView(navlib::matrix_t &affine) const override;
  long GetViewConstructionPlane(navlib::plane_t &plane) const override;
  long GetCameraMatrix(navlib::matrix_t &affine) const override;
  long GetViewExtents(navlib::box_t &affine) const override;
  long GetViewFrustum(navlib::frustum_t &frustum) const override;
  long GetViewFOV(double &fov) const override;
  long GetIsViewRotatable(navlib::bool_t &rotatble) const override;
  long IsUserPivot(navlib::bool_t &userPivot) const override;
  long GetPivotVisible(navlib::bool_t &visible) const override;
  long GetPivotPosition(navlib::point_t &position) const override;
  long GetPointerPosition(navlib::point_t &position) const override;
  long GetIsViewPerspective(navlib::bool_t &persp) const override;
  long GetModelExtents(navlib::box_t &extents) const override;
  long GetUnitsToMeters(double &meters) const override;
  long GetFloorPlane(navlib::plane_t &floor) const override;
  long GetIsSelectionEmpty(navlib::bool_t &empty) const override;
  long GetSelectionExtents(navlib::box_t &extents) const override;
  long GetSelectionTransform(navlib::matrix_t &) const override;
  long GetHitLookAt(navlib::point_t &position) const override;

  long SetSettingsChanged(long change) override;

  long SetMotionFlag(bool value) override;
  long SetTransaction(long value) override;
  long SetCameraMatrix(const navlib::matrix_t &affine) override;
  long SetSelectionTransform(const navlib::matrix_t &affine) override;
  long SetViewExtents(const navlib::box_t &extents) override;
  long SetViewFOV(double fov) override;
  long SetViewFrustum(const navlib::frustum_t &frustum) override;
  long SetPivotPosition(const navlib::point_t &position) override;
  long SetPivotVisible(bool visible) override;

  long SetHitLookFrom(const navlib::point_t &position) override;
  long SetHitDirection(const navlib::vector_t &direction) override;
  long SetHitAperture(double diameter) override;
  long SetHitSelectionOnly(bool value) override;
  long SetActiveCommand(std::string commandId) override;

private:
  // The commands
  static void OpenFile_Function(CMainFrame *pthis);
  static void Perspective_Function(CMainFrame *pthis);
  static void Parallel_Function(CMainFrame *pthis);
  static void _2D_Function(CMainFrame *pthis);
  static void ToggleGrid_Function(CMainFrame *pthis);
  static void SelectNone_Function(CMainFrame *pthis);
  static void About_Function(CMainFrame *pthis);

  typedef struct tag_Actions {
    char *id;
    std::function<void(CMainFrame *)> function;
    uint32_t label_id;
  } actions_t;

  static actions_t m_actions[1];

private:
  bool m_isUserPivot;
};
/////////////////////////////////////////////////////////////////////////////
#endif //  Mainfrm_H_INCLUDED_