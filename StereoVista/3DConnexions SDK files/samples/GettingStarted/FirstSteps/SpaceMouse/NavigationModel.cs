// <copyright file="NavigationModel.cs" company="3Dconnexion">
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
// $Id: NavigationModel.cs 15519 2018-11-12 13:29:59Z mbonk $
//
// </history>

namespace TDx.GettingStarted.Navigation
{
    using System;
    using System.Collections.Generic;
    using System.Threading.Tasks;
    using System.Windows.Threading;
    using OpenTK;
    using TDx.SpaceMouse.Navigation3D;
    using Point3 = OpenTK.Vector3;

    /// <summary>
    /// The <see cref="NavigationModel"/> class implements the <see cref="INavigation3D"/> interface.
    /// </summary>
    internal partial class NavigationModel : INavigation3D
    {
        private readonly IViewModel viewportVM;
        private readonly Navigation3D navigation3D;
        private readonly Dispatcher dispatcher;

        private bool enable = false;
        private string profile = default(string);

        /// <summary>
        /// Initializes a new instance of the <see cref="NavigationModel"/> class.
        /// </summary>
        /// <param name="viewportVM">The <see cref="IViewModel"/> interface to the applications view
        /// model.</param>
        public NavigationModel(IViewModel viewportVM)
        {
            this.viewportVM = viewportVM;
            this.dispatcher = Dispatcher.CurrentDispatcher;

            // Create the TDx.SpaceMouse.Navigation3D.Navigation3D instance and hook up the event
            // handlers.
            this.navigation3D = new Navigation3D(this);
            this.navigation3D.ExecuteCommand += this.OnExecuteCommand;
            this.navigation3D.SettingsChanged += this.SettingsChangedHandler;
            this.navigation3D.TransactionChanged += this.TransactionChangedHandler;
            this.navigation3D.MotionChanged += this.MotionChangedHandler;
            this.navigation3D.KeyUp += this.KeyUpHandler;
            this.navigation3D.KeyDown += this.KeyDownHandler;
        }

        /// <summary>
        /// Is invoked when the user invokes an application command.
        /// </summary>
        public event EventHandler<CommandEventArgs> ExecuteCommand;

        /// <summary>
        /// Gets or sets a value indicating whether the navigation is enabled.
        /// </summary>
        /// <exception cref="System.DllNotFoundException">Cannot find the 3DxWare driver library.</exception>
        public bool Enable
        {
            get
            {
                return this.enable;
            }

            set
            {
                if (value != this.enable)
                {
                    if (value)
                    {
                        this.navigation3D.Open3DMouse(this.profile);
                    }
                    else
                    {
                        this.navigation3D.Close();
                    }

                    // Use the SpaceMouse as the source of the frame timing.
                    this.navigation3D.FrameTiming = Navigation3D.TimingSource.SpaceMouse;
                    this.navigation3D.EnableRaisingEvents = value;
                    this.enable = value;
                }
            }
        }

        /// <summary>
        /// Gets or sets the name for the profile to use.
        /// </summary>
        /// <remarks>
        /// The 3Dconnexion driver will use the name to locate the application configuration file.
        /// </remarks>
        public string Profile
        {
            get
            {
                return this.profile;
            }

            set
            {
                if (this.profile != value)
                {
                    this.profile = value;
                    if (this.enable)
                    {
                        this.navigation3D.Close();
                        this.navigation3D.Open3DMouse(this.profile);
                    }
                }
            }
        }

        /// <summary>
        /// Sets the model extents used by the navigation model instance.
        /// </summary>
        /// <remarks>Notify the navigation when the model has changed that the dimensions have changed.</remarks>
        public Box3 ModelExtents
        {
            set
            {
                Box bounds = new Box
                {
                    Min = new Point(value.Min.X, value.Min.Y, value.Min.Z),
                    Max = new Point(value.Max.X, value.Max.Y, value.Max.Z),
                };

                this.navigation3D.Properties.WriteAsync(PropertyNames.ModelExtents, bounds);
            }
        }

        /// <summary>
        /// Gets or sets the active set of commands.
        /// </summary>
        public string ActiveCommands
        {
            get
            {
                return this.navigation3D.ActiveCommandSet;
            }

            set
            {
                this.navigation3D.ActiveCommandSet = value;
            }
        }

