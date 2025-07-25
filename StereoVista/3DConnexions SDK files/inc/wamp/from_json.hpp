#ifndef from_json_HPP_INCLUDED_
#define from_json_HPP_INCLUDED_
// <copyright file="from_json.hpp" company="3Dconnexion">
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
// $Id: from_json.hpp 17426 2020-05-07 06:56:31Z mbonk $
//
// 12/10/13 MSB Initial design
// </history>

#include "json.hpp"

#include <navlib/navlib_types.h>

#include <iostream>

namespace navlib {
/// <summary>
/// Convert a <see cref="json"/> to an array.
/// </summary>
/// <param name="type">The <see cref="navlib::propertyType_t"/>.</param>
/// <param name="v">The <see cref="json::value"/> to convert.</param>
/// <returns>A <see cref="navlib::value_t"/>.</returns>
template <typename T, size_t N>
void from_json(const tdx::json &j, T (&t)[N]) // namespace navlib
{
  j.get_to(t);
}

inline void from_json(const tdx::json &j, navlib::vector_t &v) {
  j.get_to(v.components);
}

inline void from_json(const tdx::json &j, navlib::point_t &p) {
  j.get_to(p.coordinates);
}

inline void from_json(const tdx::json &j, navlib::box_t &b) {
  j.get_to(b.b);
}

inline void from_json(const tdx::json &j, navlib::matrix_t &m) {
  j.get_to(m.m);
}

inline void from_json(const tdx::json &j, navlib::plane_t &p) {
  j.get_to(p.equation);
}

inline void from_json(const tdx::json &j, navlib::frustum_t &f) {
  j.get_to(f.parameters);
}

inline navlib::value_t from_json(const tdx::json &j, navlib::propertyType_t type) {
  using namespace navlib;
  value_t value;
  try {
    switch (j.type()) {
    case tdx::json::value_t::array: {
      switch (type) {
      case point_type:
        if (j.size() == sizeof(((point_t *)nullptr)->coordinates) /
                            sizeof(((point_t *)nullptr)->coordinates[0])) {
          j.get_to(value.point.coordinates);
          value.type = type;
        }
        break;

      case vector_type:
        if (j.size() == sizeof(((vector_t *)nullptr)->components) /
                            sizeof(((vector_t *)nullptr)->components[0])) {
          j.get_to(value.vector.components);
          value.type = type;
        }
        break;

      case matrix_type:
        if (j.size() == sizeof(((matrix_t *)nullptr)->m) / sizeof(((matrix_t *)nullptr)->m[0])) {
          j.get_to(value.matrix.m);
          value.type = type;
        }
        break;

      case plane_type:
        if (j.size() ==
            sizeof(((plane_t *)nullptr)->equation) / sizeof(((plane_t *)nullptr)->equation[0])) {
          j.get_to(value.plane.equation);
          value.type = type;
        }
        break;

      case box_type:
        if (j.size() == sizeof(((box_t *)nullptr)->b) / sizeof(((box_t *)nullptr)->b[0])) {
          j.get_to(value.box.b);
          value.type = type;
        }
        break;

      case frustum_type:
        if (j.size() == sizeof(((frustum_t *)nullptr)->parameters) /
                            sizeof(((frustum_t *)nullptr)->parameters[0])) {
          j.get_to(value.frustum.parameters);
          value.type = type;
        }
        break;

      default:
        throw std::runtime_error("unknown array type");
        break;
      }
    } break;

    case tdx::json::value_t::boolean:
      value = j.get<bool>();
      break;

    case tdx::json::value_t::number_unsigned:
    case tdx::json::value_t::number_integer:
      value = j.get<long>();
      break;

    case tdx::json::value_t::number_float:
      value = j.get<double>();
      break;

    case tdx::json::value_t::null:
      value = (void *)0;
      break;

    case tdx::json::value_t::string:
      value = j.get<std::string>();
      break;

    case tdx::json::value_t::object: {
      value = from_json(j.at("value"),
                        static_cast<navlib::propertyType_t>(j.at("type").get<int64_t>()));
    } break;

    default:
      break;
    }
  }
#if _DEBUG
  catch (std::runtime_error &e) {
    std::cerr << "std::runtime_error json_to_variant " << e.what() << std::endl;
  }
#else
  catch (std::runtime_error &) {
  }
#endif

  return value;
}
} // namespace navlib

void from_json(const tdx::json &j, std::vector<std::unique_ptr<SiActionNodeEx_t>> &tree);
inline void from_json_array(const tdx::json &j,
                            std::vector<std::unique_ptr<SiActionNodeEx_t>> &tree) {
  SiActionNodeEx_t *next = nullptr;
  auto size = tree.size();
  for (auto iter = j.crbegin(); iter != j.crend(); ++iter) {
    from_json(*iter, tree);

    if (size != tree.size()) {
      auto pnode = tree.back().get();
      pnode->next = next;
      next = pnode;
    }
  }
}

inline void from_json(const tdx::json &j, std::vector<std::unique_ptr<SiActionNodeEx_t>> &tree) {
  if (j.empty()) {
    return;
  }

  if (j.is_array()) {
    return from_json_array(j, tree);
  }

  auto size = tree.size();
  from_json_array(j.at("children"), tree);

  SiActionNodeEx_t node = {j.at("size").get<uint32_t>(),
                           static_cast<SiActionNodeType_t>(j.at("type").get<uint32_t>()),
                           nullptr,
                           nullptr,
                           j.at("id").get_ptr<const std::string *>()->c_str(),
                           j.at("label").get_ptr<const std::string *>()->c_str(),
                           j.at("description").get_ptr<const std::string *>()->c_str()};

  node.children = size == tree.size() ? nullptr : tree.back().get();
  tree.emplace_back(std::make_unique<SiActionNodeEx_t>(std::move(node)));
}

#endif // json_HPP_INCLUDED_