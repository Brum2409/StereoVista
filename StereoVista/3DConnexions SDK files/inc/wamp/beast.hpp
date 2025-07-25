#ifndef beast_HPP_INCLUDED_
#define beast_HPP_INCLUDED_
// <copyright file="beast.hpp" company="3Dconnexion">
// ------------------------------------------------------------------------------------------------
// Copyright (c) 2020-2021 3Dconnexion. All rights reserved.
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
// $Id: beast.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 02/12/20 MSB Initial Design
// </history>

#include <boost/beast.hpp>
namespace  {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
}

#endif // beast_HPP_INCLUDED_
