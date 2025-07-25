#ifndef CKeyEventArgs_HPP_INCLUDED
#define CKeyEventArgs_HPP_INCLUDED
// <copyright file="CKeyEventArgs.hpp" company="3Dconnexion">
// -------------------------------------------------------------------------------------------
// Copyright (c) 2018-2019 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer Kit",
// including all accompanying documentation, and is protected by intellectual property laws.
// All use of the 3Dconnexion Software Developer Kit is subject to the License Agreement found
// in the "LicenseAgreementSDK.txt" file.
// All rights not expressly granted by 3Dconnexion are reserved.
// -------------------------------------------------------------------------------------------
// </copyright>
// <history>
// *******************************************************************************************
// File History
//
// $Id: $
//
// </history>
#include "CEventArgs.hpp"
namespace TDx {
namespace TraceNL {
namespace Navigation {
/// <summary>
/// Class provides data for the key events.
/// </summary>
class CKeyEventArgs : public CEventArgs {
public:
  /// <summary>
  /// Initializes a new instance of the CKeyEventArgs class.
  /// </summary>
  /// <param name="isDown">true if the key is in the down state, false otherwise.</param>
  /// <param name="key">The virtual SpaceMouse key code.</param>
  explicit CKeyEventArgs(bool isDown, long key) : m_isDown(isDown), m_key(key) {}

  /// <summary>
  /// Gets the SpaceMouse key associated with the event.
  /// </summary>
  /// <returns>One of the virtual SpaceMouse key codes.</returns>
  long GetKey() const { return m_key; }

  /// <summary>
  /// Gets a value indicating whether the key referenced by the event is in the down state.
  /// </summary>
  /// <returns>true if the key is pressed, false otherwise.</returns>
  bool IsDown() const { return m_isDown; }

private:
  bool m_isDown;
  long m_key;
};
} // namespace Navigation
} // namespace TraceNL
} // namespace TDx
#endif // CKeyEventArgs_HPP_INCLUDED