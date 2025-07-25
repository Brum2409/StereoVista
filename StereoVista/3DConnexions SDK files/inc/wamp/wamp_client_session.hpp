#ifndef wamp_session_HPP_INCLUDED_
#define wamp_session_HPP_INCLUDED_
// <copyright file="wamp_session.hpp" company="3Dconnexion">
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
// $Id: wamp_session.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// </history>

#include "json.hpp"
#include "wamp_prefix.hpp"
#include "websocket_client_session.hpp"

// stdlib
#include <functional>
#include <future>
#include <map>
#include <string>

// navlib
#include <navlib/navlib_types.h>

namespace tdx {
namespace wamp {
namespace client {
/// <summary>
/// Represents an active Wamp connection to the server.
/// </summary>
template <class T> class wamp_session : public ws::client::websocket_session {
  typedef ws::client::websocket_session websocket_type;

public:
  typedef std::function<void(std::error_code, json)> then_t;

  /// <summary>
  /// Initialize a new instance of the <see cref="wamp_session">.
  /// </summary>
  /// <param name="ctx">The <see cref="ssl::context" fro the session.</param>
  explicit wamp_session(ssl::context &ctx) : websocket_type(ctx) {
  }

  /// <summary>
  /// Destruct the wamp_session instance.
  /// </summary>
  virtual ~wamp_session() {
  }

  /// <summary>
  /// Run the wamp session.
  /// </summary>
  /// <param name="host">A string identifying a location. May be a descriptive name or a numeric
  /// address string. If an empty string is specified, the resolved endpoints will use the loopback
  /// address.</ param>
  /// <param name="port">A string identifying the requested service. This may be a descriptive name
  /// or a numeric string corresponding to a port number.</ param>
  /// <returns>A <see cref="std::future"/> which will contain the <see cref"websocket_session"/>'s
  /// <see cref="beast::error_code"/> when the session ends.</returns>
  std::shared_future<beast::error_code> run(const char *host, const char *port) {
    // Set a decorator to change the user agent of the handshake and the secondary protocol.
    set_option(websocket::stream_base::decorator([](websocket::request_type &req) {
      req.set(http::field::sec_websocket_protocol, "wamp");
    }));

    return websocket_session::run(host, port);
  }

  /// <summary>
  /// Close the websocket connection.
  /// </summary>
  void close() {
    websocket_type::close();
  }

protected:
  std::string CurieToUri(const std::string &curie) const {
    return m_prefixes.ToUri(curie);
  }

  std::string GetHostUri(const std::string &uri) const {
    return m_prefixes.GetHostUri(uri);
  }

  /// <summary>
  /// Send a wamp call message.
  /// </summary>
  /// <typeparam name="then_">The functor declaration.</typeparam>
  /// <param name="callId">The call id of the message.</param>
  /// <param name="message">The call message.</param>
  /// <param name="then">The functor to invoke when the call result is received.</param>
  template <class then_> void send_call(std::string &&callId, std::string &&message, then_ &&then) {
    auto fn = make_shared_function(std::forward<then_>(then));
    if (!is_running()) {
      return fn(std::error_code(wamp::error::invalid_state, wamp::get_wamp_category()),
                tdx::json{});
    }

    std::unique_lock<std::mutex> guard(m_promises.m_mutex);
    m_promises.emplace_hint(m_promises.end(), std::move(callId), std::move(fn));
    guard.unlock();

    return websocket_session::send(std::move(message), ws::text);
  }

  /// <summary>
  /// websocket_session overrides.
  /// </summary>
private:
  /// <inheritdoc/>
  void on_openned() override {
  }

  /// <inheritdoc/>
  void on_message(beast::flat_buffer const &buffer) override {
    std::string msg = beast::buffers_to_string(buffer.data());

#if _DEBUG
    std::clog << "wamp_session::on_message " << msg << "\n";
#endif

    handle_message(std::move(msg));
  }

  void on_closed() override {
    static_cast<T *>(this)->handle_close(json::array());
  }

  void handle_message(std::string msg) {
    json wamp_message;
    try {
      wamp_message = json::parse(std::move(msg));
    }
    catch (const json::parse_error &ex) {
      std::cerr << "JSON syntax error at byte " << ex.byte << std::endl;
      return;
    }

    // All wamp messages are arrays
#if Trace_ParseMessage
    // Quick and dirty for now
    std::clog << "Message: " << wamp_message << "\n";
#endif
    if (!wamp_message.is_array()) {
      return;
    }

    auto iter = wamp_message.begin();

    try {
      int64_t opcode = iter->get<int64_t>();

      switch (opcode) {
      case wamp::TYPE_ID_WELCOME: {
        static_cast<T *>(this)->handle_welcome_message(std::move(wamp_message));
      } break;

      case wamp::TYPE_ID_PREFIX: {
        static_cast<T *>(this)->handle_prefix_message(std::move(wamp_message));
      } break;

      case wamp::TYPE_ID_CALL: {
        static_cast<T *>(this)->handle_call_request(std::move(wamp_message));
      } break;

      case wamp::TYPE_ID_CALLRESULT: {
        static_cast<T *>(this)->handle_callresult(std::move(wamp_message));
      } break;

      case wamp::TYPE_ID_CALLERROR: {
        static_cast<T *>(this)->handle_callerror(std::move(wamp_message));
      } break;

      case wamp::TYPE_ID_SUBSCRIBE: {
        static_cast<T *>(this)->handle_subscribe_request(std::move(wamp_message));
      } break;

      case wamp::TYPE_ID_UNSUBSCRIBE: {
        static_cast<T *>(this)->handle_unsubscribe_request(std::move(wamp_message));
      } break;

      case wamp::TYPE_ID_PUBLISH: {
        static_cast<T *>(this)->handle_publish_request(std::move(wamp_message));
      } break;

      case wamp::TYPE_ID_EVENT: {
        static_cast<T *>(this)->handle_event_message(std::move(wamp_message));
      } break;

      default: {
#if Trace_ParseMessage
        std::clog << " (TYPE_ID_UNKNOWN)" << std::endl;
#endif
        beast::error_code ec(EBADMSG, beast::generic_category());
        throw(ec);
      } break;
      }
    }
    catch (const tdx::json::exception &e) {
      std::cerr << "JSON syntax error: " << e.what() << std::endl;
    }
  }

  void handle_call_request(json args) {
#if Trace_ParseMessage
    std::clog << " (TYPE_ID_CALL)" << std::endl;
#endif
  }

  /// <summary>
  /// Handle the error response to a remote procedure call.
  /// </summary>
  /// <param name="args">A <see <cref="json::array"/> containing the json message.</param>
  /// <remarks>
  /// [ TYPE_ID_CALLERROR , callID , errorURI , errorDesc , errorDetails ]
  /// </remarks>
  void handle_callerror(json msg) {
#if Trace_ParseMessage
    std::clog << " (TYPE_ID_CALLERROR)" << std::endl;
#endif

    std::unique_lock<std::mutex> guard(m_promises.m_mutex);
    auto iter = m_promises.find(msg.at(1).get<std::string>());
    if (iter != m_promises.end()) {
      // get the promise for use later
      then_t fn = std::move(iter->second);
      // remove the callid entry
      m_promises.erase(iter);
      guard.unlock();

      if (fn != nullptr) {
        msg.erase(msg.begin(), msg.begin() + 1);
        return fn(
            std::error_code(static_cast<int>(wamp::error::call_error), wamp::get_wamp_category()),
            std::move(msg));
      }
    }
#if Trace_call
    std::cerr << "Promise for " << args[0].get_str() << " could not be found!" << std::endl;
#endif
  }

  /// <summary>
  /// Handle the response to a remote procedure call.
  /// </summary>
  /// <param name="msg">A <see <cref="json::array"/> containing the json message.</param>
  /// <remarks>
  /// [ TYPE_ID_CALLRESULT , callID , result ]
  /// </remarks>
  void handle_callresult(const json &msg) {
#if Trace_ParseMessage
    std::clog << " (TYPE_ID_CALLRESULT)" << std::endl;
#endif
    std::unique_lock<std::mutex> guard(m_promises.m_mutex);
    auto iter = m_promises.find(msg.at(1).get<std::string>());
    if (iter != m_promises.end()) {
      // get the promise for use later
      then_t fn = std::move(iter->second);
      // remove the callid entry
      m_promises.erase(iter);
      guard.unlock();
      if (fn != nullptr) {
        return fn(std::error_code(), msg.at(2));
      }
    }
#if Trace_call
    std::cerr << "Promise for " << args[1].get_str() << " could not be found!" << std::endl;
#endif
  }

  /// <summary>
  /// Handle the event message.
  /// </summary>
  /// <param name="args">A <see cref="json::array"/> containing the json message.</param>
  /// <remarks>
  /// The json message format is [ TYPE_ID_EVENT , topicURI , event ].
  /// topic_uri — The topic URI or CURIE to publish the event to.
  /// event - The event payload.
  /// </remarks>
  void handle_event_message(json msg) {
#if Trace_ParseMessage
    std::clog << " (TYPE_ID_EVENT)" << std::endl;
#endif
  }

  /// <summary>
  /// Handle the prefix message.
  /// </summary>
  /// <param name="args">A <see <cref="json::array"/> containing the json message.</param>
  /// <remarks>
  /// [ TYPE_ID_PREFIX , prefix , URI ]
  /// </remarks>
  void handle_prefix_message(const json &msg) {
#if Trace_ParseMessage
    std::clog << " (TYPE_ID_PREFIX)" << std::endl;
#endif
    if (msg.size() > 2) {
      // don't do any syntax checking simply store this for now
      m_prefixes.emplace(msg[1].get<std::string>(), msg[2].get<std::string>());
    }
  }

  /// <summary>
  ///
  /// </summary>
  /// <param name="msg"></param>
  /// <remarks>
  /// [ TYPE_ID_PUBLISH, topicURI, event ] [ TYPE_ID_PUBLISH, topicURI, event, excludeMe ]
  /// [ TYPE_ID_PUBLISH, topicURI, event, exclude, eligible ].
  /// </remarks>
  void handle_publish_request(json msg) {
#if Trace_ParseMessage
    std::clog << " (TYPE_ID_PUBLISH)" << std::endl;
#endif
  }

  /// <summary>
  ///
  /// </summary>
  /// <param name="msg"></param>
  /// <remarks>
  /// [ TYPE_ID_SUBSCRIBE, topicURI ].
  /// </remarks>
  void handle_subscribe_request(json msgs) {
#if Trace_ParseMessage
    std::clog << " (TYPE_ID_SUBSCRIBE)" << std::endl;
#endif
  }

  /// <summary>
  ///
  /// </summary>
  /// <param name="msg"></param>
  /// <remarks>
  /// [ TYPE_ID_UNSUBSCRIBE, topicURI ].
  /// </remarks>
  void handle_unsubscribe_request(json msg) {
#if Trace_ParseMessage
    std::clog << " (TYPE_ID_UNSUBSCRIBE)" << std::endl;
#endif
  }

  /// <summary>
  /// Handle the welcome message.
  /// </summary>
  /// <param name="args">A <see cref="json::array"/> contains the parsed message.</param>
  /// <remarks>
  /// The json message format is [ TYPE_ID_WELCOME , sessionId , protocolVersion, serverIdent ].
  /// </remarks>
  void handle_welcome_message(json msg) {
#if Trace_ParseMessage
    std::clog << " (TYPE_ID_WELCOME)" << std::endl;
#endif
    if (msg[2].get<uint64_t>() != 1) {
      return;
    }
    m_sessionId = std::move(msg[1].get<std::string>());
    msg.erase(msg.begin(), msg.begin());
    static_cast<T *>(this)->on_connect(std::move(msg));
  }

  void handle_close(json msg) {
  }

  /// <summary>
  /// Is invoked when a connexion is made to the server
  /// </summary>
  /// <param name="arg">A <see cref="json::element"/> containing the server identity.</param>
  void on_connect(json msg) {
  }

private:
  std::string m_sessionId;
  wamp::prefix m_prefixes;

  // Function template to workaround using move-only types in a lambda stored in a function<>.
  // See https://stackoverflow.com/a/25478459
  template <class F> struct shared_function {
    std::shared_ptr<F> f;
    shared_function(F &&f_) : f(std::make_shared<F>(std::move(f_))) {
    }
    template <class... As> auto operator()(As &&... as) const {
      return (*f)(std::forward<As>(as)...);
    }
  };

  template <class F> shared_function<std::decay_t<F>> make_shared_function(F &&f) {
    return {std::forward<F>(f)};
  }

  // CPromiseManager is a map (of outstanding calls and the function to invoke when they complete)
  // with an associated mutex
  template <class then_>
  class CPromiseManager : public std::map<const std::string, shared_function<then_>> {
    typedef std::map<const std::string, shared_function<then_>> base_type;

  public:
    std::mutex m_mutex;
  };

  CPromiseManager<then_t> m_promises;
}; // namespace client
} // namespace client
} // namespace wamp
} // namespace tdx
#endif // wamp_session_HPP_INCLUDED_
