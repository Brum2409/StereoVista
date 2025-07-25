// <copyright file="IViewModel.cs" company="3Dconnexion">
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
// <history>
// *************************************************************************************************
//
// $Id: IViewModel.cs 15519 2018-11-12 13:29:59Z mbonk $
//
// </history>

namespace TDx.GettingStarted.Navigation
{
    using OpenTK;

    /// <summary>
    ///  The ViewModel interface required for the NavigationModel
    /// </summary>
    internal interface IViewModel
    {
        /// <summary>
        /// Gets or sets a value indicating whether the view is animating.
        /// </summary>
        bool Animating { get; set; }

        /// <summary>
        /// Gets the view model's camera
        /// </summary>
        Camera3D Camera { get; }

        /// <summary>
        /// Gets the model's bounding box
        /// </summary>
        Box3 ModelBounds { get; }

        /// <summary>
        /// Gets the Viewport
        /// </summary>
        GLControl Viewport { get; }

        /// <summary>
        /// Starts a navigation transaction
        /// </summary>
        void BeginTransaction();

        /// <summary>
        /// Ends a navigation transaction
        /// </summary>
        void EndTransaction();
    }
}