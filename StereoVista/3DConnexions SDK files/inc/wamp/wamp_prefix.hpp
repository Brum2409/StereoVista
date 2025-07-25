#ifndef wamp_prefix_HPP_INCLUDED_
#define wamp_prefix_HPP_INCLUDED_
// <copyright file="wamp_prefix.hpp" company="3Dconnexion">
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
// $Id: wamp_prefix.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// </history>

#include "json.hpp"
#include "websocket_client_session.hpp"

// stdlib
#include <future>
#include <map>
#include <string>

// navlib
#include <navlib/navlib_types.h>

namespace tdx {
namespace wamp {
/// <summary>
/// Represents an active Wamp prefix.
/// </summary>
class prefix : public std::map<std::string, std::string> {
public:
  /// <summary>
  /// Converts a curie to a uri
  /// </summary>
  /// <param name="curie">The curie to convert.</param>
  /// <returns>Returns the uri or the original curie if a prefix did not exist.</returns>
  std::string ToUri(const std::string &curie) const {
    // curie is either an uri or a curie
    std::string uri;
    std::string::size_type pos = curie.find(':');

    if (pos != std::string::npos) {
      // Looks like a curie
      const_iterator iter = find(curie.substr(0, pos));
      if (iter != end()) {
        uri = iter->second;
        if (uri.back() != '#' && uri.back() != '/') {
          uri += "/";
        }
        uri += curie.substr(pos + 1);
        return uri;
      }
    }
    uri = curie;
    return uri;
  }

  /// <summary>
  /// Extracts the host uri from a curie
  /// </summary>
  /// <param name="uri">The uri or curie</param>
  /// <returns>The host uri or the original.</returns>
  std::string GetHostUri(const std::string &uri) const {
    // uri is either an uri or a curie
    std::string::size_type pos = uri.find(':');
    if (pos != std::string::npos) {
      // Looks like a curie
      const_iterator iter = find(uri.substr(0, pos));
      if (iter != end()) {
        return iter->second;
      }
    }

    pos = uri.find('#');
    if (pos != std::string::npos) {
      return uri.substr(0, pos);
    }

    // something is wrong return the original uri
    return uri;
  }
};
} // namespace wamp
} // namespace tdx
#endif // wamp_prefix_HPP_INCLUDED_
