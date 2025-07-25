#ifndef to_json_HPP_INCLUDED_
#define to_json_HPP_INCLUDED_
// <copyright file="to_json.hpp" company="3Dconnexion">
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
// $Id: to_json.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 12/10/13 MSB Initial design
// </history>

#include "base64.hpp"
#include "json.hpp"

#include <navlib/navlib_types.h>

#include <iostream>

inline tdx::json to_json(struct siResource_s const &resource) {
  tdx::json j;
  j = {{"file_name", resource.file_name},
       {"id", resource.id},
       {"type", resource.type},
       {"index", resource.index}};

  return j;
}

inline tdx::json to_json(struct siImageFile_s const &file) {
  tdx::json j;
  j = {{"file_name", file.file_name}, {"index", file.index}};

  return j;
}

inline tdx::json to_json(struct siImageData_s const &image) {
  tdx::json j;

  j["index"] = image.index;
  j["data"] = tdx::base64(image.data, image.size);

  return j;
}

tdx::json to_json(SiActionNodeEx_t const value[]);
/// <summary>
/// Recursively convert an <see cref="SiActionNodeEx_t"/> branch to json.
/// </summary>
/// <param name="node">The <see cref="SiActionNodeEx_t"/> node.</param>
/// <returns>A <see cref="tdx::json"/> object.</returns>
inline tdx::json to_json(SiActionNodeEx_t const &node) {
  tdx::json j = {{"size", node.size},
                 {"type", node.type},
                 {"children", to_json(node.children)},
                 {"id", node.id ? node.id : ""},
                 {"label", node.label ? node.label : ""},
                 {"description", node.description ? node.description : ""}};
  return j;
}

/// <summary>
/// Convert an <see cref="SiImage_t"/> structure to json.
/// </summary>
/// <param name="node">The <see cref="SiImage_t"/> instance.</param>
/// <returns>A <see cref="tdx::json"/> object.</returns>
inline tdx::json to_json(SiImage_t const &si) {
  tdx::json j = {{"size", si.size}, {"type", si.type}, {"id", si.id ? si.id : ""}};
  switch (si.type) {
  case SiImageType_t::e_none: {
  } break;
  case SiImageType_t::e_image: {
    j["image"] = to_json(si.image);
  } break;

  case SiImageType_t::e_image_file: {
    j["file"] = to_json(si.file);
  } break;

  case SiImageType_t::e_resource_file: {
    j["resource"] = to_json(si.resource);
  } break;
  }

  return j;
}

inline tdx::json to_json(SiActionNodeEx_t const value[]) {
  tdx::json j;
  for (auto iter = value; iter != nullptr; iter = iter->next) {
    j.push_back(to_json(*iter));
  }
  return j;
}

inline void to_json(tdx::json &j, SiActionNodeEx_t const node[]) {
  j = to_json(node);
}

inline void to_json(tdx::json &j, SiImage_t const &siImage) {
  j = to_json(siImage);
}

namespace navlib {
// Forward declaration
tdx::json to_json(navlib::value_t const &value);

template <typename T, size_t N> tdx::json to_json(T const (&t)[N]) {
  tdx::json j = t;
  return j;
}

inline tdx::json to_json(navlib::imagearray_t const &t) {
  tdx::json j;
  for (size_t k = 0; k < t.count; ++k) {
    j.emplace_back(to_json(t.p[k]));
  }

  return j;
}

inline void to_json(tdx::json &j, const navlib::imagearray_t &t) {
  j = to_json(t);
}

inline void to_json(tdx::json &j, const navlib::value_t &v) {
  j = to_json(v);
}

inline void to_json(tdx::json &j, navlib::vector_t const &v) {
  j = to_json(v.components);
}

inline void to_json(tdx::json &j, navlib::point_t const &p) {
  j = to_json(p.coordinates);
}

inline void to_json(tdx::json &j, navlib::box_t const &b) {
  j = to_json(b.b);
}

inline void to_json(tdx::json &j, navlib::matrix_t const &m) {
  j = to_json(m.m);
}

inline void to_json(tdx::json &j, navlib::plane_t const &p) {
  j = to_json(p.equation);
}

inline void to_json(tdx::json &j, navlib::frustum_t const &f) {
  j = to_json(f.parameters);
}

/// <summary>
/// Convert a <see cref="navlib::value_t"/> variant to json.
/// </summary>
/// <param name="value">The <see cref="navlib::value_t"/> variant to convert.</param>
/// <returns>A <see cref="tdx::json"/> object.</returns>
inline tdx::json to_json(navlib::value_t const &value) {
  tdx::json j;
  switch (value.type) {
  case auto_type:
    j = {{"type", auto_type}};
    break;

  case unknown_type:
    j = {{"type", unknown_type}};
    break;

  case voidptr_type:
    j = {{"type", voidptr_type}, {"value", reinterpret_cast<int64_t>(value.p)}};
    break;

  case bool_type:
    j = static_cast<bool>(value);
    break;

  case long_type:
    j = static_cast<long>(value);
    break;

  case float_type:
    j = static_cast<float>(value);
    break;

  case double_type:
    j = static_cast<double>(value);
    break;

  case point_type:
    j = static_cast<point_t>(value);
    break;

  case vector_type:
    j = static_cast<vector_t>(value);
    break;

  case matrix_type:
    j = static_cast<matrix_t>(value);
    break;

  case string_type:
    j = static_cast<const char *>(value);
    break;

  case plane_type:
    j = static_cast<plane_t>(value);
    break;

  case box_type:
    j = static_cast<box_t>(value);
    break;

  case frustum_type:
    j = static_cast<frustum_t>(value);
    break;

  case cstr_type:
    j = static_cast<const char *>(value);
    break;

  case actionnodeexptr_type:
    j = static_cast<const SiActionNodeEx_t *>(value);
    break;

  case imagearray_type:
    j = static_cast<const imagearray_t>(value);
    break;
  }

  return j;
}
} // namespace navlib

#endif // json_HPP_INCLUDED_