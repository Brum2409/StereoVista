#ifndef IActionAccessors_HPP_INCLUDED
#define IActionAccessors_HPP_INCLUDED
// <copyright file="IActionAccessors.hpp" company="3Dconnexion">
// ------------------------------------------------------------------------------------------------
// Copyright (c) 2018-2022 3Dconnexion. All rights reserved.
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
// </history>

#include <SpaceMouse/IEvents.hpp>

namespace TDx {
namespace SpaceMouse {
namespace ActionInput {
/// <summary>
/// The accessor interface to the client action input properties.
/// </summary>
class IActionAccessors : public Navigation3D::IEvents {
};
} // namespace ActionInput
} // namespace SpaceMouse
} // namespace TDx
#endif // IActionAccessors_HPP_INCLUDED
