#ifndef ws_handle_http_request_HPP_INCLUDED_
#define ws_handle_http_request_HPP_INCLUDED_
// <copyright file="handle_http_request.hpp" company="3Dconnexion">
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
// $Id: handle_http_request.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 03/10/20 MSB Adapted from beast\example\http\server\async-ssl\http_server_async_ssl.cpp Copyright
//              (c) 2016-2019 Vinnie Falco.  http://www.boost.org/LICENSE_1_0.txt.
// </history>
// boost

#include "version.h"

#include "beast.hpp"
#include "client_state.hpp"

// stdlib
#include <string>

#define CLIENT_VERSION_STRING CLIENT_STRING "/" FILE_VERSION_STR

namespace ws {

/// <summary>
/// This function produces an HTTP response for the given request. The type of the response object
/// depends on the contents of the request, so the interface requires the caller to pass a generic
/// lambda for receiving the response.
/// </summary>
template <class Body, class Allocator, class Send>
void handle_http_request(client_state const& state,
                         http::request<Body, http::basic_fields<Allocator>> &&req, Send &&send) {
  /// <summary>
  /// Returns a bad request response.
  /// </summary>
  /// <param name="why">A <see cref="std::string"/> explaining why the request was bad.
  /// </param>
  auto const bad_request = [&req](std::string why) {
    http::response<http::string_body> res{http::status::bad_request, req.version()};
    res.set(http::field::server, CLIENT_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = std::move(why);
    res.prepare_payload();
    return res;
  };

  /// <summary>
  /// Returns a not found response.
  /// </summary>
  /// <param name="why">A <see cref="std::string"/> containing what was not found.</param>
  auto const not_found = [&req](std::string target) {
    http::response<http::string_body> res{http::status::not_found, req.version()};
    res.set(http::field::server, CLIENT_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = "The resource '" + std::move(target) + "' was not found.";
    res.prepare_payload();
    return res;
  };

  /// <summary>
  /// Returns a server error response.
  /// </summary>
  /// <param name="why">A <see cref="std::string"/> containing the error message.</param>
  auto const server_error = [&req](std::string what) {
    http::response<http::string_body> res{http::status::internal_server_error, req.version()};
    res.set(http::field::server, CLIENT_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = "An error occurred: '" + std::move(what) + "'";
    res.prepare_payload();
    return res;
  };

  // Make sure we can handle the method
  if (req.method() != http::verb::get && req.method() != http::verb::options) {
    return send(bad_request("Unknown HTTP-method"));
  }

  if (req.method() == http::verb::get) {
    std::stringstream body;

    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, CLIENT_VERSION_STRING);
    res.keep_alive(req.keep_alive());

    res.set(http::field::access_control_allow_origin, "*");

    if (req.target().compare("/3dconnexion/nlproxy") == 0) {
      res.set(http::field::content_type, "application/json");
      body << "{ \"port\" : " << std::dec << state.port() << ", \"version\" : \"" << PRODUCT_VERSION_STR
           << "\" }";
      res.body() = std::move(body.str());
      res.prepare_payload();
      return send(std::move(res));
    }

    else if (req.target().compare("/version") != std::string::npos) {
      res.set(http::field::content_type, "text/html");
      body << "<!doctype html><html><body>You requested " << req.target()
           << "<br>Server version: " << PRODUCT_VERSION_STR << "</body></html>";
      res.body() = std::move(body.str());
      res.prepare_payload();
      return send(std::move(res));
    }

    else {
      return send(bad_request("Illegal request-target"));
    }
  }

  if (req.method() == http::verb::options) {
    std::stringstream body;

    http::response<http::empty_body> res{http::status::ok, req.version()};
    res.set(http::field::server, CLIENT_VERSION_STRING);
    res.keep_alive(req.keep_alive());

    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET");
    res.set(http::field::content_type, "application/json");
    res.prepare_payload();
    return send(std::move(res));
  }
}
} // namespace ws
#endif // ws_handle_http_request_HPP_INCLUDED_
