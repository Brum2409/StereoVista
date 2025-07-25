#ifndef navlib_client_session_HPP_INCLUDED_
#define navlib_client_session_HPP_INCLUDED_
// <copyright file="navlib_error.h" company="3Dconnexion">
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
// *************************************************************************************************
// File History
//
// $Id$
//
// 07/06/20 MSB Initial design.
// </history>
// <description>
// *************************************************************************************************
// File Description
//
// This header file defines the classes used for the navlib wamp client session.
//
// *************************************************************************************************
// </description>
#include "from_json.hpp"
#include "https_get.hpp"
#include "to_json.hpp"
#include "uniqueid.hpp"
#include "wamp.hpp"
#include "wamp_client_session.hpp"

#include <navlib/navlib.h>
#include <navlib/navlib_error.h>

// stdlib
#include <functional>
#include <future>
#include <string>

namespace tdx {
namespace wamp {
namespace client {
#define TDX_NL_VERSION 0.6
#define TDX_NL_MICRO 1
static const std::string version = "TDX_NL_VERSION.TDX_NL_MICRO";
static const std::string tdx_controller = "3dconnexion:3dcontroller";
static const std::string tdx_controller_uri = "3dconnexion:3dcontroller/";

class NlSession : public wamp_session<NlSession> {
  typedef wamp_session<NlSession> base_type;

public:
  NlSession(ssl::context &ctx) : wamp_session(ctx) {
  }

  bool is_running() {
    if (m_session_state.valid() && m_session_state.wait_for(std::chrono::nanoseconds::zero()) ==
                                        std::future_status::timeout) {
      return true;
    }

    return false;
  }

  void run(std::error_code ec) {
    if (is_running()) {
      ec = navlib::make_error_code(navlib::navlib_errc::already_connected);
      return;
    }

    m_host = "127.51.68.120";
    std::string resource = "/3dconnexion/nlproxy";
    std::string port = "8181";

    beast::error_code bec;
    std::string message = ::ws::https_get(m_host, port, resource, bec);
    ec = bec;
    if (ec) {
      return;
    }

    std::string ws_port = "8182";
    try {
      tdx::json j = tdx::json::parse(std::move(message));
      ws_port = std::to_string(j["port"].get<uint64_t>());
    }
    catch (const tdx::json::exception& e) {
      std::cerr << "HTTP response is invalid JSON: " << e.id << ", " << e.what() << std::endl;
      ec = navlib::make_error_code(navlib::navlib_errc::error);
    }

    run(m_host.c_str(), ws_port.c_str());

    // Wait until the session is initialized
    if (is_running()) {
      std::unique_lock<std::mutex> lock(m_cv_m);
      while (m_connexion.empty() && is_running()) {
        m_cv.wait_for(lock, std::chrono::milliseconds(10));
      }
    }

    if (!is_running()) {
      navlib::make_error_code(navlib::navlib_errc::error);
      return;
    }
    return;
  }

  /// <summary>
  /// Wait until the session ends.
  /// </summary>
  /// <returns></returns>
  void wait() {
    return m_session_state.wait();
  }

