// <copyright file="3DxTraceNL.cpp" company="3Dconnexion">
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
#include "CApplication3D.hpp"

// stdlib
#include <chrono>
#include <stdio.h>
#include <iostream>
#include <memory>
#include <thread>

using namespace TDx::TraceNL;
using namespace TDx::TraceNL::ViewModels;
using namespace TDx::TraceNL::Navigation;
int main() {
  FILE *f;
  freopen_s(&f, "CONOUT$", "w", stdout);
  std::cout.clear();
  std::cout << "3DxTraceNL started..." << std::endl;

  CApplication3D theApp;
  theApp.Run();

  std::cout << "3DxTraceNL ending" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));

  return 0;
}