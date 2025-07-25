#ifndef json_HPP_INCLUDED_
#define json_HPP_INCLUDED_
// <copyright file="json.hpp" company="3Dconnexion">
// ------------------------------------------------------------------------------------------------
// Copyright (c) 2013-2021 3Dconnexion. All rights reserved.
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
// $Id: json.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 12/10/13 MSB Initial design
// </history>

#ifndef JSON_USE_IMPLICIT_CONVERSIONS
#define JSON_USE_IMPLICIT_CONVERSIONS 0
#endif
#include <nlohmann/json.hpp>
namespace tdx {
using nlohmann::json;
} // namespace tdx
#endif // json_HPP_INCLUDED_