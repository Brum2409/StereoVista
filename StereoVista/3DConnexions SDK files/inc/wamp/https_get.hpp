#ifndef ws_https_get_HPP_INCLUDED_
#define ws_https_get_HPP_INCLUDED_
// <copyright file="https_get.hpp" company="3Dconnexion">
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
// $Id: https_get.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 03/10/20 MSB Adapted from
//              https://www.boost.org/doc/libs/1_73_0/libs/beast/doc/html/beast/using_io/timeouts.html
//              Copyright (c) 2016-2019 Vinnie Falco.  http://www.boost.org/LICENSE_1_0.txt.
//              Most comments are from the original source.
// </history>
// boost

#include "beast.hpp"
#include "net.hpp"

// boost
#include <boost/asio/spawn.hpp>
#include <boost/beast/ssl.hpp>

// stdlib
#include <string>

namespace ws {
/// <summary>
/// Request an HTTP resource from a TLS host and return it as a string, with a timeout.
/// </summary>
/// <param name="host">A string identifying a location. May be a descriptive name or a numeric
/// address string. If an empty string is specified, the resolved endpoints will use the loopback
/// address.</ param>
/// <param name="port">A string identifying the requested service. This may be a descriptive name
/// or a numeric string corresponding to a port number.</ param>
/// <param name="target">A string identifying the requested resource.</ param>
/// <param name="ec">>A <see cref="std::future"/></param>
/// <returns>A <see cref="beast::error_code"/> which will contain an <see cref="beast::error_code"/>
/// when the request ends.</returns>
/// <returns>A <see cref="std::string"/> containing the requested resource.</returns>
inline std::string https_get(std::string const &host,
                             std::string const &port, std::string const &target,
                      beast::error_code &ec) {
  // It is the responsibility of the algorithm to clear the error first.
  ec = {};

  // We use our own I/O context, to make this function blocking.
  net::io_context ioc;

  // This context is used to hold client and server certificates.
  net::ssl::context ctx(net::ssl::context::tlsv12);

  // This string will hold the body of the HTTP response, if any.
  std::string result;

  // Note that Networking TS does not come with spawn. This function
  // launches a "fiber" which is a coroutine that has its own separately
  // allocated stack.

  boost::asio::spawn(ioc, [&](boost::asio::yield_context yield) {
    // We use the Beast ssl_stream wrapped around a beast tcp_stream.
    beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

    // The resolver will be used to look up the IP addresses for the host name.
    net::ip::tcp::resolver resolver(ioc);

    // First, look up the name. Networking has its own timeout for this. The `yield` object is a
    // CompletionToken which specializes the `net::async_result` customization point to make the
    // fiber work.
    //
    // This call will appear to "block" until the operation completes. It isn't really blocking.
    // Instead, the fiber implementation saves the call stack and suspends the function until the
    // asynchronous operation is complete. Then it restores the call stack, and resumes the function
    // to the statement following the async_resolve. This allows an asynchronous algorithm to be
    // expressed synchronously.
    auto const endpoints = resolver.async_resolve(host, port, {}, yield[ec]);
    if (ec) {
      return;
    }

    // The function `get_lowest_layer` retrieves the "bottom most" object
    // in the stack of stream layers. In this case it will be the tcp_stream.
    // This timeout will apply to all subsequent operations collectively.
    // That is to say, they must all complete within the same 30 second
    // window.

    get_lowest_layer(stream).expires_after(std::chrono::seconds(30));

    // `tcp_stream` range connect algorithms are member functions, unlike net::
    get_lowest_layer(stream).async_connect(endpoints, yield[ec]);
    if (ec) {
      return;
    }

    // Perform the TLS handshake
    stream.async_handshake(net::ssl::stream_base::client, yield[ec]);
    if (ec) {
      return;
    }

    // Send an HTTP GET request for the target
    {
      http::request<http::empty_body> req;
      req.method(http::verb::get);
      req.target(target);
      req.version(11);
      req.set(http::field::host, host);
      req.set(http::field::user_agent, "NLClient");
      http::async_write(stream, req, yield[ec]);
      if (ec) {
        return;
      }
    }

    // Now read the response
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::async_read(stream, buffer, res, yield[ec]);
    if (ec) {
      return;
    }

    // Try to perform the TLS shutdown handshake
    stream.async_shutdown(yield[ec]);

    // `net::ssl::error::stream_truncated`, also known as an SSL "short read", indicates the peer
    // closed the connection without performing the required closing handshake (for example, Google
    // does this to improve performance). Generally this can be a security issue, but if your
    // communication protocol is self-terminated (as it is with both HTTP and WebSocket) then you
    // may simply ignore the lack of close_notify:
    //
    // https://github.com/boostorg/beast/issues/38
    //
    // https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
    //
    // When a short read would cut off the end of an HTTP message, Beast returns the error
    // beast::http::error::partial_message. Therefore, if we see a short read here, it has occurred
    // after the message has been completed, so it is safe to ignore it.

    if (ec == net::ssl::error::stream_truncated) {
      ec = {};
    }
    else if (ec) {
      return;
    }

    // Set the string to return to the caller
    result = std::move(res.body());
  });

  // `run` will dispatch completion handlers, and block until there is no more "work" remaining.
  // When this call returns, the operations are complete and we can give the caller the result.
  ioc.run();

  return result;
}
} // namespace ws
#endif // ws_https_get_HPP_INCLUDED_
