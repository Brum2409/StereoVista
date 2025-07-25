// <copyright file="Space3DCallbacks.cs" company="3Dconnexion">
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
// $Id: Space3DCallbacks.cs 17485 2020-05-29 12:44:39Z mbonk $
//
// </history>

namespace TDx.TestNL.Navigation
{
    using System.Windows.Media.Media3D;
    using TDx.SpaceMouse.Navigation3D;

    /// <summary>
    /// Implements the callbacks for the ISpace3D interface.
    /// </summary>
    internal partial class NavigationModel : ISpace3D
    {
        /// <summary>
        /// Is called when the Navigation3D instance needs to get the coordinate system used by the client.
        /// </summary>
        /// <returns>The coordinate system matrix.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Matrix ISpace3D.GetCoordinateSystem()
        {
            return Matrix.Identity;
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to get the orientation of the front view.
        /// </summary>
        /// <returns>The orientation matrix of the front view.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">No transform for the front view.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Matrix ISpace3D.GetFrontView()
        {
            return Matrix.Identity;
        }
    }
}