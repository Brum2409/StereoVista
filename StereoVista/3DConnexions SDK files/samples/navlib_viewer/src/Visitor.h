#ifndef Visitor_H_INCLUDED_
#define Visitor_H_INCLUDED_
// Visitor.h: interface for the Visitor class.
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
/// This file contains the declaration of the CVisitor class used to traverse
/// CGeomObj nodes and execute a callback on the node.
///
///////////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: Visitor.h 15018 2018-05-17 13:02:47Z mbonk $
//

typedef bool (*VisitorCallback)(CGeomObj *, const Matrix3d &, void *,
                                const std::vector<int> *);

class CVisitor {
public:
  explicit CVisitor(VisitorCallback pCallback, void *puserData,
                    const std::vector<int> *pnodeFilter = NULL);
  bool operator()(CGeomObj *pnode, const Matrix3d &accumMatrix);
  virtual ~CVisitor();

private:
  void *m_puserData;
  VisitorCallback m_pfunc;
  const std::vector<int> *m_pnodeFilter;
};

#endif // Visitor_H_INCLUDED_
