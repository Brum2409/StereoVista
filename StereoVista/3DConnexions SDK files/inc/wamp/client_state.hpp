#ifndef client_state_HPP_INCLUDED_
#define client_state_HPP_INCLUDED_
// <copyright file="client_state.hpp" company="3Dconnexion">
// ------------------------------------------------------------------------------------------------
// Copyright (c) 2015-2020 3Dconnexion. All rights reserved.
//
// This file and source code is COMPANY CONFIDENTIAL material. Permission to either use, copy,
// modify, or distribute this software must be granted by 3Dconnexion before any such action takes
// place.
// ------------------------------------------------------------------------------------------------
// </copyright>
// <history>
// ************************************************************************************************
// File History
//
// $Id: server_state.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 02/12/20 MSB Adapted from beast\example\websocket\server\chat-multi\shared_state.hpp Copyright
//              (c) 2016-2019 Vinnie Falco.  http://www.boost.org/LICENSE_1_0.txt.
// </history>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#define CLIENT_STRING "3DxNLClient"
namespace ws {
typedef std::weak_ptr<void> connection_hdl_t;

// Forward declaration
class websocket_session;

/// <summary>
/// Represents the shared client state.
/// </summary>
class client_state {
public:
  explicit client_state(uint16_t port)
      :m_port(port) {
  }

  uint16_t port() const noexcept {
    return m_port;
  }

  void port(uint16_t port) noexcept {
    m_port = port;
  }

  /// <summary>
  /// Insert a session into to the session container.
  /// </summary>
  /// <param name="session">The <see cref="websocket_session"/> to insert.</param>
  void insert(websocket_session *session_sysex);

  /// <summary>
  /// Erase a session from the session container.
  /// </summary>
  /// <param name="session">The <see cref="websocket_session"/> to erase.</param>
  void erase(websocket_session *session_sysex);

private:
  /// <summary>
  /// The port used for the websocket session;
  /// </summary>
  uint16_t m_port;

  /// <summary>
  /// This mutex synchronizes all access to the sessions.
  /// </summary>
  std::mutex m_mutex;

  /// <summary>
  /// Keep a list of all the connected clients.
  /// </summary>
  std::unordered_set<websocket_session *> m_sessions;
};
} // namespace ws
#endif // client_state_HPP_INCLUDED_
