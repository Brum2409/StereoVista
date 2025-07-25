#ifndef wamp_HPP_INCLUDED_
#define wamp_HPP_INCLUDED_
// <copyright file="wamp.hpp" company="3Dconnexion">
// ------------------------------------------------------------------------------------------------
// Copyright (c) 2014-2020 3Dconnexion. All rights reserved.
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
// $Id: wamp.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 01/20/14 MSB Initial design.
// </history>
#include "json.hpp"
#include "to_json.hpp"

// boost
#include <boost/system/error_code.hpp>

// stdlib
#include <string>

namespace tdx {
namespace wamp {
typedef enum messageTypes {
  TYPE_ID_WELCOME = 0,
  TYPE_ID_PREFIX = 1,
  TYPE_ID_CALL = 2,
  TYPE_ID_CALLRESULT = 3,
  TYPE_ID_CALLERROR = 4,
  TYPE_ID_SUBSCRIBE = 5,
  TYPE_ID_UNSUBSCRIBE = 6,
  TYPE_ID_PUBLISH = 7,
  TYPE_ID_EVENT = 8
} messageTypes_t;

const int ProtocolVersion = 1;

template <class T> struct tag_Header {
  int opcode;
  T arg1;
  T arg2;
  T arg3;
};

/// Errors related to wamp
namespace error {
enum value {
  /// <summary>
  /// invalid uri was used.
  /// </summary>
  invalid_uri = 1,

  /// <summary>
  /// unknown method was called.
  /// </summary>
  unknown_method,

  /// <summary>
  /// A function was called in a state that it was illegal to do so.
  /// </summary>
  invalid_state,

  /// <summary>
  /// pass_through from underlying library.
  /// </summary>
  pass_through,

  /// <summary>
  /// the connexion id is not known or invalid.
  /// </summary>
  unknown_connexion,

  /// <summary>
  /// the packet format is invalid.
  /// </summary>
  invalid_format,

  /// <summary>
  /// the type is invalid.
  /// </summary>
  invalid_type,

  /// <summary>
  /// call error.
  /// </summary>
  call_error,
};
}

/// Error category related to wamp
class wamp_category : public std::error_category {
public:
  const char *name() const noexcept override {
    return "websocket.wamp";
  }

