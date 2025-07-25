#ifndef base64_HPP_INCLUDED_
#define base64_HPP_INCLUDED_
// <copyright file="base64.hpp" company="3Dconnexion">
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
// $Id: base64.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 07/29/20 MSB Adapted from https://stackoverflow.com/a/60580965.
// </history>

#include <openssl/evp.h>

// stdlib
#include <string>
#include <iostream>

namespace tdx {
/// <summary>
/// Encode a <see cref="uint8_t"/> array to base64.
/// </summary>
/// <param name="data">The <see cref="uint8_t"/> array containing the data to encode.</param>
/// <param name="size">The size of the <see cref="uint8_t"/> array.</param>
/// <returns>A <see cref="sid::string"/> containing the base64 encoded data.</returns>
inline std::string base64(const uint8_t data[], size_t size) {
  std::string result;

  // Resize to encoded string + null terminator;
  result.resize((4 * ((size + 2) / 3)) + 1);

  int encoded = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&result[0]), data, static_cast<int>(size));

  // remove the null terminator
  result.resize(encoded);

  return result;
}
} // namespace tdx
#endif // base64_HPP_INCLUDED_