#ifndef CApplication_HPP_INCLUDED
#define CApplication_HPP_INCLUDED
// <copyright file="CApplication.hpp" company="3Dconnexion">
// -------------------------------------------------------------------------------------------
// Copyright (c) 2022 3Dconnexion. All rights reserved.
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
// $Id$
//
// </history>
#include "CActionInputModel.hpp"
#include "IViewModel.hpp"

// stdlib
#include <condition_variable>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace TDx {
/// <summary>
/// Contains the code specific to the ActionInput sample.
/// </summary>
namespace ActionInput {
/// <summary>
/// Class that represents the navigation library client application.
/// </summary>
class CApplication {
public:
  /// <summary>
  /// Initializes a new instance of the CApplication class.
  /// </summary>
  CApplication();

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
  void ExecuteCommandHandler(Commands::CCommandEventArgs &args);
  //
  // Summary:
  //    Handle the KeyDown event
  //
  void KeyDownHandler(Commands::CKeyEventArgs &args);
  //
  // Summary:
  //    Handle the KeyUp event
  //
  void KeyUpHandler(Commands::CKeyEventArgs &args);
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
  void DisableInput() {
    try {
      m_actionInputModel.PutEnable(false);
    } catch (const std::exception &) {
      // Something went wrong
    }
  }
  /// <summary>
  /// Initializes the input model instance.
  /// </summary>
  void EnableInput();

private:
  bool m_exit = false;
  std::mutex m_cv_m;
  std::condition_variable m_cv;

private:
  View::CViewportViewModel m_viewportViewModel;
  Input::CActionInputModel m_actionInputModel;

  typedef std::map<std::string, std::function<void(void)>> commands_t;
  commands_t m_applicationCommands;
};
} // namespace ActionInput
} // namespace TDx
#endif // CApplication_HPP_INCLUDED