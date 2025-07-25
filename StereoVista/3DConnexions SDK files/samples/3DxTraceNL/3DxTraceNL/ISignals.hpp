#ifndef ISignals_HPP_INCLUDED
#define ISignals_HPP_INCLUDED
// <copyright file="IEvents.hpp" company="3Dconnexion">
// ------------------------------------------------------------------------------------
// Copyright (c) 2018 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software
// Developer Kit", including all accompanying documentation, and is protected by
// intellectual property laws. All use of the 3Dconnexion Software Developer Kit
// is subject to the License Agreement found in the "LicenseAgreementSDK.txt"
// file. All rights not expressly granted by 3Dconnexion are reserved.
// ------------------------------------------------------------------------------------
// </copyright>
// <history>
// *************************************************************************************
// File History
//
// $Id: $
//
// </history>

namespace TDx {
namespace TraceNL {
namespace Navigation {
class CCommandEventArgs;
class CKeyEventArgs;

/// <summary>
/// Signals interface used by the navigation model / navigation model implementation.
/// </summary>
class ISignals {
public:
  /// <summary>
  /// Raises the CommandExecuted signal.
  /// </summary>
  /// <param name="e">A <see cref="CommandEventArgs"/> that contains the event data.</param>
  virtual void OnExecuteCommand(CCommandEventArgs &e) = 0;
  /// <summary>
  /// Raises the SettingsChanged event.
  /// </summary>
  virtual void OnSettingsChanged() = 0;
  /// <summary>
  /// Raises the KeyDown event.
  /// </summary>
  /// <param name="e">A <see cref="CKeyEventArgs"/> that contains the event data.</param>
  virtual void OnKeyDown(CKeyEventArgs &e) = 0;
  /// <summary>
  /// Raises the KeyUp event.
  /// </summary>
  /// <param name="e">A <see cref="CKeyEventArgs"/> that contains the event data.</param>
  virtual void OnKeyUp(CKeyEventArgs &e) = 0;
};
} // namespace Navigation
} // namespace TraceNL
} // namespace TDx
#endif // ISignals_HPP_INCLUDED