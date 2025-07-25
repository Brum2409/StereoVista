#ifndef CCommandEventArgs_HPP_INCLUDED
#define CCommandEventArgs_HPP_INCLUDED
// <copyright file="CommandEventArgs.hpp" company="3Dconnexion">
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
// stdlib
#include <string>
namespace TDx {
namespace TraceNL {
namespace Navigation {
/// <summary>
/// Class provides data for the Command event.
/// </summary>
class CCommandEventArgs : public CEventArgs {
public:
  /// <summary>
  /// Initializes a new instance of the CCommandEventArgs class.
  /// </summary>
  /// <param name="id">The id of the command to invoke.</param>
  explicit CCommandEventArgs(std::string id) : m_id(std::move(id)) {}

#if defined(_MSC_EXTENSIONS)
  /// <summary>
  /// Gets the identifier of the command to invoke.
  /// </summary>
  __declspec(property(get = GetId)) const std::string &Id;
#endif

  /// <summary>
  /// Gets the identifier of the command to invoke.
  /// </summary>
  /// <returns>The command's unique string identifier.</returns>
  const std::string &GetId() { return m_id; }

private:
  std::string m_id;
};
} // namespace Navigation
} // namespace TraceNL
} // namespace TDx
#endif // CCommandEventArgs_HPP_INCLUDED