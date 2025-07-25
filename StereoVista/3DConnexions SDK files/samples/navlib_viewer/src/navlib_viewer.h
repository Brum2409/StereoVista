#ifndef navlib_viewer_H_INCLUDED_
#define navlib_viewer_H_INCLUDED_
// navlib_viewer.h
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
/// This file contains the declaration of the CS3DMApp CWinApp derived class.
///
///////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: navlib_viewer.h 15018 2018-05-17 13:02:47Z mbonk $
//

#include "resource.h" // main symbols

/////////////////////////////////////////////////////////////////////////////
// CS3DMApp
class CS3DMApp : public CWinApp {
public:
  CS3DMApp();
  virtual ~CS3DMApp();

  // Overrides
  // ClassWizard generated virtual function overrides
  //{{AFX_VIRTUAL(CMCADApp)
public:
  virtual BOOL InitInstance();
  virtual int Run();
  //}}AFX_VIRTUAL

  // Implementation
  //{{AFX_MSG(CMCADApp)
  afx_msg void OnAppAbout();
  afx_msg void OnFileOpen();
  // NOTE - the ClassWizard will add and remove member functions here.
  //    DO NOT EDIT what you see in these blocks of generated code !
  //}}AFX_MSG
  DECLARE_MESSAGE_MAP()
};
#endif // navlib_viewer_H_INCLUDED_
