// <copyright file="CActionInputModel.cpp" company="3Dconnexion">
// ------------------------------------------------------------------------------------------------
// Copyright (c) 2022 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer Kit",
// including all accompanying documentation, and is protected by intellectual property laws. All
// use of the 3Dconnexion Software Developer Kit is subject to the License Agreement found in the
// "LicenseAgreementSDK.txt" file. All rights not expressly granted by 3Dconnexion are reserved.
// ------------------------------------------------------------------------------------------------
// </copyright>
// <history>
// ************************************************************************************************
// File History
//
// $Id$
//
// </history>

#include "framework.h"

#include "CActionInputModel.hpp"


namespace TDx {
namespace ActionInput {
namespace Input {

using namespace Commands;

/// <summary>
/// Initializes a new instance of the CActionInputModel class.
/// </summary>
/// <param name="vm"></param>
/// <param name="vm">The <see cref="IViewModelNavigation"/> interface to use.</param>
/// <remarks>The win32 ActionInput sample requires the multi-threaded navlib as it doesn't have
/// a windows message pump.</remarks>
CActionInputModel::CActionInputModel(View::IViewModel *vm) : base_type(true), m_ivm(vm) {}

/// <summary>
/// IEvents overrides
/// </summary>
long CActionInputModel::SetActiveCommand(std::string commandId) {
  CCommandEventArgs e(std::move(commandId));

  if (m_enableRaisingEvents) {
    OnExecuteCommand(e);
    if (!e.IsHandled()) {
      return navlib::make_result_code(navlib::navlib_errc::invalid_function);
    }
  }

  if (!e.IsHandled()) {
    return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
  }

  return 0;
}

long CActionInputModel::SetKeyPress(long vkey) {
  CKeyEventArgs e(true, vkey);

  if (m_enableRaisingEvents) {
    OnKeyDown(e);
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

long CActionInputModel::SetKeyRelease(long vkey) {
  CKeyEventArgs e(true, vkey);

  if (m_enableRaisingEvents) {
    OnKeyUp(e);
    return 0;
  }

  return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
}

/// <summary>
/// Raises the ExecuteCommand signal
/// </summary>
/// <param name="e">A <see cref="CommandEventArgs"/> that contains the event data.</param>
void CActionInputModel::OnExecuteCommand(CCommandEventArgs &e) { ExecuteCommand(e); }

/// <summary>
/// Raises the KeyDown event.
/// </summary>
/// <param name="e">A <see cref="CKeyEventArgs"/> that contains the event data.</param>
void CActionInputModel::OnKeyDown(CKeyEventArgs &e) { KeyDown(e); }

/// <summary>
/// Raises the KeyUp event.
/// </summary>
/// <param name="e">A <see cref="CKeyEventArgs"/> that contains the event data.</param>
void CActionInputModel::OnKeyUp(CKeyEventArgs &e) { KeyUp(e); }
} // namespace Input
} // namespace ActionInput
} // namespace TDx