  /// <summary>
  /// Creates a new navigation instance.
  /// </summary>
  /// <remarks>The client specifies the name of the instance and the properties that are available
  /// for querying and updating by the navigation framework.</remarks>
  /// <param name="hdl">A <see cref="nlHandle_t"/> for the new navigation instance.</param>
  /// <param name="appname">The name of the application.</param>
  /// <param name="property_accessors">An array of <see cref="accessor_t"/> structures containing
  /// the property name, accessor and mutator functions that the client exposes to the navigation
  /// instance.</param>
  /// <param name="accessor_count">The number of <see cref="accessor_t"/> entries passed in the
  /// property_accessors parameter.</param>
  /// <param name="options">Pointer to a <see cref="nlCreateOptions_t"/>. This parameter is optional
  /// and may be null.</param>
  /// <returns>0 on success or a navlib error, see <see cref="navlib_errc::navlib_errc_t"/> and
  /// <see cref="make_result_code"/>.</returns>
  long NlCreate(navlib::nlHandle_t& hdl, const char *appname,
                const navlib::accessor_t property_accessors[], size_t accessor_count,
                const navlib::nlCreateOptions_t *options) {

    // Create the navlib connexion
    std::string callId = wamp::make_uniqueid();
    navlib::nlOptions_t config = options != nullptr ? options->options : navlib::nlOptions_t{};

    json info = {{"version", TDX_NL_VERSION},
                 {"name", appname},
                 {"rowMajorOrder", (config & navlib::nlOptions_t::row_major_order) != 0}};

    std::string message =
        wamp::make_call(callId, "3dx_rpc:create", tdx_controller, m_connexion, info);
    std::promise<uint64_t> ph;
    auto h = ph.get_future();

    send_call(std::move(callId), std::move(message),
              [this, p = std::move(ph)](std::error_code ec, json result) mutable -> void {
                try {
                  if (ec) {
                    fail(ec, std::move(result));
                    throw std::system_error(ec);
                  }
                  uint64_t hdl_ = result["instance"].get<uint64_t>();

                  // Subscribe to the events.
                  websocket_session::send(
                      wamp::make_subscribe(tdx_controller_uri + std::to_string(hdl_)), ws::text);

                  p.set_value(hdl_);
                }

                catch (...) {
                  try {
                    // store anything thrown in the promise
                    p.set_exception(std::current_exception());
                  }
                  catch (...) {
                  } // set_exception() may throw too
                }
              });

    try {
      hdl = h.get();
    }
    catch (const std::future_error &e) {
      return e.code().value();
    }
    catch (const std::system_error &e) {
      return e.code().value();
    }
    catch (const json::exception &e) {
      return e.id;
    }
    catch (...) {
      return navlib::make_result_code(navlib::navlib_errc::error);
    }

    if (hdl != INVALID_NAVLIB_HANDLE) {
      m_accessors.emplace(
          std::make_pair(hdl, std::vector<navlib::accessor_t>(
                                   property_accessors, &property_accessors[accessor_count])));
    }

    return 0;
  }

  /// <summary>
  /// Closes an open navigation instance handle and destroys the navigation instance.
  /// </summary>
  /// <param name="nh">A valid <see cref="nlHandle_t"/> of an open navigation instance.</param>
  /// <returns>0 if the function succeeds, otherwise a navlib error, see
  /// <see cref="navlib_errc::navlib_errc_t"/> and <see cref="make_result_code"/>.</returns>
  long NlClose(navlib::nlHandle_t nh) {
    if (!is_running()) {
      return navlib::make_result_code(navlib::navlib_errc::invalid_operation);
    }

    std::string callId = wamp::make_uniqueid();
    std::string message =
        wamp::make_call(callId, "3dx_rpc:delete", tdx_controller_uri + std::to_string(nh));

    std::promise<std::error_code> ph;
    auto error = ph.get_future();

    send_call(std::move(callId), std::move(message),
              [this, p = std::move(ph)](std::error_code ec, json result) mutable -> void {
                p.set_value(ec);
              });

    return navlib::make_result_code(error.get().value());
  }

  /// <summary>
  /// Read the value of a property cached in the navlib.
  /// </summary>
  /// <param name="nh">The <see cref="nlHandle_t"/> of the open navigation instance.</param>
  /// <param name="name">The name of the property whose value is being queried.</param>
  /// <param name="value">A <see cref="value_t"/> that contains the property value when the function
  /// returns.</param>
  /// <returns>0 if the function succeeds, otherwise a navlib error, see
  /// <see cref="navlib_errc::navlib_errc_t"/> and <see cref="make_result_code"/>.</returns>
  long NlReadValue(navlib::nlHandle_t nh, navlib::property_t name, navlib::value_t& value) {
    std::string callId = wamp::make_uniqueid();
    std::string message = wamp::make_call(
        callId, "3dx_rpc:read", tdx_controller_uri + std::to_string(nh), std::string(name));

    std::promise<std::error_code> ph;
    auto error = ph.get_future();

    send_call(std::move(callId), std::move(message),
              [this, p = std::move(ph), &value,
               type = NlGetType(name)](std::error_code ec, json result) mutable -> void {
                try {
                  if (ec) {
                    fail(ec, std::move(result));
                  }
                  else {
                    value = from_json(result, type);
                  }
                  p.set_value(ec);
                }

                catch (...) {
                  try {
                    // store anything thrown in the promise
                    p.set_exception(std::current_exception());
                  }
                  catch (...) {
                  } // set_exception() may throw too
                }
              });

    return navlib::make_result_code(error.get().value());
  }

