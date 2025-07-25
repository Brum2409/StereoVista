// navlib_viewer.cpp : Defines the behaviors for the application.
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
/// This file contains the definition of the CS3DMApp CWinApp derived class
/// which represents the Windows application object.
///
#include "stdafx.h"
///////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: navlib_viewer.cpp 19813 2022-11-22 08:02:16Z mbonk $
//
#include "mcaddoc.h"
#include "mcadview.h"
#include "mainfrm.h"
#include "navlib_viewer.h"
#include "resource.h"

// atl
#include <atlpath.h>

// stdlib
#include <fcntl.h>
#include <io.h>

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About
class CAboutDlg : public CDialog {
public:
  CAboutDlg();

  // Dialog Data
  //{{AFX_DATA(CAboutDlg)
  enum { IDD = IDD_ABOUTBOX };
  //}}AFX_DATA

  // Implementation
protected:
  virtual void DoDataExchange(CDataExchange *pDX); // DDX/DDV support
  //{{AFX_MSG(CAboutDlg)
  // No message handlers
  //}}AFX_MSG
  DECLARE_MESSAGE_MAP()
protected:
  BOOL OnInitDialog();
};

/////////////////////////////////////////////////////////////////////////////
// CMCADApp

BEGIN_MESSAGE_MAP(CS3DMApp, CWinApp)
//{{AFX_MSG_MAP(CMCADApp)
ON_COMMAND(ID_APP_ABOUT, &CS3DMApp::OnAppAbout)
// NOTE - the ClassWizard will add and remove mapping macros here.
//    DO NOT EDIT what you see in these blocks of generated code!
//}}AFX_MSG_MAP
// Standard file based document commands
// ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
// ON_COMMAND(ID_FILE_OPEN, CWinApp::OnFileOpen)
ON_COMMAND(ID_FILE_OPEN, &CS3DMApp::OnFileOpen)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMCADApp construction

CS3DMApp::CS3DMApp() {
  // TODO: add construction code here,
  // Place all significant initialization in InitInstance
}

CS3DMApp::~CS3DMApp() {}

/////////////////////////////////////////////////////////////////////////////
// The one and only CMCADApp object

CS3DMApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CMCADApp initialization

BOOL CS3DMApp::InitInstance() {
  // Standard initialization
  // If you are not using these features and wish to reduce the size
  //  of your final executable, you should remove from the following
  //  the specific initialization routines you do not need.
  LoadStdProfileSettings(); // Load standard INI file options (including MRU)

#if __DEBUG
  BOOL b = AllocConsole();
  *stdout = *_tfdopen(
      _open_osfhandle((intptr_t)GetStdHandle(STD_OUTPUT_HANDLE), _O_WRONLY),
      _T("a"));
#endif

  CWinApp::InitInstance();

  // Register the application's document templates.  Document templates
  //  serve as the connection between documents, frame windows and views.

  CSingleDocTemplate *pDocTemplate;
  pDocTemplate =
      new CSingleDocTemplate(IDR_MAINFRAME, RUNTIME_CLASS(CMCADDoc),
                             RUNTIME_CLASS(CMainFrame), // main SDI frame window
                             RUNTIME_CLASS(CMCADView));
  AddDocTemplate(pDocTemplate);

  // create a new (empty) document
  OnFileNew();

  if (m_lpCmdLine[0] != '\0') {
    // TODO: add command line processing here
  }

  CPath file(_T(".\\Turbine4.obj"));
  if (file.FileExists())
    OpenDocumentFile(file);

  return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CMCADApp commands

int CS3DMApp::Run() {
  // TODO: Add your specialized code here and/or call the base class

  return CWinApp::Run();
}

/////////////////////////////////////////////////////////////////////////////
// Message handlers
// App command to run the dialog
void CS3DMApp::OnAppAbout() {
  CAboutDlg aboutDlg;
  aboutDlg.DoModal();
}

void CS3DMApp::OnFileOpen() {
  CWinApp::OnFileOpen();
  // TODO: Add your command handler code here
}
/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About
CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD) {
  //{{AFX_DATA_INIT(CAboutDlg)
  //}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange *pDX) {
  CDialog::DoDataExchange(pDX);
  //{{AFX_DATA_MAP(CAboutDlg)
  //}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
//{{AFX_MSG_MAP(CAboutDlg)
// No message handlers
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

BOOL CAboutDlg::OnInitDialog() {
  CDialog::OnInitDialog();

#if (_WIN32_WINNT >= 0x0600)
  LANGID langId = ::GetThreadUILanguage();
#else
  LANGID langId = ::GetUserDefaultUILanguage();
#endif

  CString strText;
  if (!strText.LoadString(AfxGetResourceHandle(), IDS_ABOUT_TITLE, langId))
    if (!strText.LoadString(AfxGetResourceHandle(), IDS_ABOUT_TITLE))
      strText.SetString(L"");

  SetWindowText(strText.GetString());

  if (!strText.LoadString(AfxGetResourceHandle(), IDS_ABOUT_DESCRIPTION,
                          langId))
    if (!strText.LoadString(AfxGetResourceHandle(), IDS_ABOUT_DESCRIPTION))
      strText.SetString(L"");

  CWnd *pWnd = GetDlgItem(IDC_ABOUT_DESCRIPTION);
  if (pWnd)
    pWnd->SetWindowTextW(strText.GetString());

  return TRUE;
}