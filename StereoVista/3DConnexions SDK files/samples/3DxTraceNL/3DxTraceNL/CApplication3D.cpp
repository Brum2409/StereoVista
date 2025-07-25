// <copyright file="CApplication3D.cpp" company="3Dconnexion">
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
#include "CApplication3D.hpp"
#include "CCommandEventArgs.hpp"
#include "CKeyEventArgs.hpp"

// stdlib
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>

#if defined(_MSC_VER) && (_MSC_VER >= 1900 && _MSVC_LANG < 201703)
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING 1
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace TDx {
/// <summary>
/// Contains the code specific to the 3DxTraceNL sample.
/// </summary>
namespace TraceNL {
/// <summary>
/// String to use as the unique id for the 'Open' command.
/// </summary>
constexpr const char *const ID_OPEN = "ID_OPEN";
/// <summary>
/// String to use as the unique id for the 'Close' command.
/// </summary>
constexpr const char *const ID_CLOSE = "ID_CLOSE";
/// <summary>
/// String to use as the unique id for the 'Save' command.
/// </summary>
constexpr const char *const ID_SAVE = "ID_SAVE";
/// <summary>
/// String to use as the unique id for the 'Exit' command.
/// </summary>
constexpr const char *const ID_EXIT = "ID_EXIT";
/// <summary>
/// String to use as the unique id for the 'About' command.
/// </summary>
constexpr const char *const ID_ABOUT = "ID_ABOUT";
/// <summary>
/// String to use as the unique id for the 'Select All' command.
/// </summary>
constexpr const char *const ID_SELECTALL = "ID_SELECTALL";
/// <summary>
/// String to use as the unique id for the "Clear Selection' command.
/// </summary>
constexpr const char *const ID_CLEARSELECTION = "ID_CLEARSELECTION";
/// <summary>
/// String to use as the unique id for the 'Parallel Projection' command.
/// </summary>
constexpr const char *const ID_PARALLEL = "ID_PARALLEL";
/// <summary>
/// String to use as the unique id for the 'Perspective Projection' command.
/// </summary>
constexpr const char *const ID_PERSPECTIVE = "ID_PERSPECTIVE";

/// <summary>
/// Initializes a new instance of the CApplication3D class.
/// </summary>
CApplication3D::CApplication3D() : m_navigationModel(&m_viewportViewModel) {
  using std::placeholders::_1;

  // Connect the event handlers
  m_navigationModel.ExecuteCommand.connect(
      std::bind(std::mem_fn(&CApplication3D::ExecuteCommandHandler), this, _1));
  m_navigationModel.KeyDown.connect(
      std::bind(std::mem_fn(&CApplication3D::KeyDownHandler), this, _1));
  m_navigationModel.KeyUp.connect(std::bind(std::mem_fn(&CApplication3D::KeyUpHandler), this, _1));
  m_navigationModel.SettingsChanged.connect(
      std::bind(std::mem_fn(&CApplication3D::SettingsChangedHandler), this));
}

/// <summary>
/// Displays information about the application.
/// </summary>
void CApplication3D::About() {
  std::cout << "!! About command invoked." << std::endl;
  std::cout << "\n";
  std::cout << "********************************************************\n";
  std::cout << "*            3Dconnexion 3DxTraceNL Sample             *\n";
  std::cout << "* Copyright (c) 2018 3Dconnexion. All rights reserved. *\n";
  std::cout << "********************************************************\n";
  std::cout << "\n" << std::endl;
}

/// <summary>
/// Exits the application.
/// </summary>
void CApplication3D::Exit() {
  std::cout << "!! Exit command invoked." << std::endl;
  std::unique_lock<std::mutex> lock(m_cv_m);
  m_exit = true;
  lock.unlock();
  m_cv.notify_all();
}

/// <summary>
/// Close the current 3D model.
/// </summary>
void CApplication3D::CloseFile() {
  std::cout << "!! Close command invoked." << std::endl;
  m_viewportViewModel.PutModel(Media3D::CModel3DGroup());
}

/// <summary>
/// Open a 3D model file.
/// </summary>
void CApplication3D::OpenFile() {
  std::cout << "!! Open command invoked." << std::endl;
  Media3D::CModel3DGroup model;
  model.GetChildren().emplace_back(Media3D::CModel3D());
  m_viewportViewModel.PutModel(std::move(model));
}

/// <summary>
/// Save the 3D model.
/// </summary>
void CApplication3D::SaveFile() { std::cout << "!! Save command invoked." << std::endl; }

/// <summary>
/// The application's main execution loop.
/// </summary>
void CApplication3D::Run() {
  // Create a viewport
  m_viewport = std::make_shared<Media3D::CViewport3D>();

  // Set the viewport the view model controls
  m_viewportViewModel.SetView(m_viewport);

  Enable3DNavigation();

  // This application has nothing to do.
  std::unique_lock<std::mutex> lock(m_cv_m);
  while (!m_exit) {
    m_cv.wait(lock);
  }

  Disable3DNavigation();

  std::cout << "CApplication3D::Run() exiting..." << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
}

/// <summary>
/// Initializes the navigation model instance.
/// </summary>
void CApplication3D::Enable3DNavigation() {
  // Set the hint/title for the '3Dconnexion Settings' Utility.
  m_navigationModel.PutProfileHint(YOUR_PROGRAM_NAME_GOES_HERE);

  std::error_code ec;

  // Enable input from / output to the Navigation3D controller.
  m_navigationModel.EnableNavigation(true, ec);
  if (ec) {
    // something failed
    return;
  }

  try {
    ExportCommandImages();

    ExportApplicationCommands();
  } catch (const std::exception &) {
    // something unexpected happened
  }
}

/// <summary>
/// Handle the ExecuteCommand event.
/// </summary>
/// <param name="args">A <see cref="CCommandEventArgs"/> that contains the data for the command to
/// invoke.</param>
void CApplication3D::ExecuteCommandHandler(Navigation::CCommandEventArgs &args) {
  std::cout << "ExecuteCommand (" << args.GetId() << ") invoked." << std ::endl;

  commands_t::const_iterator iter = m_applicationCommands.find(args.GetId());
  if (iter != m_applicationCommands.cend()) {
    try {
      iter->second();
      args.Handled = true;
    } catch (const std::exception &e) {
      // Some sort of error handling
      std::cout << "Uncaught exception thrown in " << args.GetId() << e.what() << std::endl;
    }
  }
}

/// <summary>
/// Handle the KeyDown event.
/// </summary>
/// <param name="args">A <see cref="CKeyEventArgs"/> that contains the data for the event.</param>
void CApplication3D::KeyDownHandler(Navigation::CKeyEventArgs &args) {
  std::cout << "KeyDown (Key=" << args.GetKey() << ") invoked." << std ::endl;
}

/// <summary>
/// Handle the KeyUp event.
/// </summary>
/// <param name="args">A <see cref="CKeyEventArgs"/> that contains the data for the event.</param>
void CApplication3D::KeyUpHandler(Navigation::CKeyEventArgs &args) {
  std::cout << "KeyUp (Key=" << args.GetKey() << ") invoked." << std ::endl;
}

/// <summary>
/// Handle the SettingsChangedHandler event.
/// </summary>
void CApplication3D::SettingsChangedHandler() {
  std::cout << "SettingsChanged invoked." << std ::endl;
}

/// <summary>
/// Exports the application commands to the 3Dconnexion Settings Configuration Utility.
/// </summary>
void CApplication3D::ExportApplicationCommands() {
  using SpaceMouse::CCategory;
  using SpaceMouse::CCommand;
  using SpaceMouse::CCommandSet;

  m_applicationCommands = {
      {ID_CLOSE, [this]() { CloseFile(); }},
      {ID_OPEN, [this]() { OpenFile(); }},
      {ID_SAVE, [this]() { SaveFile(); }},
      {ID_EXIT, [this]() { Exit(); }},
      {ID_ABOUT, [this]() { About(); }},
      {ID_SELECTALL, [this]() { m_viewportViewModel.SelectAll(); }},
      {ID_CLEARSELECTION, [this]() { m_viewportViewModel.ClearSelection(); }},
      {ID_PARALLEL,
       [this]() { m_viewportViewModel.PutProjection(ViewModels::Projection::Orthographic); }},
      {ID_PERSPECTIVE,
       [this]() { m_viewportViewModel.PutProjection(ViewModels::Projection::Perspective); }}};

  // An CommandSet can also be considered to be a button bank, a menubar, or a
  // set of toolbars
  CCommandSet menuBar("Default", "Ribbon");

  // Create some categories / menus / tabs to the menu
  {
    // File menu
    CCategory menu("FileMenu", "File");
    menu.push_back(CCommand(ID_OPEN, "Open file...", "Open a 3D image file."));
    menu.push_back(CCommand(ID_CLOSE, "Close file", "Close the current 3D image file."));
    menu.push_back(CCommand(ID_EXIT, "Exit"));
    menuBar.push_back(std::move(menu));
  }

  {
    // Select menu
    CCategory menu("SelectMenu", "Selection");
    menu.push_back(CCommand(ID_SELECTALL, "Select All"));
    menu.push_back(CCommand(ID_CLEARSELECTION, "Clear Selection"));
    menuBar.push_back(std::move(menu));
  }

  {
    // Views menu
    CCategory menu("ViewsMenu", "View");
    menu.push_back(CCommand(ID_PARALLEL, "Parallel View", "Switch to an orthographic projection."));
    menu.push_back(
        CCommand(ID_PERSPECTIVE, "Perspective View", "Switch to a perspective projection."));
    menuBar.push_back(std::move(menu));
  }

  {
    // Help menu
    CCategory menu("HelpMenu", "Help");
    menu.push_back(CCommand(ID_ABOUT, "About...", "Display information about the program."));
    menuBar.push_back(std::move(menu));
  }

  // Add the command set to the commands available for assigning to 3DMouse buttons
  m_navigationModel.AddCommandSet(menuBar);

  // Activate the command set
  m_navigationModel.PutActiveCommands(menuBar.GetId());
}

/// <summary>
/// Exports the images for the commands to the 3Dconnexion Settings Configuration Utility.
/// </summary>
/// <remarks>
/// Images can be exported from three different sources:
/// - an image file from the hard-disk, by specifying the index (in case of multi-image file),
/// - a resource file from the hard-disk, by specifying resource type and index,
/// - an image buffer.
/// All the formats that can be loaded by Gdiplus::Bitmap::FromStream() (including recognizable SVG
/// formats) are allowed.
/// </remarks>
void CApplication3D::ExportCommandImages() {
  std::vector<CImage> images = {
      CImage::FromFile(fs::canonical("images/about.png").generic_string(), 0, ID_ABOUT),
      CImage::FromResource("c:/windows/system32/ieframe.dll", "#216", "#2", 12, ID_OPEN),
      CImage::FromResource("c:/windows/system32/ieframe.dll", "#216", "#2", 10, ID_EXIT),
      CImage::FromFile(fs::canonical("images/close.png").generic_string(), 0, ID_CLOSE),
      CImage::FromFile(fs::canonical("images/select_all.png").generic_string(), 0, ID_SELECTALL),
      CImage::FromFile(fs::canonical("images/clear_selection.png").generic_string(), 0,
                       ID_CLEARSELECTION),
      CImage::FromFile(fs::canonical("images/parallel_view.png").generic_string(), 0,
                       ID_PARALLEL)};


  std::ifstream file(fs::canonical("images/perspective_view.png").generic_string(),
                     std::ios::binary);
  std::string data;
  std::copy(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(),
            std::back_inserter(data));

  images.push_back(std::move(CImage::FromData(data, 0, ID_PERSPECTIVE)));

  m_navigationModel.AddImages(images);
} // namespace TraceNL
} // namespace TraceNL
} // namespace TDx