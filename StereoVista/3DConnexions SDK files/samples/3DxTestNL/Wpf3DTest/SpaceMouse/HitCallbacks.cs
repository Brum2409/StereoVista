// <copyright file="HitCallbacks.cs" company="3Dconnexion">
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
// $Id: HitCallbacks.cs 17485 2020-05-29 12:44:39Z mbonk $
//
// </history>

namespace TDx.TestNL.Navigation
{
    using System.Windows.Media.Media3D;
    using TDx.SpaceMouse.Navigation3D;
    using TDx.TestNL.HitTesting;
    using Application = System.Windows.Application;

    /// <summary>
    /// Implements the callbacks for the IHit interface
    /// </summary>
    internal partial class NavigationModel : IHit
    {
        private bool isHitSelectionOnly = false;
        private double hitAperture = 0;
        private Point3D hitLookFrom = default(Point3D);
        private Vector3D hitDirection = default(Vector3D);

        /// <summary>
        /// Is called when the Navigation3D instance needs the result of the hit test.
        /// </summary>
        /// <returns>The hit position in world coordinates.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">Nothing was hit.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Point IHit.GetLookAt()
        {
            return this.dispatcher.InvokeIfRequired(() =>
            {
                var ray = new ApertureRay(this.hitLookFrom, this.hitDirection, this.hitAperture)
                {
                    IsPerspective = this.viewportVM.Camera.IsPerspective,
                    PlaneDistance = this.viewportVM.Camera.NearPlaneDistance
                };

                Point3D hit;
                if (this.viewportVM.HitTest(ray, this.isHitSelectionOnly, out hit))
                {
                    return hit.AsPoint();
                }

                throw new TDx.SpaceMouse.Navigation3D.NoDataException("Nothing Hit");
            });
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to set the source of the hit ray/cone.
        /// </summary>
        /// <param name="value">The source of the hit cone <see cref="Point"/>.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IHit.SetLookFrom(Point value)
        {
            this.hitLookFrom = value.AsPoint3D();
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to set the direction of the hit ray/cone.
        /// </summary>
        /// <param name="value">The direction of the ray/cone to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IHit.SetLookDirection(Vector value)
        {
            this.hitDirection = value.AsVector3D();
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to set the aperture of the hit ray/cone.
        /// </summary>
        /// <param name="value">The aperture of the ray/cone on the near plane.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IHit.SetLookAperture(double value)
        {
            this.hitAperture = value;
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to set the selection filter.
        /// </summary>
        /// <param name="value">If true ignore non-selected items</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IHit.SetSelectionOnly(bool value)
        {
            this.isHitSelectionOnly = value;
        }
    }
}