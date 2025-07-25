#ifndef CApplication3D_HPP_INCLUDED
#define CApplication3D_HPP_INCLUDED
// <copyright file="CApplication3D.hpp" company="3Dconnexion">
// -------------------------------------------------------------------------------------------
// Copyright (c) 2018-2021 3Dconnexion. All rights reserved.
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
#include "CNavigationModel.hpp"
#include "CViewport3D.hpp"
#include "CViewportViewModel.hpp"

// stdlib
#include <condition_variable>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace TDx {
/// <summary>
/// Contains the code specific to the 3DxTraceNL sample.
/// </summary>
namespace TraceNL {
/// <summary>
/// Class that represents the 3D application.
/// </summary>
class CApplication3D {
public:
  /// <summary>
  /// Initializes a new instance of the CApplication3D class.
  /// </summary>
  CApplication3D();

  /// <summary>
  /// The application's main execution loop.
  /// </summary>
  void Run();

private:
  /// <summary>
  /// Displays information about the application.
  /// </summary>
  void About();
  /// <summary>
  /// Close the current 3D model
  /// </summary>
  void CloseFile();
  /// <summary>
  /// Exits the application.
  /// </summary>
  void Exit();
  /// <summary>
  /// Open a 3D model file.
  /// </summary>
  void OpenFile();
  /// <summary>
  /// Save the 3D model.
  /// </summary>
  void SaveFile();

private:
  /// <summary>
  /// Handle the ExecuteCommand event.
  /// </summary>
  /// <param name="args">A <see cref="CCommandEventArgs"/> that contains the data for the command to invoke.</param>
  void ExecuteCommandHandler(Navigation::CCommandEventArgs &args);
  //
  // Summary:
  //    Handle the KeyDown event
  //
  void KeyDownHandler(Navigation::CKeyEventArgs &args);
  //
  // Summary:
  //    Handle the KeyUp event
  //
  void KeyUpHandler(Navigation::CKeyEventArgs &args);
  //
  // Summary:
  //    Handle the SettingsChangedHandler event
  //
  void SettingsChangedHandler();
  //
  // Summary:
  //    Exports the application commands to the 3Dconnexion Settings
  //    Configuration Utility.
  //
  void ExportApplicationCommands();
  //
  // Summary:
  //    Exports the images for the commands to the 3Dconnexion Settings
  //    Configuration Utility.
  //
  void ExportCommandImages();
  //
  // Summary:
  //    Disables the navigation model
  //
  void Disable3DNavigation() {
    try {
      m_navigationModel.EnableNavigation(false);
    } catch (const std::exception &) {
      // Something went wrong
    }
  }
  /// <summary>
  /// Initializes the navigation model instance.
  /// </summary>
  void Enable3DNavigation();

private:
  bool m_exit = false;
  std::mutex m_cv_m;
  std::condition_variable m_cv;

private:
  ViewModels::CViewportViewModel m_viewportViewModel;
  Navigation::CNavigationModel m_navigationModel;

  std::shared_ptr<Media3D::CViewport3D> m_viewport;

  typedef std::map<std::string, std::function<void(void)>> commands_t;
  commands_t m_applicationCommands;
};
} // namespace TraceNL
} // namespace TDx
#endif // CApplication3D_HPP_INCLUDED