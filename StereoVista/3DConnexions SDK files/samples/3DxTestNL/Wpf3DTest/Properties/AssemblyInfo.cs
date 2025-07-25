// <copyright file="AssemblyInfo.cs" company="3Dconnexion">
// -------------------------------------------------------------------------------------
// Copyright (c) 2018 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer
// Kit", including all accompanying documentation, and is protected by intellectual
// property laws. All use of the 3Dconnexion Software Developer Kit is subject to the
// License Agreement found in the "LicenseAgreementSDK.txt" file.
// All rights not expressly granted by 3Dconnexion are reserved.
// -------------------------------------------------------------------------------------
// </copyright>
// <history>
// *************************************************************************************
// File History
//
// $Id: AssemblyInfo.cs 21353 2025-01-27 09:30:42Z mbonk $
//
// </history>

using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows;

using TDx.TestNL;

// General Information about an assembly is controlled through the following
// set of attributes. Change these attribute values to modify the information
// associated with an assembly.
[assembly: AssemblyTitle(Constants.ProgramName)]
[assembly: AssemblyDescription("")]
[assembly: AssemblyConfiguration("")]
[assembly: AssemblyCompany("3Dconnexion")]
[assembly: AssemblyProduct(Constants.ProgramName)]
[assembly: AssemblyCopyright("Copyright © 2018-2025 3Dconnexion")]
[assembly: AssemblyTrademark("All rights reserved.")]
[assembly: AssemblyCulture("")]

// Setting ComVisible to false makes the types in this assembly not visible
// to COM components.  If you need to access a type in this assembly from
// COM, set the ComVisible attribute to true on that type.
[assembly: ComVisible(false)]

// In order to begin building localizable applications, set
// <UICulture>CultureYouAreCodingWith</UICulture> in your .csproj file
// inside a <PropertyGroup>.  For example, if you are using US english
// in your source files, set the <UICulture> to en-US.  Then uncomment
// the NeutralResourceLanguage attribute below.  Update the "en-US" in
// the line below to match the UICulture setting in the project file.

// [assembly: NeutralResourcesLanguage("en-US", UltimateResourceFallbackLocation.Satellite)]
[assembly: ThemeInfo(
    ResourceDictionaryLocation.None, // where theme specific resource dictionaries are located (used if a resource is not found in the page, or application resource dictionaries)
    ResourceDictionaryLocation.SourceAssembly) // where the generic resource dictionary is located

// (used if a resource is not found in the page,
// app, or any theme specific resource dictionaries)
]

// Version information for an assembly consists of the following four values:
//
//      Major Version
//      Minor Version
//      Build Number
//      Revision
//
// You can specify all the values or you can default the Build and Revision Numbers
// by using the '*' as shown below:
// [assembly: AssemblyVersion("1.0.*")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]