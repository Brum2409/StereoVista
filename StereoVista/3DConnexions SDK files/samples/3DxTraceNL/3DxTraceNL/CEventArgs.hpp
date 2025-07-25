#ifndef CEventArgs_HPP_INCLUDED
#define CEventArgs_HPP_INCLUDED
// <copyright file="CEventArgs.hpp" company="3Dconnexion">
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

namespace TDx {
namespace TraceNL {
namespace Navigation {

/// <summary>
///  Represents the base class for classes that contain event data.
/// </summary>
class CEventArgs {
public:
#if defined(_MSC_EXTENSIONS)
  /// <summary>
  /// Initializes a new instance of the CEventArgs class.
  /// </summary>
  CEventArgs() : m_handled(false) {}

  /// <summary>
  /// Gets or sets a value indicating whether the event has been handled.
  /// </summary>
  __declspec(property(get = GetHandled, put = PutHandled)) bool Handled;

  bool GetHandled() const { return m_handled; }
  void PutHandled(bool value) { m_handled = value; }

private:
  bool m_handled;
#else
  CEventArgs() : Handled(false) {}

  bool Handled;
#endif // defined(_MSC_EXTENSIONS)
};
} // namespace Navigation
} // namespace TraceNL
} // namespace TDx
#endif // CEventArgs_HPP_INCLUDED