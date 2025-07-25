// <copyright file="ViewCallbacks.cs" company="3Dconnexion">
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
// $Id: ViewCallbacks.cs 20976 2024-06-21 08:34:36Z mbonk $
//
// </history>

namespace TDx.TestNL.Navigation
{
    using System;
    using System.Windows.Input;
    using System.Windows.Media.Media3D;
    using TDx.SpaceMouse.Navigation3D;
    using Application = System.Windows.Application;

    /// <summary>
    /// Class implements the callbacks required for the IView interface.
    /// </summary>
    internal partial class NavigationModel : IView
    {
        /// <summary>
        /// Is called when the Navigation3D instance needs to get the camera matrix from the view.
        /// </summary>
        /// <returns>The camera matrix.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Matrix IView.GetCameraMatrix()
        {
            var camera = this.viewportVM.Camera;
            return camera.Dispatcher.InvokeIfRequired(() =>
            {
                Vector3D up = camera.UpDirection;
                Vector3D backward = -camera.LookDirection;
                Vector3D right = camera.RightDirection;

                up.Normalize();
                backward.Normalize();
                right.Normalize();

                return new Matrix(
                    right.X, right.Y, right.Z, 0,
                    up.X, up.Y, up.Z, 0,
                    backward.X, backward.Y, backward.Z, 0,
                    camera.Position.X, camera.Position.Y, camera.Position.Z, 1);
            });
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to set the view's camera matrix.
        /// </summary>
        /// <param name="matrix">The camera <see cref="Matrix"/>.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetCameraMatrix(Matrix matrix)
        {
            var camera = this.viewportVM.Camera;
            camera.Dispatcher.InvokeIfRequired(() =>
            {
                camera.UpDirection = new Vector3D(matrix.M21, matrix.M22, matrix.M23);

                // A camera looks down the negative z-axis
                camera.LookDirection = new Vector3D(-matrix.M31, -matrix.M32, -matrix.M33);

                camera.Position = new Point3D(matrix.M41, matrix.M42, matrix.M43);
            });
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to get the extents of the view.
        /// </summary>
        /// <returns>The view's extents as a box or null.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Box IView.GetViewExtents()
        {
            return this.dispatcher.InvokeIfRequired(() =>
            {
                double halfCamOrthoWidth = this.viewportVM.Camera.Width / 2;
                double aspectRatio = this.viewportVM.Viewport.ActualWidth / this.viewportVM.Viewport.ActualHeight;
                return new Box(
                    -halfCamOrthoWidth,
                    -halfCamOrthoWidth / aspectRatio,
                    -this.viewportVM.Camera.FarPlaneDistance,
                    halfCamOrthoWidth,
                    halfCamOrthoWidth / aspectRatio,
                    this.viewportVM.Camera.FarPlaneDistance);
            });
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to set the view's extents.
        /// </summary>
        /// <param name="extents">The view's extents to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetViewExtents(Box extents)
        {
            var camera = this.viewportVM.Camera;
            camera.Dispatcher.InvokeIfRequired(() =>
            {
                if (camera.IsPerspective)
                {
                    throw new System.InvalidOperationException("The view is not orthographic.");
                }
                camera.Width = extents.Max.X - extents.Min.X;
            });
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to get the camera's field of view.
        /// </summary>
        /// <returns>The view's field of view.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        double IView.GetViewFOV()
        {
            var camera = this.viewportVM.Camera;
            return camera.Dispatcher.InvokeIfRequired(() =>
            {
                if (!camera.IsPerspective)
                {
                    throw new System.InvalidOperationException("View is not perspective");
                }

                return camera.Fov * Math.PI / 180.0;
            });
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to set the camera's field of view.
        /// </summary>
        /// <param name="fov">The camera field of view to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetViewFOV(double fov)
        {
            var camera = this.viewportVM.Camera;
            camera.Dispatcher.InvokeIfRequired(() =>
            {
                if (!camera.IsPerspective)
                {
                    throw new System.InvalidOperationException("View is not perspective");
                }

                camera.Fov = fov * 180.0 / Math.PI;
            });
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to get the view frustum.
        /// </summary>
        /// <returns>The view's frustum.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Frustum IView.GetViewFrustum()
        {
            return this.dispatcher.InvokeIfRequired(() =>
            {
                if (!this.viewportVM.Camera.IsPerspective)
                {
                    throw new System.InvalidOperationException("View is not perspective");
                }

                double nearPlaneDistance = this.viewportVM.Camera.NearPlaneDistance;

                // oppositeSide = nearSide * tan(alpha)
                double frustumHalfWidth =
                    nearPlaneDistance *
                    Math.Tan(this.viewportVM.Camera.Fov * 0.5 * Math.PI / 180.0);

                double aspectRatio = this.viewportVM.Viewport.ActualWidth / this.viewportVM.Viewport.ActualHeight;

                return new Frustum(
                    -frustumHalfWidth,
                    frustumHalfWidth,
                    -frustumHalfWidth / aspectRatio,
                    frustumHalfWidth / aspectRatio,
                    nearPlaneDistance,
                    this.viewportVM.Camera.FarPlaneDistance);
            });
        }

        /// <summary>
        /// Is invoked when the Navigation3D instance needs to set the view frustum.
        /// </summary>
        /// <param name="frustum">The view <see cref="Frustum"/> to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetViewFrustum(Frustum frustum)
        {
            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is invoked when the Navigation3D instance needs to get the view's projection type.
        /// </summary>
        /// <returns>true for a perspective view, false for an orthographic view, otherwise null.</returns>
        bool IView.IsViewPerspective()
        {
            return this.viewportVM.Camera.IsPerspective;
        }

        /// <summary>
        /// Is invoked when the Navigation3D instance needs to get the camera's target.
        /// </summary>
        /// <returns>The position of the camera's target.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">The camera does not have a target.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Point IView.GetCameraTarget()
        {
            throw new TDx.SpaceMouse.Navigation3D.NoDataException("This camera does not have a target");
        }

        /// <summary>
        /// Is invoked when the Navigation3D instance needs to set the camera's target.
        /// </summary>
        /// <param name="target">The location of the camera's target to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetCameraTarget(Point target)
        {
            throw new System.InvalidOperationException("This camera does not have a target");
        }

        /// <summary>
        /// Is invoked when the Navigation3D instance needs to get the view's construction plane.
        /// </summary>
        /// <returns>The <see cref="Plane"/> equation of the construction plane.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">The view does not have a construction plane.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Plane IView.GetViewConstructionPlane()
        {
            throw new TDx.SpaceMouse.Navigation3D.NoDataException("The view does not have a construction plane");
        }

        /// <summary>
        /// Is invoked when the Navigation3D instance needs to know whether the view can be rotated.
        /// </summary>
        /// <returns>true if the view can be rotated false if not, otherwise null.</returns>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        bool IView.IsViewRotatable()
        {
            return true;
        }

        /// <summary>
        /// Is invoked when the Navigation3D instance needs to set the position of the pointer.
        /// </summary>
        /// <param name="position">The location of the pointer in world coordinates to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetPointerPosition(Point position)
        {
            throw new System.InvalidOperationException("Setting the pointer position is not supported");
        }

        /// <summary>
        /// Is invoked when the Navigation3D instance needs to get the position of the pointer.
        /// </summary>
        /// <returns>The <see cref="Point"/> in world coordinates of the pointer on the projection plane.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">The view does not have a pointer.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Point IView.GetPointerPosition()
        {
            return this.dispatcher.InvokeIfRequired(() =>
            {
                System.Windows.Point mousePos = Mouse.GetPosition(this.viewportVM.Viewport);
                var result = this.viewportVM.ToWorldCoordinates(mousePos);
                return new Point(result.X, result.Y, result.Z);
            });
        }
    }
}