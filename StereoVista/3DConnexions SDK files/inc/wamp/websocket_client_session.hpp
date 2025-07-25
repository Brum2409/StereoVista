#ifndef websocket_client_session_HPP_INCLUDED_
#define websocket_client_session_HPP_INCLUDED_
// <copyright file="websocket_client_session.hpp" company="3Dconnexion">
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
// $Id: websocket_session.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 03/18/20 MSB Adapted from beast/example/websocket/client/async-ssl-system-executor/
//              websocket_client_async_ssl_system_executor.cpp.
//              Copyright (c) 2016-2019 Vinnie Falco. http://www.boost.org/LICENSE_1_0.txt.
// </history>

#include "beast.hpp"
#include "handle_http_request.hpp"
#include "net.hpp"
#include "version.h"
#include "websocket_session.hpp"

// boost
#include <boost/asio/dispatch.hpp>
#include <boost/beast/ssl.hpp>

// stdlib
#include <iostream>
#include <memory>

namespace tdx {
namespace ws {
namespace client {
/// <summary>
/// Represents an active WebSocket connection to the server.
/// </summary>
class websocket_session : public std::enable_shared_from_this<websocket_session> {
  using tcp = boost::asio::ip::tcp;

  // Objects are constructed with a strand to ensure that handlers do not execute concurrently.
  websocket_session(net::strand<net::system_executor> ex, ssl::context &ctx)
      : m_stream(ex, ctx), m_resolver(ex) {
  }

public:
  /// <summary>
  /// Initialize a new instance of the <see cref="websocket_session">.
  /// </summary>
  /// <param name="ctx">The <see cref="ssl::context" fro the session.</param>
  /// <remarks>Delegate construction to a private constructor to be able to use the same strand for
  /// both I/O objects.</remarks>
  explicit websocket_session(ssl::context &ctx)
      : websocket_session(net::make_strand(net::system_executor{}), ctx) {
  }

  virtual ~websocket_session() {
  }

  /// <summary>
  /// Run the WebSocket session.
  /// </summary>
  /// <param name="host">A string identifying a location. May be a descriptive name or a numeric
  /// address string. If an empty string is specified, the resolved endpoints will use the loopback
  /// address.</ param>
  /// <param name="port">A string identifying the requested service. This may be a descriptive name
  /// or a numeric string corresponding to a port number.</ param>
  /// <returns>A <see cref="std::future"/> which will contain the <see cref"websocket_session"/>'s
  /// <see cref="beast::error_code"/> when the session ends.</returns>
  std::shared_future<beast::error_code> run(const char *host, const char *port) {
    std::promise<beast::error_code> session_error;
    std::shared_future<beast::error_code> state = session_error.get_future().share();

    if (is_running()) {
      // The session is in progress
      session_error.set_value(
          boost::system::errc::make_error_code(boost::system::errc::already_connected));
      return state;
    }

    // If the future has no state or a value then the previous session has ended
    m_session_error = std::move(session_error);
    m_session_state = state;

    m_host = host;

    // Look up the domain name
    m_resolver.async_resolve(
        host, port, beast::bind_front_handler(&websocket_session::on_resolve, shared_from_this()));

    return state;
  }

  /// <summary>
  /// Send the binary payload.
  /// </summary>
  /// <param name="ss">A <see cref="std::string"/> containing the payload to send.</param>
  /// <param name="">The <see cref="binary_t"/> flag indicating that the data is binary.</param>
  void send(std::string &&ss, binary_t) {
    // Post our work to the strand, this ensures that the members of `this` will not be accessed
    // concurrently.
    net::post(m_stream.get_executor(),
              beast::bind_front_handler(&websocket_session::on_send, shared_from_this(),
                                        std::move(ss), true));
  }

  /// <summary>
  /// Send the text payload.
  /// </summary>
  /// <param name="ss">A <see cref="std::string"/> containing the payload to send.</param>
  /// <param name="">The <see cref="text_t"/> flag indicating that the data is text.</param>
  void send(std::string &&ss, text_t) {
    // Post our work to the strand, this ensures that the members of `this` will not be accessed
    // concurrently.
    net::post(m_stream.get_executor(),
              beast::bind_front_handler(&websocket_session::on_send, shared_from_this(),
                                        std::move(ss), false));
  }

