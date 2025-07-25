#ifndef CAPPLICATIONCOMMAND_HPP_INCLUDED
#define CAPPLICATIONCOMMAND_HPP_INCLUDED
// capplicationcommand.hpp
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
///////////////////////////////////////////////////////////////////////////////
///
/// \file
/// This file contains the declaration of the CApplicationCommand class. This
/// functor class is used to serialize and de-serialize application commands.
///
/////////////////////////////////////////////////////////////////////////////
// History
//
// $id:$
//
// 07/14/14 MSB SmartUI Version
//

// atl
#include <atlstr.h>

// stdlib
#include <cstdint>
#include <iostream>
#include <string>

class CMainFrame;
/////////////////////////////////////////////////////////////////////////////
// CApplicationCommand
//
// This functor class is used to serialize and de-serialize application commands
// using the << and >> operators. Serializing the application command will
// create a unique identifier that can be used to identify the command.
// Invoking the operator() will invoke the contained command.

class CApplicationCommand {
public:
  enum ActionType { unknown = 0, menuItem = 1 };
  // Construction
public:
  CApplicationCommand();
  CApplicationCommand(const CApplicationCommand &);
  CApplicationCommand(uint32_t actionId, const CString &text, ActionType type);
  CApplicationCommand(const CString &actionId, const CString &text,
                      ActionType type);
  virtual ~CApplicationCommand();

  // interface
public:
  CString GetText();

  // operators
public:
  const CApplicationCommand &operator=(const CApplicationCommand &);
  bool operator()(CMainFrame *);

  std::ostream &operator<<(std::ostream &str) const;
  std::istream &operator>>(std::istream &str);

private:
  uint32_t m_id;
  ActionType m_type;
  CString m_text;
  CString m_strId;
};

std::ostream &operator<<(std::ostream &str, const CApplicationCommand &);
std::istream &operator>>(std::istream &str, CApplicationCommand &action);

#endif // #define CAPPLICATIONCOMMAND_HPP_INCLUDED