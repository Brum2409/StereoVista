// capplicationcommand.cpp
/*
 * Copyright (c) 2014-2018 3Dconnexion. All rights reserved.
 *
 * This file and source code are an integral part of the "3Dconnexion
 * Software Developer Kit", including all accompanying documentation,
 * and is protected by intellectual property laws. All use of the
 * 3Dconnexion Software Developer Kit is subject to the License
 * Agreement found in the "LicenseAgreementSDK.txt" file.
 * All rights not expressly granted by 3Dconnexion are reserved.
 */
/////////////////////////////////////////////////////////////////////////////
///
/// \file
/// This file contains the definition of the CApplicationCommand class. This
/// functor class is used to serialize and de-serialize application commands
/// using the << and >> operators. Serializing the application command will
/// create a unique identifier that can be used to identify the command.
/// Invoking the operator() will invoke the contained command.
///
#include "stdafx.h"
/////////////////////////////////////////////////////////////////////////////
// History
//
// $id:$
//
#include "capplicationcommand.hpp"

/////////////////////////////////////////////////////////////////////////////
// CApplicationCommand
CApplicationCommand::CApplicationCommand() : m_id(0), m_type(unknown) {}

CApplicationCommand::~CApplicationCommand() {}

CApplicationCommand::CApplicationCommand(const CApplicationCommand &rhs)
    : m_id(rhs.m_id), m_type(rhs.m_type), m_text(rhs.m_text),
      m_strId(rhs.m_strId) {}

CApplicationCommand::CApplicationCommand(uint32_t actionId, const CString &text,
                                         ActionType type)
    : m_id(actionId), m_type(type), m_text(text) {}

CApplicationCommand::CApplicationCommand(const CString &actionId,
                                         const CString &text, ActionType type)
    : m_id(0), m_type(type), m_text(text), m_strId(actionId) {}

const CApplicationCommand &CApplicationCommand::
operator=(const CApplicationCommand &rhs) {
  m_id = rhs.m_id;
  m_type = rhs.m_type;
  m_text = rhs.m_text;
  m_strId = rhs.m_strId;
  return *this;
}

CString CApplicationCommand::GetText() { return m_text; }

bool CApplicationCommand::operator()(CMainFrame *p) {
  if (m_type == menuItem) {
    if (m_id) {
      try {
        ::PostMessageA(::AfxGetApp()->GetMainWnd()->m_hWnd, WM_COMMAND, m_id,
                       0);
      } catch (...) {
      }
      return true;
    }
  }
  return false;
}

/// <summary>
/// This is used to stream the application command to a string which is used to
/// uniquely identify the command. Here we use the command id
/// </summary>
std::ostream &CApplicationCommand::operator<<(std::ostream &str) const {
  if (m_type == menuItem)
    str << "MenuItem"
        << " " << m_id;
  return str;
}

/// <summary>
/// Stream in the application command to a string which is uniquely identified
/// by the command id.
/// </summary>
std::istream &CApplicationCommand::operator>>(std::istream &str) {
  std::string type;
  str >> type;
  std::string id;
  if (type.compare("MenuItem") == 0) {
    m_type = menuItem;
    str >> m_id;
  }
  m_strId = CU8T(id.c_str());
  return str;
}

std::ostream &operator<<(std::ostream &str, const CApplicationCommand &action) {
  return action.operator<<(str);
}

std::istream &operator>>(std::istream &str, CApplicationCommand &action) {
  return action.operator>>(str);
}