  /// <summary>
  /// Stream option used to adjust the fields of WebSocket requests and responses.
  /// </summary>
  /// <param name="opt">A <see cref="websocket::stream_base::decorator"/> option.</param>
  void set_option(websocket::stream_base::decorator opt) {
    m_stream.set_option(std::move(opt));
  }

  /// <summary>
  /// Close the websocket connection.
  /// </summary>
  void close() {
    m_stream.async_close(
        websocket::close_code::normal,
        beast::bind_front_handler(&websocket_session::on_close, shared_from_this()));
  }

protected:
  /// <summary>
  /// Called when the websocket connection is successfully opened.
  /// </summary>
  virtual void on_openned() {
  }

  /// <summary>
  /// Called when a message has been successfully read.
  /// </summary>
  /// <param name="buffer"></param>
  virtual void on_message(beast::flat_buffer const &buffer) {
  }

  /// <summary>
  /// Called when the websocket connection is successfully closed.
  /// </summary>
  virtual void on_closed() {
  }

  /// <summary>
  /// Query the running state of the websocket session.
  /// </summary>
  /// <returns>true if a websocket session is currently in progress, false otherwise.</returns>
  bool is_running() {
    try {
      return m_session_state.valid() &&
             m_session_state.wait_for(std::chrono::nanoseconds::zero()) ==
                 std::future_status::timeout;
    }
    catch (const std::future_error &) {
      return false;
    }
  }

private:
  /// <summary>
  /// Fails the connection and reports errors to std::cerr.
  /// </summary>
  /// <param name="ec">The <see cref="beast::error_code"/> to report.</param>
  /// <param name="what">The operation that produced the <see cref="beast::error_code"/>.</param>
  void fail(beast::error_code ec, char const *what) {
    m_stream.async_close(
        websocket::close_code::protocol_error,
        beast::bind_front_handler(&websocket_session::on_close, shared_from_this()));
#if 0
  get_lowest_layer(m_stream).close();
#endif

    m_session_error.set_value(ec);

    if (ec == net::error::operation_aborted || ec == websocket::error::closed) {
      return;
    }

    std::cerr << what << ": " << ec.message() << "\n";
  }

  /// <summary>
  /// Handle the result of an asynchronous websocket close operation.
  /// </summary>
  /// <param name="ec">The <see cref="beast::error_code"/> associated with the operation.</param>
  void on_close(beast::error_code ec) {
    if (ec) {
      std::cerr << "Close: " << ec.message() << "\n";
    }

    m_queue.clear();

    // If we get here then the connection is closed gracefully
    m_session_error.set_value(ec);

    on_closed();
  }

  /// <summary>
  /// Handle the result of an asynchronous websocket connect operation.
  /// </summary>
  /// <param name="ec">The <see cref="beast::error_code"/> associated with the operation.</param>
  /// <param name="ep">The remote endpoint to which the socket is connected.</ param>
  void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
    if (ec) {
      return fail(ec, "Connect");
    }

    // Update the host_ string. This will provide the value of the Host HTTP header during the
    // WebSocket handshake. See https://tools.ietf.org/html/rfc7230#section-5.4
    m_host += ':' + std::to_string(ep.port());

    // Set a timeout on the operation
    beast::get_lowest_layer(m_stream).expires_after(std::chrono::seconds(30));

#if 0
  // Perform the SSL handshake
  m_stream.next_layer().async_handshake(
      ssl::stream_base::client,
      beast::bind_front_handler(&websocket_session::on_ssl_handshake, shared_from_this()));
#else
    // Perform the SSL handshake
    m_stream.next_layer().handshake(ssl::stream_base::client, ec);

    on_ssl_handshake(ec);
#endif
  }