  /// <summary>
  /// Write the value for a property to the navlib.
  /// </summary>
  /// <param name="nh">The <see cref="nlHandle_t"/> of the open navigation instance.</param>
  /// <param name="name">The name of the property whose value is to be written.</param>
  /// <param name="value">A pointer to a <see cref="value_t"/> that contains the new property
  /// value.</param>
  /// <returns>0 if the function succeeds, otherwise a navlib error, see
  /// <see cref="navlib_errc::navlib_errc_t"/> and <see cref="make_result_code"/>.</returns>
  long NlWriteValue(navlib::nlHandle_t nh, navlib::property_t name, navlib::value_t const &value) {
    tdx::json j;
    j[name] = to_json(value);

    std::string callId = wamp::make_uniqueid();
    std::string message =
        wamp::make_call(callId, "3dx_rpc:update", tdx_controller_uri + std::to_string(nh), j);
    send_call(std::move(callId), std::move(message),
              [this](std::error_code ec, json result) mutable -> void {
                try {
                  if (ec) {
                    fail(ec, std::move(result));
                  }
                }
                catch (...) {
                }
              });

    return 0;
  }

  /// <summary>
  /// Query the type of a navlib property.
  /// </summary>
  /// <param name="name">The name of the property whose type is to be queried.</param>
  /// <returns>One of the <see cref="propertyTypes"/> values.</returns>
  navlib::propertyType_t NlGetType(navlib::property_t name) {
    using navlib::propertyDescription;
    try {
      std::string strProperty(name);
      size_t i;
      for (i = 0; i < sizeof(propertyDescription) / sizeof(navlib::propertyDescription[0]); ++i) {
        if (strProperty == propertyDescription[i].name)
          return propertyDescription[i].type;
      }

      std::string::size_type pos = strProperty.rfind(".");
      std::string objectType(strProperty.substr(pos + 1));
      for (i = 0; i < sizeof(propertyDescription) / sizeof(propertyDescription[0]); ++i) {
        if (objectType == propertyDescription[i].name)
          return propertyDescription[i].type;
      }
    }
    catch (std::exception &) {
    }
    return navlib::auto_type;
  }

  /// <summary>
  /// Run the NlSession session.
  /// </summary>
  /// <param name="host">A string identifying a location. May be a descriptive name or a numeric
  /// address string. If an empty string is specified, the resolved endpoints will use the loopback
  /// address.</ param>
  /// <param name="port">A string identifying the requested service. This may be a descriptive name
  /// or a numeric string corresponding to a port number.</ param>
  /// <remarks>
  /// The m_session <see cref="std::future"/> will contain the <see cref"websocket_session"/>'s
  /// <see cref="beast::error_code"/> when the session ends.
  /// </remarks>
  void run(const char *host, const char *port) {
    // Set a decorator to change the user agent of the handshake and the secondary protocol.
    set_option(websocket::stream_base::decorator([](websocket::request_type &req) {
      req.set(http::field::user_agent, std::string(CLIENT_VERSION_STRING) + " wamp-nlclient");
    }));

    // Check whether the session is already running
    if (!m_session_state.valid()) {
      m_host = host;
      m_session_state = base_type::run(host, port);
    }
  }

