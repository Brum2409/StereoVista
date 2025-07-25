// <copyright file="PivotCallbacks.cs" company="3Dconnexion">
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
// $Id: PivotCallbacks.cs 17485 2020-05-29 12:44:39Z mbonk $
//
// </history>

namespace TDx.TestNL.Navigation
{
    using TDx.SpaceMouse.Navigation3D;

    /// <summary>
    /// Implements the callbacks for the IPivot interface
    /// </summary>
    internal partial class NavigationModel : IPivot
    {
        /// <summary>
        /// Is called when the Navigation3D instance needs to get the position of the rotation pivot.
        /// </summary>
        /// <returns>The position of the pivot.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">No pivot position.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Point IPivot.GetPivotPosition()
        {
            return this.viewportVM.PivotPosition.AsPoint();
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to set the position of the rotation pivot.
        /// </summary>
        /// <param name="value">The pivot <see cref="Point"/>.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IPivot.SetPivotPosition(Point value)
        {
            this.viewportVM.PivotPosition = value.AsPoint3D();
        }

        /// <summary>
        /// Occurs when the Navigation3D instance needs to set the visibility of the pivot point.
        /// </summary>
        /// <param name="value">true if the pivot is visible otherwise false.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IPivot.SetPivotVisible(bool value)
        {
            this.viewportVM.PivotVisible = value;
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to retrieve whether the user has manually set a pivot point.
        /// </summary>
        /// <returns>true if the user has set a pivot otherwise false.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        bool IPivot.IsUserPivot()
        {
            return false;
        }
    }
}