        /// <summary>
        /// Add commands to the sets of commands.
        /// </summary>
        /// <param name="commands">The <see cref="CommandTree"/> to add.</param>
        public void AddCommands(CommandTree commands)
        {
            Task t = Task.Run(() =>
            {
                List<Image> images = new List<Image>();
                foreach (CommandSet set in commands)
                {
                    this.GetImages(set, images);
                }
                if (images.Count > 0)
                {
                    this.navigation3D.AddImages(images);
                }
            });

            this.navigation3D.AddCommands(commands);

            t.Wait();
        }

        /// <summary>
        /// Add a set of commands to the sets of commands.
        /// </summary>
        /// <param name="commands">The <see cref="CommandSet"/> to add.</param>
        public void AddCommandSet(CommandSet commands)
        {
            CommandTree tree = new CommandTree
            {
                commands
            };

            this.AddCommands(tree);
        }

        /// <summary>
        /// Add to the images available to the 3D mouse properties UI.
        /// </summary>
        /// <param name="images">The <see cref="List{Image}"/> containing the images to add.</param>
        public void AddImages(List<Image> images)
        {
            this.navigation3D.AddImages(images);
        }

        #region INavigation3D interface implementation

        #region ISpace3D

        /// <summary>
        /// Is called by the Navigation3D instance to get the coordinate system used by the client.
        /// </summary>
        /// <returns>The coordinate system matrix.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Matrix ISpace3D.GetCoordinateSystem()
        {
            return Matrix.Identity;
        }

        /// <summary>
        /// Is called by the Navigation3D instance to get the orientation of the front view.
        /// </summary>
        /// <returns>The orientation matrix of the front view.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">No transform for the front view.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Matrix ISpace3D.GetFrontView()
        {
            return Matrix.Identity;
        }

        #endregion ISpace3D

        #region IView

        /// <summary>
        /// Is called by the Navigation3D instance to get the camera matrix from the view.
        /// </summary>
        /// <returns>The camera matrix.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Matrix IView.GetCameraMatrix()
        {
            var camera = this.viewportVM.Camera;

            // See 'What do I need to do.docx'
            var zAxis = camera.Eye - camera.Target;
            zAxis.Normalize();
            var xAxis = Vector3.Cross(camera.Up, zAxis);
            xAxis.Normalize();
            var yAxis = Vector3.Cross(zAxis, xAxis);

            return new Matrix(
                xAxis.X, xAxis.Y, xAxis.Z, 0,
                yAxis.X, yAxis.Y, yAxis.Z, 0,
                zAxis.X, zAxis.Y, zAxis.Z, 0,
                camera.Eye.X, camera.Eye.Y, camera.Eye.Z, 1);
        }

        /// <summary>
        /// Is called by the Navigation3D instance to set the view's camera matrix.
        /// </summary>
        /// <param name="matrix">The camera <see cref="Matrix"/>.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetCameraMatrix(Matrix matrix)
        {
            // See 'What do I need to do.docx'
            var camera = this.viewportVM.Camera;
            var distance = (camera.Target - camera.Eye).Length;

            camera.Up = new Vector3((float)matrix.M21, (float)matrix.M22, (float)matrix.M23);
            camera.Eye = new Point3((float)matrix.M41, (float)matrix.M42, (float)matrix.M43);

            // Attempt to keep the looked at position constant
            var zAxis = new Vector3((float)matrix.M31, (float)matrix.M32, (float)matrix.M33);
            camera.Target = camera.Eye - (zAxis * distance);
        }

