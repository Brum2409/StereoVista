// <copyright file="ActionInput.cpp" company="3Dconnexion">
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
// $Id: $
//
// </history>
#include "CApplication.hpp"

// stdlib
#include <chrono>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <thread>

using namespace TDx::ActionInput;
int main() {
  FILE *f;
  freopen_s(&f, "CONOUT$", "w", stdout);
  std::cout.clear();
  std::cout
      << "Started the ActionInput sample...\n\n"
      << "\tThe sample exports commands that can be assigned to 3DConnexion device buttons using\n"
      << "\tthe \'3Dconnexion Properties\' app. The commands from this sample are listed in the \n"
      << "\tflyout under the string passed to the PutProfileHint() method: \"Action Input sample\".\n"
      << "\tIn the sample the commands result in a corresponding text being printed to the console\n"
      << "\twindow.\n\n"
      << "\tThe sample requires that the \'Images\' directory is a subfolder of the application\n"
      << "\tdirectory, for the icons for the commands to be displayed.\n\n"
      << "\tNote: To debug the sample using Visual Studio, start ActionInput.exe from the OS and\n"
      << "\tthen attach the debugger to the running application.";
  std::cout << std::endl;

  CApplication theApp;
  theApp.Run();

  std::cout << "ActionInput sample exiting. Goodbye!" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));

  return 0;
}