#ifndef CMouseButtonEventArgs_HPP_INCLUDED
#define CMouseButtonEventArgs_HPP_INCLUDED
// <copyright file="CMouseButtonEventArgs.hpp" company="3Dconnexion">
// -------------------------------------------------------------------------------------------
// Copyright (c) 2018-2019 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer Kit",
// including all accompanying documentation, and is protected by intellectual property laws.
// All use of the 3Dconnexion Software Developer Kit is subject to the License Agreement found
// in the "LicenseAgreementSDK.txt" file.
// All rights not expressly granted by 3Dconnexion are reserved.
// -------------------------------------------------------------------------------------------
// </copyright>
// <history>
// *******************************************************************************************
// File History
//
// $Id: $
//
// </history>
#include "CEventArgs.hpp"
#include "Point2D.hpp"
namespace TDx {
namespace TraceNL {
namespace Input {
//
// Summary:
//     Defines values that specify the buttons on a mouse device.
enum class MouseButton {
  //
  // Summary:
  //     The left mouse button.
  Left = 0,
  //
  // Summary:
  //     The middle mouse button.
  Middle = 1,
  //
  // Summary:
  //     The right mouse button.
  Right = 2,
  //
  // Summary:
  //     The first extended mouse button.
  XButton1 = 3,
  //
  // Summary:
  //     The second extended mouse button.
  XButton2 = 4
};

//
// Summary:
//     Specifies the possible states of a mouse button.
enum class MouseButtonState {
  //
  // Summary:
  //     The button is released.
  Released = 0,
  //
  // Summary:
  //     The button is pressed.
  Pressed = 1
};

/// <summary>
/// Provides data for mouse button related events.
/// </summary>
class CMouseButtonEventArgs : public Navigation::CEventArgs {
public:
  //
  // Summary:
  //     Initializes a new instance of the CMouseButtonEventArgs class
  //
  // Parameters:
  //   button:
  //     The mouse button whose state is being described.
  //
  //   state:
  //     The state of the button
  //
  //   position:
  //     The position of the mouse
  CMouseButtonEventArgs(MouseButton button, MouseButtonState state, Media2D::Point2D position)
      : m_button(button), m_state(state), m_position(position) {}

#if defined(_MSC_EXTENSIONS)
  __declspec(property(get = GetChangedButton)) MouseButton ChangedButton;
  __declspec(property(get = GetButtonState)) MouseButtonState ButtonState;
#endif

  //
  // Summary:
  //     Gets the button associated with the event.
  //
  // Returns:
  //     The button which was pressed.
  MouseButton GetChangedButton() const { return m_button; }

  //
  // Summary:
  //     Gets the state of the button associated with the event.
  //
  // Returns:
  //     The state the button is in.
  MouseButtonState GetButtonState() const { return m_state; }

  //
  // Summary:
  //     Gets the position of the mouse associated with this event
  //     relative to the Viewport
  //
  // Returns:
  //     The position of the mouse
  Media2D::Point2D GetPosition() { return m_position; }

private:
  MouseButton m_button;
  MouseButtonState m_state;
  Media2D::Point2D m_position;
};
} // namespace Input
} // namespace TraceNL
} // namespace TDx
#endif // CMouseButtonEventArgs_HPP_INCLUDED