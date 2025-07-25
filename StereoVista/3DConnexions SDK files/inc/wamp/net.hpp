#ifndef net_HPP_INCLUDED_
#define net_HPP_INCLUDED_
// <copyright file="net.hpp" company="3Dconnexion">
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
// $Id: net.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 02/12/20 MSB Initial Design
// </history>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
namespace  {
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
}

#endif // net_HPP_INCLUDED_