  /// <summary>
  ///  Handle the result of an asynchronous websocket handshake operation.
  /// </summary>
  /// <param name="ec">The <see cref="beast::error_code"/> associated with the operation.</param>
  /// <remarks>After calling the parents on_open method, initiates an asynchronous read from the
  /// socket.
  /// </remarks>
  void on_handshake(beast::error_code ec) {
    if (ec) {
      return fail(ec, "Handshake");
    }

    on_openned();

    // Read a message
    m_stream.async_read(m_buffer,
                        beast::bind_front_handler(&websocket_session::on_read, shared_from_this()));
  }

  /// <summary>
  /// Handle the result of an asynchronous read operation.
  /// </summary>
  /// <param name="ec">The <see cref="beast::error_code"/> associated with the operation.</param>
  /// <param name="">The <see cref="std::size_t"/> data read.</param>
  void on_read(beast::error_code ec, std::size_t) {
#if _DEBUG
    std::cout << "websocket_session::on_read " << beast::buffers_to_string(m_buffer.data()) << "\n";
#endif
    // Handle the error, if any
    if (ec) {
      return fail(ec, "Read");
    }

    on_message(m_buffer);

    // Clear the buffer
    m_buffer.consume(m_buffer.size());

    // Read another message
    m_stream.async_read(m_buffer,
                        beast::bind_front_handler(&websocket_session::on_read, shared_from_this()));
  }

  /// <summary>
  /// Called when the resolve operation completes. Connects to the IP address if the resolve was
  /// successful.
  /// </summary>
  /// <param name="ec">The result of the resolve operation.</param>
  /// <param name="results">A <see cref="tcp::resolver::results_type"> containing the endpoints as a
  /// range.</param>
  void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
    // Handle the error, if any
    if (ec) {
      return fail(ec, "Resolve");
    }

    // Set a timeout on the operation
    beast::get_lowest_layer(m_stream).expires_after(std::chrono::seconds(30));

    // Make the connection on the IP address we get from a lookup
    beast::get_lowest_layer(m_stream).async_connect(
        results, beast::bind_front_handler(&websocket_session::on_connect, shared_from_this()));
  }

  void on_send(std::string &&ss, bool binary) {
    // Always add to queue
    m_queue.emplace_back(std::move(ss), binary);

    // Are we already writing?
    if (m_queue.size() > 1) {
      return;
    }

    // We are not currently writing, so send the data immediately.
    m_stream.binary(std::get<bool>(m_queue.front()));
    m_stream.async_write(
        net::buffer(std::get<std::string>(m_queue.front())),
        beast::bind_front_handler(&websocket_session::on_write, shared_from_this()));
  }
  void on_ssl_handshake(beast::error_code ec) {
    // Handle the error, if any
    if (ec) {
      return fail(ec, "SSL Handshake");
    }

    // Turn off the timeout on the tcp_stream, because
    // the websocket stream has its own timeout system.
    beast::get_lowest_layer(m_stream).expires_never();

    // Set suggested timeout settings for the websocket
    m_stream.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

    // Perform the websocket handshake
    m_stream.async_handshake(
        m_host, "/",
        beast::bind_front_handler(&websocket_session::on_handshake, shared_from_this()));
  }

  void on_write(beast::error_code ec, std::size_t) {
    std::cout << "websocket_session::on_write " << std::get<std::string>(m_queue.front()) << "\n";

    // Handle the error, if any
    if (ec) {
      return fail(ec, "Write");
    }

    // Remove the string from the queue
    m_queue.erase(m_queue.begin());

    // If nothing more to do then return.
    if (m_queue.empty()) {
      return;
    }

    // Send the next message
    m_stream.binary(std::get<bool>(m_queue.front()));
    m_stream.async_write(
        net::buffer(std::get<std::string>(m_queue.front())),
        beast::bind_front_handler(&websocket_session::on_write, shared_from_this()));
  }

private:
  std::string m_host;
  beast::flat_buffer m_buffer;
  websocket::stream<beast::ssl_stream<beast::tcp_stream>> m_stream;
  tcp::resolver m_resolver;
  std::vector<std::pair<std::string, bool>> m_queue;
  std::promise<beast::error_code> m_session_error;
  std::shared_future<beast::error_code> m_session_state;
};
} // namespace client
} // namespace ws
} // namespace tdx
#endif // websocket_client_session_HPP_INCLUDED_
