#ifndef uniqueid_HPP_INCLUDED_
#define uniqueid_HPP_INCLUDED_
// <copyright file="uniqueid.hpp" company="3Dconnexion">
// ------------------------------------------------------------------------------------------------
// Copyright (c) 2014-2021 3Dconnexion. All rights reserved.
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
// $Id$
//
// 01/22/14 MSB Initial design.
// </history>

// stdlib
#include <string>
#include <boost/random.hpp>
#include <boost/random/random_device.hpp>

namespace tdx {
namespace wamp {
inline std::string make_uniqueid(size_t length=16) {
  std::string id;
  static std::string chars("abcdefghijklmnopqrstuvwxyz"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "1234567890"
                    // Only allow alphanumeric characters
                    //    "!@#$%^&*()"
                    //    "`~-_=+[{]{\\|;:'\",<.>/? "
  );

  boost::random::random_device rng;
  boost::random::uniform_int_distribution<size_t> index_dist(0, chars.size() - 1);

  size_t i;
  for (i = 0; i < length; ++i) {
    char alpha = chars[index_dist(rng)];
    id += alpha;
  }
  return id;
}
} // namespace wamp
} // namespace tdx
#endif // uniqueid_HPP_INCLUDED_