        /// <summary>
        /// Is called by the Navigation3D instance to get the extents of the view.
        /// </summary>
        /// <returns>The view's extents as a box or null.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Box IView.GetViewExtents()
        {
            if (this.viewportVM.Camera.IsPerspective)
            {
                throw new System.InvalidOperationException("View is not orthographic");
            }

            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is called by the Navigation3D instance to set the view's extents.
        /// </summary>
        /// <param name="extents">The view's extents to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetViewExtents(Box extents)
        {
            if (this.viewportVM.Camera.IsPerspective)
            {
                throw new System.InvalidOperationException("View is not orthographic");
            }

            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is called by the Navigation3D instance to get the camera's vertical field of view.
        /// </summary>
        /// <returns>The view's field of view in radians.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        double IView.GetViewFOV()
        {
            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is called by the Navigation3D instance to set the camera's field of view.
        /// </summary>
        /// <param name="fov">The camera field of view to set in radians.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetViewFOV(double fov)
        {
            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is called by the Navigation3D instance to get the view frustum.
        /// </summary>
        /// <returns>The view's frustum.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Frustum IView.GetViewFrustum()
        {
            if (!this.viewportVM.Camera.IsPerspective)
            {
                throw new System.InvalidOperationException("View is not perspective");
            }

            double nearPlaneDistance = this.viewportVM.Camera.NearPlaneDistance;

            // oppositeSide = nearSide * tan(alpha)
            double frustumHalfHeight = nearPlaneDistance *
            Math.Tan(this.viewportVM.Camera.FieldOfView * 0.5);

            double aspectRatio = this.viewportVM.Viewport.AspectRatio;

            return new Frustum(
                -frustumHalfHeight * aspectRatio,
                frustumHalfHeight * aspectRatio,
                -frustumHalfHeight,
                frustumHalfHeight,
                nearPlaneDistance,
                this.viewportVM.Camera.FarPlaneDistance);
        }

        /// <summary>
        /// Is invoked by the Navigation3D instance to set the view frustum.
        /// </summary>
        /// <param name="frustum">The view <see cref="Frustum"/> to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetViewFrustum(Frustum frustum)
        {
            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is invoked by the Navigation3D instance to get the view's projection type.
        /// </summary>
        /// <returns>true for a perspective view, false for an orthographic view, otherwise null.</returns>
        bool IView.IsViewPerspective()
        {
            return this.viewportVM.Camera.IsPerspective;
        }

        /// <summary>
        /// Is invoked by the Navigation3D instance to get the camera's target.
        /// </summary>
        /// <returns>The position of the camera's target.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">The camera does not have a target.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Point IView.GetCameraTarget()
        {
            throw new TDx.SpaceMouse.Navigation3D.NoDataException("This camera does not have a movable target object");
        }

        /// <summary>
        /// Is invoked by the Navigation3D instance to set the camera's target.
        /// </summary>
        /// <param name="target">The location of the camera's target to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetCameraTarget(Point target)
        {
            throw new System.InvalidOperationException("This camera does not have a movable target object");
        }

        /// <summary>
        /// Is invoked by the Navigation3D instance to get the view's construction plane.
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
        /// Is invoked by the Navigation3D instance to know whether the view can be rotated.
        /// </summary>
        /// <returns>true if the view can be rotated false if not, otherwise null.</returns>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        bool IView.IsViewRotatable()
        {
            return true;
        }

        /// <summary>
        /// Is invoked by the Navigation3D instance to set the position of the pointer.
        /// </summary>
        /// <param name="position">The location of the pointer in world coordinates to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IView.SetPointerPosition(Point position)
        {
            throw new System.InvalidOperationException("Setting the pointer position is not supported");
        }

        /// <summary>
        /// Is invoked by the Navigation3D instance to get the position of the pointer.
        /// </summary>
        /// <returns>The <see cref="Point"/> in world coordinates of the pointer on the projection plane.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">The view does not have a pointer.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Point IView.GetPointerPosition()
        {
            throw new System.InvalidOperationException("Getting the pointer position is not supported");
        }

        #endregion IView

        #region IModel

        /// <summary>
        /// Is called by the Navigation3D instance to get the extents of the model.
        /// </summary>
        /// <returns>The extents of the model in world coordinates.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">There is no model in the scene.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Box IModel.GetModelExtents()
        {
            Box bounds = new Box
            {
                Min = new Point(this.viewportVM.ModelBounds.Min.X, this.viewportVM.ModelBounds.Min.Y, this.viewportVM.ModelBounds.Min.Z),
                Max = new Point(this.viewportVM.ModelBounds.Max.X, this.viewportVM.ModelBounds.Max.Y, this.viewportVM.ModelBounds.Max.Z),
            };

            return bounds;
        }

        /// <summary>
        /// Is called by the Navigation3D instance to get the extents of the selection.
        /// </summary>
        /// <returns>The extents of the selection in world coordinates.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">There is no selection.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Box IModel.GetSelectionExtents()
        {
            throw new System.NotImplementedException("Editing the model is not supported.");
        }

        /// <summary>
        /// Is called by the Navigation3D instance to get the extents of the selection.
        /// </summary>
        /// <returns>true if the selection set is empty, otherwise false.</returns>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        bool IModel.IsSelectionEmpty()
        {
            return true;
        }

        /// <summary>
        /// Is called by the Navigation3D instance to set the selections's transform matrix.
        /// </summary>
        /// <param name="transform">The selection's transform <see cref="Matrix"/> in world coordinates.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IModel.SetSelectionTransform(Matrix transform)
        {
            throw new System.NotImplementedException("Editing the model is not supported.");
        }

        /// <summary>
        /// Is called by the Navigation3D instance to get the selections's transform matrix.
        /// </summary>
        /// <returns>The selection's transform <see cref="Matrix"/> in world coordinates or null.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">There is no selection.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Matrix IModel.GetSelectionTransform()
        {
            throw new TDx.SpaceMouse.Navigation3D.NoDataException();
        }

        #endregion IModel

        #region IHit

        /// <summary>
        /// Is called by the Navigation3D instance the result of the hit test.
        /// </summary>
        /// <returns>The hit position in world coordinates.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">Nothing was hit.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Point IHit.GetLookAt()
        {
            throw new TDx.SpaceMouse.Navigation3D.NoDataException("Nothing Hit");
        }

        /// <summary>
        /// Is called by the Navigation3D instance to set the source of the hit ray/cone.
        /// </summary>
        /// <param name="value">The source of the hit cone <see cref="Point"/>.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IHit.SetLookFrom(Point value)
        {
            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is called by the Navigation3D instance to set the direction of the hit ray/cone.
        /// </summary>
        /// <param name="value">The direction of the ray/cone to set.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IHit.SetLookDirection(Vector value)
        {
            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is called by the Navigation3D instance to set the aperture of the hit ray/cone.
        /// </summary>
        /// <param name="value">The aperture of the ray/cone on the near plane.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IHit.SetLookAperture(double value)
        {
            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is called by the Navigation3D instance to set the selection filter.
        /// </summary>
        /// <param name="value">If true ignore non-selected items</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IHit.SetSelectionOnly(bool value)
        {
            throw new System.NotImplementedException();
        }

        #endregion IHit

        #region IPivot

        /// <summary>
        /// Is called by the Navigation3D instance to get the position of the rotation pivot.
        /// </summary>
        /// <returns>The position of the pivot.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">No pivot position.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Point IPivot.GetPivotPosition()
        {
            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is called by the Navigation3D instance to set the position of the rotation pivot.
        /// </summary>
        /// <param name="value">The pivot <see cref="Point"/>.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IPivot.SetPivotPosition(Point value)
        {
            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Occurs by the Navigation3D instance to set the visibility of the pivot point.
        /// </summary>
        /// <param name="value">true if the pivot is visible otherwise false.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IPivot.SetPivotVisible(bool value)
        {
            throw new System.NotImplementedException();
        }

        /// <summary>
        /// Is called by the Navigation3D instance to retrieve whether the user has manually set a pivot point.
        /// </summary>
        /// <returns>true if the user has set a pivot otherwise false.</returns>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        bool IPivot.IsUserPivot()
        {
            return false;
        }

        #endregion IPivot

        #endregion INavigation3D interface implementation

        #region Navigation3D event handlers

        private void OnExecuteCommand(object sender, CommandEventArgs eventArgs)
        {
            this.dispatcher.InvokeIfRequired(() => this.ExecuteCommand?.Invoke(sender, eventArgs));
        }

        private void KeyDownHandler(object sender, KeyEventArgs eventArgs)
        {
        }

        private void KeyUpHandler(object sender, KeyEventArgs eventArgs)
        {
        }

        private void MotionChangedHandler(object sender, MotionEventArgs eventArgs)
        {
            this.viewportVM.Animating = eventArgs.IsNavigating;
        }

        private void SettingsChangedHandler(object sender, EventArgs eventArgs)
        {
        }

        private void TransactionChangedHandler(object sender, TransactionEventArgs eventArgs)
        {
            if (eventArgs.IsBegin)
            {
                this.viewportVM.BeginTransaction();
            }
            else
            {
                this.viewportVM.EndTransaction();
            }
        }

        #endregion Navigation3D event handlers

        private void GetImages(CommandTreeNode set, List<Image> images)
        {
            foreach (CommandTreeNode node in set.Children)
            {
                if (node is Command)
                {
                    Command command = node as Command;
                    if (command.Image != null)
                    {
                        if (command.Image.Id == node.Id)
                        {
                            images.Add(command.Image);
                        }
                        else
                        {
                            Image image = command.Image.Copy();
                            image.Id = node.Id;
                            images.Add(image);
                        }
                    }
                }
                else
                {
                    this.GetImages(node, images);
                }
            }
        }
    }
}