  std::string message(int value) const override {
    switch (value) {
    case error::invalid_uri:
      return "URI error";
    case error::unknown_method:
      return "Unknown method error";
    case error::invalid_state:
      return "Invalid state";
    case error::pass_through:
      return "Pass through from underlying library";
    case error::unknown_connexion:
      return "Connexion does not exist";
    case error::invalid_format:
      return "Invalid packet format";
    case error::invalid_type:
      return "Invalid type";
    case error::call_error:
      return "rpc call error";
    default:
      return "Unknown";
    }
  }
};

inline const std::error_category &get_wamp_category() {
  static wamp_category instance;
  return instance;
}

inline std::error_code make_error_code(error::value e) {
  return std::error_code(static_cast<int>(e), get_wamp_category());
}

/// <summary>
/// Builds the PREFIX message (client to server) [ TYPE_ID_PREFIX , prefix , URI ].
/// </summary>
/// <param name="prefix">The shortened CURIE prefix to be registered.</param>
/// <param name="URI"> The full URI to register the prefix to.</param>
/// <returns>The PREFIX message as a JSON string.</returns>
inline std::string make_prefix(std::string prefix, std::string URI) {
  std::string jsonmsg = {"[1,\"" + std::move(prefix) + "\",\"" + std::move(URI) + "\"]"};
  return jsonmsg;
}

/// <summary>
/// Builds the SUBSCRIBE  message (client to server) [ TYPE_ID_SUBSCRIBE  , topicURI ].
/// </summary>
/// <param name="topicURI">The topic URI or CURIE (from PREFIX) to receive published events to the
/// given topic.</param>
/// <returns>The SUBSCRIBE message as a JSON string.</returns>
inline std::string make_subscribe(std::string topicURI) {
  std::string jsonmsg = {"[5,\"" + std::move(topicURI) + "\"]"};
  return jsonmsg;
}

/// <summary>
///  Builds the RPC CALL message (client to server) [ TYPE_ID_CALL , callID , procURI , resourceUri
///  ].
/// </summary>
/// <param name="callId">A unique <see cref="std::string"/> to identify the RPC call.</param>
/// <param name="procUri">A <see cref="std::string"/> that identifies the remote procedure to be
/// called and MUST be a valid URI or CURIE.</param>
/// <param name="resourceUri">A <see cref="std::string"/> that identifies the remote resource.
/// </param>
/// <returns>The CALL message as a JSON string.</returns>
/// <remarks>[TYPE_ID_CALL, callID , procURI , resourceUri ].</remarks>
inline std::string make_call(std::string callId, std::string procUri, std::string resourceUri) {
  std::string jsonmsg = {"[2,\"" + std::move(callId) + "\",\"" + std::move(procUri) + "\",\"" +
                         std::move(resourceUri) + "\"]"};

  return jsonmsg;
}

/// <summary>
/// Builds the RPC CALL message (client to server) [ TYPE_ID_CALL , callID , procURI , … ]
/// </summary>
/// <param name="callId">A unique <see cref="std::string"/> to identify the RPC call.</param>
/// <param name="procUri">A <see cref="std::string"/> that identifies the remote procedure to be
/// called and MUST be a valid URI or CURIE.</param>
/// <param name="resourceUri">A <see cref="std::string"/> that identifies the remote resource.
/// </param>
/// <param name="value">An addition arithmetic argument to be sent with the CALL message.</param>
/// <returns>The CALL message as a JSON string.</returns>
/// <remarks> [TYPE_ID_CALL, callID , procURI , resourceUri, value ]</remarks>
template <typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
std::string make_call(std::string callId, std::string procUri, std::string resourceUri, T value) {
  std::string jsonmsg = {"[2,\"" + std::move(callId) + "\",\"" + std::move(procUri) + "\",\"" +
                         std::move(resourceUri) + "\"," + std::to_string(value) + "]"};

  return jsonmsg;
}

/// <summary>
/// Builds the RPC CALL message (client to server) [ TYPE_ID_CALL , callID , procURI , … ]
/// </summary>
/// <param name="callId">A unique <see cref="std::string"/> to identify the RPC call.</param>
/// <param name="procUri">A <see cref="std::string"/> that identifies the remote procedure to be
/// called and MUST be a valid URI or CURIE.</param>
/// <param name="resourceUri">A <see cref="std::string"/> that identifies the remote resource.
/// </param>
/// <param name="args">Addition arguments to be sent with the CALL message.</param>
/// <returns>The CALL message as a JSON string.</returns>
/// <remarks> [TYPE_ID_CALL, callID , procURI , resourceUri, value ]</remarks>
inline std::string make_call(std::string callId, std::string procUri, std::string resourceUri,
                      const tdx::json &args) {
  std::string jsonmsg = {"[2,\"" + std::move(callId) + "\",\"" + std::move(procUri) + "\",\"" +
                         std::move(resourceUri) + "\"," + args.dump() + "]"};

  return jsonmsg;
}

/// <summary>
/// Builds the RPC CALL message (client to server) [ TYPE_ID_CALL , callID , procURI , … ]
/// </summary>
/// <param name="callId">A unique <see cref="std::string"/> to identify the RPC call.</param>
/// <param name="procUri">A <see cref="std::string"/> that identifies the remote procedure to be
/// called and MUST be a valid URI or CURIE.</param>
/// <param name="resourceUri">A <see cref="std::string"/> that identifies the remote resource.
/// </param>
/// <param name="id">A <see cref="std::string"/> that identifies the connexion id.
/// </param>
/// <param name="args">Addition <see cref="tdx::json"/> arguments to be sent with the CALL message.
/// </param>
/// <returns></returns>
/// <remarks> [TYPE_ID_CALL, callID , procURI , resourceUri, connexion, args ]</remarks>
inline std::string make_call(std::string callId, std::string procUri, std::string resourceUri,
                      std::string id, const tdx::json &j) {
  std::string jsonmsg = {"[2,\"" + std::move(callId) + "\",\"" + std::move(procUri) + "\",\"" +
                         std::move(resourceUri) + "\",\"" + std::move(id) + "\"," +
                         std::move(j.dump()) + "]"};

  return jsonmsg;
}

/// <summary>
/// Builds the RPC CALLRESULT message (server to client) [ TYPE_ID_CALLRESULT , callID , result ],
/// </summary>
/// <param name="callId">The call ID of the matching TYPE_ID_CALL received by the client.</param>
/// <param name="j">The results of the RPC procedure initiated by the CALL.</param>
/// <returns>The CALLRESULT message as a JSON string.</returns>
inline std::string make_callresult(std::string callId, const tdx::json& j = tdx::json()) {
  std::string jsonmsg = {"[3,\"" + std::move(callId) + "\"," + std::move(j.dump()) + "]"};
  return jsonmsg;
}

/// <summary>
/// Builds the RPC CALLERROR message (server to client) [ TYPE_ID_CALLERROR , callID , errorURI ,
/// errorDesc , errorDetails(Optional) ].
/// </summary>
/// <param name="callId">The call ID of the matching TYPE_ID_CALL received by the client.</param>
/// <param name="errorURI">A CURIE or URI identifying the error.</param>
/// <param name="errorDesc">A description of the error that occurred.</param>
/// <param name="errorDetails">Optional. Used to communicate application error details, defined by the error_uri.</param>
/// <returns>The CALLERROR message as a JSON string.</returns>
inline std::string make_callerror(std::string callId, std::string errorURI,
                                  std::string errorDesc, std::string errorDetails=std::string()) {

  std::string jsonmsg = {"[4,\"" + std::move(callId) + "\",\"" + std::move(errorURI) + "\",\"" +
                         std::move(errorDesc) + "\",\"" + std::move(errorDetails) + "\"]"};
  return jsonmsg;
}
} // namespace wamp
} // namespace tdx
#endif // wamp_HPP_INCLUDED_