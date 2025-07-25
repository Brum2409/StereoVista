#ifndef CActionInputModel_HPP_INCLUDED
#define CActionInputModel_HPP_INCLUDED
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

#include "CCommandEventArgs.hpp"
#include "CKeyEventArgs.hpp"
#include "ICommandSignals.hpp"
#include "nod/nod.hpp"
#include "IViewModel.hpp"

// SpaceMouse
#include <SpaceMouse/CActionInput.hpp>

namespace TDx {
namespace ActionInput {
namespace Input {
/// <summary>
/// Implements the Input interface.
/// </summary>
class CActionInputModel : public SpaceMouse::ActionInput::CActionInput,
                         private Commands::ICommandSignals {
  typedef SpaceMouse::ActionInput::CActionInput base_type;

public:
  CActionInputModel() = delete;

  /// <summary>
  /// Initializes a new instance of the CActionInputModel class.
  /// </summary>
  /// <param name="vm">The <see cref="IViewModel"/> interface to use.</param>
  explicit CActionInputModel(View::IViewModel *vm);

  // The signals

  /// <summary>
  /// Invoked when a command requires execution.
  /// </summary>
  nod::signal<void(Commands::CCommandEventArgs &e)> ExecuteCommand;

  /// <summary>
  /// Invoked when a key on a 3Dconnexion device is pressed.
  /// </summary>
  nod::signal<void(Commands::CKeyEventArgs &e)> KeyDown;

  /// <summary>
  /// Invoked when a key on a 3Dconnexion device is released.
  /// </summary>
  nod::signal<void(Commands::CKeyEventArgs &e)> KeyUp;

  /// <summary>
  /// IAccessors overrides
  /// </summary>
protected:
  // IEvents overrides
  /// <inheritdoc/>
  long SetActiveCommand(std::string commandId) override;

  /// <inheritdoc/>
  long SetKeyPress(long vkey) override;

  /// <inheritdoc/>
  long SetKeyRelease(long vkey) override;

  //  ICommandSignals overrides
protected:
  /// <summary>
  /// Raises the ExecuteCommand signal
  /// </summary>
  /// <param name="e">A <see cref="CommandEventArgs"/> that contains the event data.</param>
  void OnExecuteCommand(Commands::CCommandEventArgs &e) override;

  /// <summary>
  /// Raises the KeyDown event.
  /// </summary>
  /// <param name="e">A <see cref="CKeyEventArgs"/> that contains the event data.</param>
  void OnKeyDown(Commands::CKeyEventArgs &e) override;

  /// <summary>
  /// Raises the KeyUp event.
  /// </summary>
  /// <param name="e">A <see cref="CKeyEventArgs"/> that contains the event data.</param>
  void OnKeyUp(Commands::CKeyEventArgs &e) override;

private:
  // interface to the ViewModel
  View::IViewModel *m_ivm = nullptr;
  std::mutex m_mutex;
  bool m_enableRaisingEvents = true;
};
} // namespace Input
} // namespace ActionInput
} // namespace TDx
#endif // CActionInputModel_HPP_INCLUDED