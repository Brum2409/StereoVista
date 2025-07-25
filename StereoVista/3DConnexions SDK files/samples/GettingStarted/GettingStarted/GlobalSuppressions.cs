// <copyright file="GlobalSuppressions.cs" company="3Dconnexion">
// -------------------------------------------------------------------------------------------------
// Copyright (c) 2020 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer Kit",
// including all accompanying documentation, and is protected by intellectual property laws. All use
// of the 3Dconnexion Software Developer Kit is subject to the License Agreement found in the
// "LicenseAgreementSDK.txt" file.
// All rights not expressly granted by 3Dconnexion are reserved.
// -------------------------------------------------------------------------------------------------
// </copyright>

using System.Diagnostics.CodeAnalysis;

[assembly: SuppressMessage(
    "StyleCop.CSharp.ReadabilityRules",
    "SA1117:Parameters must be on same line or separate lines",
    Justification = "Matrices are more readable.",
    Scope = "member",
    Target = "~M:TDx.GettingStarted.Navigation.NavigationModel.TDx#SpaceMouse#Navigation3D#IView#GetCameraMatrix~TDx.SpaceMouse.Navigation3D.Matrix")]
