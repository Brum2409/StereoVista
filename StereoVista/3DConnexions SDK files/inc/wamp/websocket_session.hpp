#ifndef websocket_session_HPP_INCLUDED_
#define websocket_session_HPP_INCLUDED_
// <copyright file="websocket_session.hpp" company="3Dconnexion">
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
// $Id: websocket_session.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// </history>

// stdlib
#include <memory>

namespace tdx {
namespace ws {
/// <summary>
/// binary message type.
/// </summary>
struct binary_t {
  explicit binary_t() = default;
};

/// <summary>
/// text message type.
/// </summary>
struct text_t {
  explicit text_t() = default;
};

constexpr binary_t binary{};
constexpr text_t text{};

} // namespace ws
} // namespace tdx
#endif // websocket_session_HPP_INCLUDED_
