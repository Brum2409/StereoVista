#ifndef CKeyEventArgs_HPP_INCLUDED
#define CKeyEventArgs_HPP_INCLUDED
// <copyright file="CKeyEventArgs.hpp" company="3Dconnexion">
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
// stdlib
namespace TDx {
namespace TraceNL {
namespace Navigation {
//
// Class holds key event data
//
class CKeyEventArgs {
public:
  // Dummy implementation
  explicit CKeyEventArgs(bool isDown, long key) {}
};
} // namespace Navigation
} // namespace TraceNL
} // namespace TDx
#endif // CKeyEventArgs_HPP_INCLUDED