#ifndef mcaddoc_H_INCLUDED_
#define mcaddoc_H_INCLUDED_
// MCADdoc.h : interface of the CMCADDoc class
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
/// This file contains the declaration of the CMCADDoc document class.
/// see https://docs.microsoft.com/en-us/cpp/mfc/document-view-architecture
///
///////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: mcaddoc.h 19813 2022-11-22 08:02:16Z mbonk $
//
#include "GeomObj.h" // Added by ClassView

// stdlib
#include <memory>

class CMCADDoc : public CDocument {
protected: // create from serialization only
  CMCADDoc();
  DECLARE_DYNCREATE(CMCADDoc)

  // Attributes
public:
  // Operations
public:
  // Overrides
  // ClassWizard generated virtual function overrides
  //{{AFX_VIRTUAL(CMCADDoc)
public:
  virtual BOOL OnNewDocument();
  //}}AFX_VIRTUAL

  // Implementation
public:
  virtual ~CMCADDoc();
  virtual void Serialize(CArchive &ar); // overridden for document i/o
#ifdef _DEBUG
  virtual void AssertValid() const;
  virtual void Dump(CDumpContext &dc) const;
#endif

  __declspec(property(get = GetSelectedObject,
                      put = PutSelectedObject)) CGeomObj *SelectedObject;
  CGeomObj *GetSelectedObject() { return m_pCurrentSelectedObj; }
  CGeomObj *PutSelectedObject(CGeomObj *SelectedObject) {
    m_pCurrentSelectedObj = SelectedObject;
    return SelectedObject;
  }

  __declspec(property(get = GetModel, put = PutModel)) CGeomObj *Model;
  CGeomObj *GetModel() const { return m_pModel.get(); }
  CGeomObj *PutModel(CGeomObj *ModelObject) {
    m_pModel.reset(ModelObject);
    return m_pModel.get();
  }

  __declspec(property(get = GetView)) CViewObj *Camera;
  CViewObj *GetView() const { return m_pViewObj.get(); }

private:
  CGeomObj *m_pCurrentSelectedObj; // currently selected node
  std::unique_ptr<CViewObj> m_pViewObj;
  std::unique_ptr<CGeomObj> m_pModel; // ptr to the world node

protected:
  // Generated message map functions
protected:
  //{{AFX_MSG(CMCADDoc)
  // NOTE - the ClassWizard will add and remove member functions here.
  //    DO NOT EDIT what you see in these blocks of generated code !
  //}}AFX_MSG
  DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////
#endif // ndef mcaddoc_H_INCLUDED_
