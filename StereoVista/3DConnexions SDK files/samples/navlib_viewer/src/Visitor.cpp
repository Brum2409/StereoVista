// Visitor.cpp: implementation of the Visitor class.
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
/// This file contains the definition of the CVisitor class used to traverse
/// CGeomObj nodes and execute a callback on the node.
///
#include "stdafx.h"
///////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: Visitor.cpp 15018 2018-05-17 13:02:47Z mbonk $
//
#include "GeomObj.h"
#include "Visitor.h"
#include "navlib_viewer.h"

CVisitor::CVisitor(VisitorCallback pCallback, void *puserData,
                   const std::vector<int> *pnodeFilter)
    : m_puserData(puserData), m_pfunc(pCallback), m_pnodeFilter(pnodeFilter) {}

CVisitor::~CVisitor() {}

bool CVisitor::operator()(CGeomObj *pnode, const Matrix3d &accumMatrix) {
  return (*m_pfunc)(pnode, accumMatrix, m_puserData, m_pnodeFilter);
}