  /// <summary>
  /// Invoked when a connection is established.
  /// </summary>
  /// <param name="arg"></param>
  /// The json message format is [ sessionId , protocolVersion, serverIdent ].
  void on_connect(json arg) {
    websocket_session::send(wamp::make_prefix("self", "wss://" + m_host + "/3dconnexion#"),
                            tdx::ws::text);
    websocket_session::send(wamp::make_prefix("3dx_rpc", "wss://" + m_host + "/3dconnexion#"),
                            tdx::ws::text);
    websocket_session::send(wamp::make_prefix("3dconnexion", "wss://" + m_host + "/3dconnexion/"),
                            tdx::ws::text);
    websocket_session::send(
        wamp::make_prefix("wamp-error", "wss://" + m_host + "/3dconnexion/error#"), tdx::ws::text);
    websocket_session::send(
        wamp::make_prefix("navlib-error", "wss://" + m_host + "/3dconnexion/3dmouse/error#"),
        tdx::ws::text);

    // Create the 3dmouse
    std::string callId = wamp::make_uniqueid();
    std::string message =
        wamp::make_call(callId, "3dx_rpc:create", "3dconnexion:3dmouse", TDX_NL_VERSION);

    send_call(std::move(callId), std::move(message), [this](std::error_code ec, json result) {
      if (ec) {
        return fail(ec, std::move(result));
      }
      try {
        std::unique_lock<std::mutex> lock(m_cv_m);
        m_connexion = result["connexion"].get<std::string>();
      }
      catch (const json::exception &e) {
        return fail(e);
      }
      m_cv.notify_all();
    });
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
    std::error_code ec = wamp::make_error_code(wamp::error::invalid_format);

    const std::string &topicURI = msg[1].get_ref<std::string &>();

    size_t pos = topicURI.rfind(tdx_controller_uri);
    if (pos == std::string::npos) {
      return;
    }

    try {
      navlib::nlHandle_t hdl = std::stoll(topicURI.substr(pos + tdx_controller_uri.length()));
      const tdx::json &wamp_message = msg[2].get_ref<tdx::json::array_t &>();

      int64_t opcode = wamp_message[0].get<int64_t>();
      if (opcode == wamp::TYPE_ID_CALL) {
        ec = wamp::make_error_code(wamp::error::unknown_method);
        try {
          const std::string &method = wamp_message[2].get_ref<const std::string &>();
          if (method == "self:read") {
            return do_read(hdl, wamp_message);
          }
          else if (method == "self:update") {
            return do_update(hdl, wamp_message);
          }
        }
        catch (const std::runtime_error&) {
          ec = std::make_error_code(std::errc::invalid_argument);
        }
        catch (...) {
          ec = wamp::make_error_code(wamp::error::invalid_format);
        }
        std::string errorUri = "wamp-error:" + std::to_string(ec.value());
        return websocket_session::send(
            wamp::make_callerror(wamp_message[1].get<std::string>(), errorUri, ec.message()),
            ws::text);
      }
    }
    catch (const tdx::json::exception &e) {
      return fail(e);
    }
    catch (...) {
    }

    fail(ec, "handle_event_message");
  }

  /// <summary>
  /// Handle the 3dx_rpc:update remote procedure call.
  /// </summary>
  /// <param name="hdl">The navlib connection handle.</param>
  /// <param name="msg">The <see cref="tdx::json"/> [ TYPE_ID_CALL , callID , procURI , args ]
  /// message.</param>
  void do_read(navlib::nlHandle_t hdl, const tdx::json &msg) {
    const std::string &callId = msg[1].get_ref<const std::string &>();
    navlib::value_t value;
    long result = GetAppValue(hdl, msg[4].get_ref<const std::string &>(), value);

    std::string message;
    if (result) {
      std::string errorUri = "navlib-error:" + std::to_string(result);
      message = wamp::make_callerror(callId, errorUri, navlib_category.message(result));
    }
    else {
      tdx::json j = to_json(value);
      message = wamp::make_callresult(callId, j);
    }

    websocket_session::send(std::move(message), ws::text);
  }

