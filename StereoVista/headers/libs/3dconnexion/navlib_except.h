#ifndef NAVLIB_EXCEPT_H_INCLUDED_
#define NAVLIB_EXCEPT_H_INCLUDED_
// <copyright file="navlib_except.h" company="3Dconnexion">
// -------------------------------------------------------------------------------------------------
// Copyright (c) 2014-2024 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer Kit",
// including all accompanying documentation, and is protected by intellectual property laws. All use
// of the 3Dconnexion Software Developer Kit is subject to the License Agreement found in the
// "LicenseAgreementSDK.txt" file. All rights not expressly granted by 3Dconnexion are reserved.
// -------------------------------------------------------------------------------------------------
// </copyright>
// <history>
// *************************************************************************************************
// File History
//
// $Id$
//
// 03/08/23 MSB Initial design
// </history>
// <description>
// *************************************************************************************************
// File Description
//
// This header file defines the classes used for exceptions.
//
// *************************************************************************************************
// </description>

#include <navlib/navlib_defines.h>

#include <stdexcept>

NAVLIB_BEGIN_

class invalid_type : public std::runtime_error {
public:
  using runtime_error::runtime_error;
};

NAVLIB_END_ // namespace navlib
#endif      // NAVLIB_EXCEPT_H_INCLUDED_
