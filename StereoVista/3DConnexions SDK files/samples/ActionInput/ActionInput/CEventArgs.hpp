#ifndef CEventArgs_HPP_INCLUDED
#define CEventArgs_HPP_INCLUDED
// <copyright file="CEventArgs.hpp" company="3Dconnexion">
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
// $Id: $
//
// </history>

namespace TDx {
namespace Commands {

/// <summary>
///  Represents the base class for classes that contain event data.
/// </summary>
class CEventArgs {
public:
  /// <summary>
  /// Gets a value indicating whether the event has been handled.
  /// </summary>
  /// <returns>true if handled, otherwise false.</returns>
  bool IsHandled() const { return m_handled; }

  /// <summary>
  /// Sets a value indicating whether the event has been handled.
  /// </summary>
  /// <param name="value">true if handled, otherwise false.</param>
  void PutHandled(bool value) { m_handled = value; }

private:
  bool m_handled = false;
};
} // namespace Commands
} // namespace TDx
#endif // CEventArgs_HPP_INCLUDED