  /// <summary>
  /// Handle the 3dx_rpc:update remote procedure call.
  /// </summary>
  /// <param name="hdl">The navlib connection handle.</param>
  /// <param name="msg">The <see cref="tdx::json"/> [ TYPE_ID_CALL , callID , procURI , args ]
  /// message.</param>
  void do_update(navlib::nlHandle_t hdl, const tdx::json &msg) {
    long result;
    const std::string &callId = msg[1].get_ref<const std::string &>();

    try {
      const std::string &name = msg[4].get_ref<const std::string &>();
      navlib::value_t value = from_json(msg[5], NlGetType(name.c_str()));

      result = SetAppValue(hdl, name, value);
    }
    catch (const std::exception &) {
      result = navlib::make_result_code(navlib::navlib_errc::invalid_argument);
    }

    std::string message;
    if (result) {
      std::string errorUri = "navlib-error:" + std::to_string(result);
      message = wamp::make_callerror(callId, errorUri, navlib_category.message(result));
    }
    else {
      tdx::json j = result;
      message = wamp::make_callresult(callId, j);
    }

    websocket_session::send(std::move(message), ws::text);
  }

  /// <summary>
  /// Report errors to std::cerr.
  /// </summary>
  /// <param name="ec">The <see cref="std::error_code"/> to report.</param>
  /// <param name="what">The <see cref="json::array"/> error message received .</param>
  void fail(std::error_code ec, const json &args) {
    for (size_t i = 1; i < args.size(); ++i) {
      std::cerr << args[i].get<std::string>() << " ";
    }
    std::cerr << ec.message() << "\n";
  }

  /// <summary>
  /// Report errors to std::cerr.
  /// </summary>
  /// <param name="ec">The <see cref="json::exception"/> to report.</param>
  void fail(const json::exception &e) {
    std::cerr << "json exception id=" << e.id << " : " << e.what() << "\n";
  }

  /// <summary>
  /// Report errors to std::cerr.
  /// </summary>
  /// <param name="ec">The <see cref="std::error_code"/> to report.</param>
  /// <param name="what">The operation that produced the <see cref="std::error_code"/>.</param>
  void fail(std::error_code ec, char const *what) {
    std::cerr << what << ": " << ec.message() << "\n";
  }

  long GetAppValue(navlib::nlHandle_t hdl, const std::string &propertyName,
                   navlib::value_t &propertyValue) {
    long error = navlib::make_result_code(navlib::navlib_errc::invalid_operation);

    auto pair = m_accessors.find(hdl);
    if (pair != m_accessors.end()) {
      auto iter = std::find_if(
          pair->second.cbegin(), pair->second.cend(),
          [&](const navlib::accessor_t &c) { return propertyName.compare(c.name) == 0; });
      if (iter != pair->second.cend() && iter->fnGet != nullptr) {
        try {
          error = iter->fnGet(iter->param, propertyName.c_str(), &propertyValue);
        }
        catch (const std::exception &) {
          error = navlib::make_result_code(navlib::navlib_errc::error);
        }
      }
      else {
        error = navlib::make_result_code(navlib::navlib_errc::property_not_found);
      }
    }

    return error;
  }

  long SetAppValue(navlib::nlHandle_t hdl, const std::string &propertyName,
                   const navlib::value_t &propertyValue) {

    long error = navlib::make_result_code(navlib::navlib_errc::invalid_operation);

    auto pair = m_accessors.find(hdl);
    if (pair != m_accessors.end()) {
      auto iter = std::find_if(
          pair->second.cbegin(), pair->second.cend(),
          [&](const navlib::accessor_t &c) { return propertyName.compare(c.name) == 0; });
      if (iter != pair->second.cend() && iter->fnSet != nullptr) {
        try {
          error = iter->fnSet(iter->param, propertyName.c_str(), &propertyValue);
        }
        catch (const std::exception &) {
          error = navlib::make_result_code(navlib::navlib_errc::error);
        }
      }
      else {
        error = navlib::make_result_code(navlib::navlib_errc::property_not_found);
      }
    }

    return error;
  }

private:
  std::string m_connexion;
  std::string m_host;
  std::shared_future<beast::error_code> m_session_state;
  std::map<navlib::nlHandle_t, std::vector<navlib::accessor_t>> m_accessors;
  std::condition_variable m_cv;
  std::mutex m_cv_m;
}; // namespace client
} // namespace client
} // namespace wamp
} // namespace tdx
#endif // navlib_client_session_HPP_INCLUDED_
