// <copyright file="IViewModelNavigation.cs" company="3Dconnexion">
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
// $Id: IViewModelNavigation.cs 15519 2018-11-12 13:29:59Z mbonk $
//
// </history>

namespace TDx.TestNL.Navigation
{
    using System;
    using System.ComponentModel;
    using System.Windows.Controls;
    using System.Windows.Media.Media3D;
    using TDx.TestNL.HitTesting;
    using TDx.TestNL.ViewModels.Utils;

    /// <summary>
    ///  ViewModel interface required for the NavigationModel
    /// </summary>
    internal interface IViewModelNavigation : INotifyPropertyChanged
    {
        /// <summary>
        /// Occurs when the frame time changed
        /// </summary>
        event EventHandler<FrameTimeChangedEventArgs> FrameTimeChanged;

        /// <summary>
        /// Gets or sets a value indicating whether the view is animating.
        /// </summary>
        bool Animating { get; set; }

        /// <summary>
        /// Gets the view model's camera
        /// </summary>
        Camera3D Camera { get; }

        /// <summary>
        /// Gets the 3D model geometry/>.
        /// </summary>
        Model3DGroup Model { get; }

        /// <summary>
        /// Gets the group of selected model parts.
        /// </summary>
        Model3DGroup SelectedModel { get; }

        /// <summary>
        /// Gets or sets the Pivot position value.
        /// </summary>
        /// <exception cref="System.ArgumentException">The argument provided is not valid.</exception>
        Point3D PivotPosition { get; set; }

        /// <summary>
        /// Gets a value indicating whether a user pivot is set.
        /// </summary>
        bool IsUserPivot { get; }

        /// <summary>
        /// Gets or sets a value indicating whether the Pivot is visible.
        /// </summary>
        bool PivotVisible { get; set; }

        /// <summary>
        /// Gets the Viewport
        /// </summary>
        Viewport3D Viewport { get; }

        /// <summary>
        /// Starts a navigation transaction
        /// </summary>
        void BeginTransaction();

        /// <summary>
        /// Ends a navigation transaction
        /// </summary>
        void EndTransaction();

        /// <summary>
        /// Performs hit testing on the model.
        /// </summary>
        /// <param name="hitRay">The <see cref="ApertureRay"/> to use for the hit-testing.</param>
        /// <param name="selection">Filter the hits to the selection.</param>
        /// <param name="hit">The <see cref="Point3D"/> of the hit in world coordinates.</param>
        /// <returns>true if something was hit, false otherwise.</returns>
        bool HitTest(ApertureRay hitRay, bool selection, out Point3D hit);

        /// <summary>
        /// Convert a 2D viewport <see cref="System.Windows.Point"/> to world coordinates.
        /// </summary>
        /// <param name="pt2D"><see cref="System.Windows.Point"/> on the viewport.</param>
        /// <returns>The <see cref="Point3D"/> in world coordinates.</returns>
        Point3D ToWorldCoordinates(System.Windows.Point pt2D);
    }
}