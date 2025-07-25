// <copyright file="IModelProvider.cs" company="3Dconnexion">
// ------------------------------------------------------------------------------------
// Copyright (c) 2018 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer
// Kit", including all accompanying documentation, and is protected by intellectual
// property laws. All use of the 3Dconnexion Software Developer Kit is subject to the
// License Agreement found in the "LicenseAgreementSDK.txt" file.
// All rights not expressly granted by 3Dconnexion are reserved.
// ------------------------------------------------------------------------------------
// </copyright>
// <history>
// *************************************************************************************
// File History
//
// $Id: IModelProvider.cs 15000 2018-05-15 12:04:24Z mbonk $
//
// </history>

namespace TDx.TestNL.ModelLoader
{
    using TDx.TestNL.ModelLoader.Geometry;

    /// <summary>
    /// Interface
    /// </summary>
    public interface IModelProvider
    {
        /// <summary>
        /// Gets the <see cref="Model3D"/>.
        /// </summary>
        Model3D Model { get; }
    }
}