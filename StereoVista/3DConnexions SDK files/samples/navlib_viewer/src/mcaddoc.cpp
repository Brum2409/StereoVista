// MCADdoc.cpp : implementation of the CMCADDoc class
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
/// This file contains the definition of the CMCADDoc document class.
/// see https://docs.microsoft.com/en-us/cpp/mfc/document-view-architecture
///
#include "stdafx.h"
///////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: mcaddoc.cpp 19813 2022-11-22 08:02:16Z mbonk $
//
#include "GeomObj.h"
#include "mcaddoc.h"
#include "navlib_viewer.h"

#ifdef _DEBUG
#undef THIS_FILE
static char BASED_CODE THIS_FILE[] = __FILE__;
#endif

void handler_invalid_parameter(const wchar_t *expression,
                               const wchar_t *function, const wchar_t *file,
                               unsigned int line, uintptr_t pReserved) {
  throw "Invalid parameter error.";
}

/////////////////////////////////////////////////////////////////////////////
// CMCADDoc

IMPLEMENT_DYNCREATE(CMCADDoc, CDocument)

BEGIN_MESSAGE_MAP(CMCADDoc, CDocument)
//{{AFX_MSG_MAP(CMCADDoc)
// NOTE - the ClassWizard will add and remove mapping macros here.
//    DO NOT EDIT what you see in these blocks of generated code!
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMCADDoc construction/destruction

CMCADDoc::CMCADDoc()
    : m_pCurrentSelectedObj(NULL), m_pViewObj(new CViewObj),
      m_pModel(new CGeomObj) {}

CMCADDoc::~CMCADDoc() {}

BOOL CMCADDoc::OnNewDocument() {
  if (!CDocument::OnNewDocument())
    return FALSE;

  // TODO: add reinitialization code here
  // (SDI documents will reuse this document)

  return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CMCADDoc serialization

void CMCADDoc::Serialize(CArchive &ar) {
  if (ar.IsStoring()) {
    // TODO: add storing code here
  } else {
    // TODO: add loading code here
    m_pModel.reset(new CWavefrontObj());
    m_pCurrentSelectedObj = m_pModel.get();

    _invalid_parameter_handler handler_new = handler_invalid_parameter;
    _invalid_parameter_handler handler_old =
        _set_invalid_parameter_handler(handler_new);

    try {
      dynamic_cast<CWavefrontObj *>(m_pModel.get())->Serialize(ar);
    } catch (...) {
      AfxGetMainWnd()->MessageBox(TEXT(
          "Serialization failure.\nPlease use Wavefront (OBJ) files only."));
    }

    _set_invalid_parameter_handler(handler_old);
    handler_old = NULL;
  }
}

#ifdef _DEBUG
/////////////////////////////////////////////////////////////////////////////
// CMCADDoc diagnostics

void CMCADDoc::AssertValid() const { CDocument::AssertValid(); }

void CMCADDoc::Dump(CDumpContext &dc) const { CDocument::Dump(dc); }
#endif //_DEBUG