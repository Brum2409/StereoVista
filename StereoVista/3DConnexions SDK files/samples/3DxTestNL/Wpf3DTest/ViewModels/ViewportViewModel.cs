// <copyright file="ViewportViewModel.cs" company="3Dconnexion">
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
// $Id: ViewportViewModel.cs 15653 2018-12-11 06:26:57Z mbonk $
//
// </history>

namespace TDx.TestNL.ViewModels
{
    using System;
    using System.Diagnostics;
    using System.Windows;
    using System.Windows.Controls;
    using System.Windows.Input;
    using System.Windows.Media;
    using System.Windows.Media.Media3D;
    using TDx.TestNL.Extensions;
    using TDx.TestNL.HitTesting;
    using TDx.TestNL.Navigation;
    using TDx.TestNL.ViewModels.Utils;

    /// <summary>
    /// View-Model of a <see cref="Views.Viewport"/>.
    /// </summary>
    public class ViewportViewModel : ViewModel, IViewModelNavigation
    {
        private readonly HitTestExecutor hitTestExecutor = new HitTestExecutor();
        private TimeSpan renderingTime = TimeSpan.Zero;
        private bool animating = false;
        private bool userPivot = false;
        private bool pivotVisible = false;
        private Thickness pivotMargin = default(Thickness);
        private Point3D pivotPosition = default(Point3D);

        private Model3DGroup selectedModel = new Model3DGroup();

        private Model3DGroup model;
        private AmbientLight ambientLight = new AmbientLight(Color.FromRgb(0x60, 0x60, 0x60));
        private DirectionalLight directionalLight = new DirectionalLight(Colors.White, new Vector3D(-3, -4, -5));

        /// <summary>
        /// Initializes a new instance of the <see cref="ViewportViewModel"/> class.
        /// </summary>
        public ViewportViewModel()
        {
        }

        /// <summary>
        /// Occurs when the frame time changed
        /// </summary>
        public event EventHandler<FrameTimeChangedEventArgs> FrameTimeChanged;

        /// <summary>
        /// Gets or sets a value indicating whether the view is animating.
        /// </summary>
        public bool Animating
        {
            get
            {
                return this.animating;
            }

            set
            {
                if (this.animating != value)
                {
                    this.animating = value;
                    this.OnPropertyChanged();
                    if (this.animating)
                    {
                        CompositionTarget.Rendering += this.CompositionTarget_Rendering;
                    }
                    else
                    {
                        CompositionTarget.Rendering -= this.CompositionTarget_Rendering;
                    }
                }
            }
        }

        /// <summary>
        /// Gets a value indicating whether a user pivot is set.
        /// Implements <see cref="IViewModelNavigation.IsUserPivot"/>.
        /// </summary>
        public bool IsUserPivot
        {
            get
            {
                return this.userPivot;
            }

            private set
            {
                this.userPivot = value;

                this.OnPropertyChanged();
            }
        }

        /// <summary>
        /// Gets the Pivot margin. This property is used to change the Pivot icons's position.
        /// </summary>
        public Thickness PivotMargin
        {
            get
            {
                return this.pivotMargin;
            }

            private set
            {
                this.pivotMargin = value;

                this.OnPropertyChanged();
            }
        }

        /// <summary>
        /// Gets or sets the Pivot position value. This property recalculates
        /// 3D world coordinates to <see cref="Viewport3D"/> 's 2D coordinates
        /// and affects <see cref="PivotMargin"/>.
        /// Implements <see cref="IViewModelNavigation.PivotPosition"/>.
        /// </summary>
        /// <exception cref="System.ArgumentException">The argument provided is not valid.</exception>
        public Point3D PivotPosition
        {
            get
            {
                return this.pivotPosition;
            }

            set
            {
                if (value.IsNaN())
                {
                    throw new ArgumentException("Point3D is NaN");
                }

                this.pivotPosition = value;
                this.OnPropertyChanged();

                this.UpdatePivotMargin();
            }
        }

        /// <summary>
        /// Gets or sets a value indicating whether the Pivot is visible.
        /// Implements <see cref="IViewModelNavigation.PivotVisible"/>
        /// </summary>
        public bool PivotVisible
        {
            get
            {
                return this.pivotVisible;
            }

            set
            {
                this.pivotVisible = value;

                this.OnPropertyChanged();
            }
        }

        /// <summary>
        /// Gets the group of selected model parts.
        /// Implements <see cref="IViewModelNavigation.SelectedModel"/>
        /// </summary>
        public Model3DGroup SelectedModel
        {
            get
            {
                return this.selectedModel;
            }

            private set
            {
                this.selectedModel = value;

                this.OnPropertyChanged();
            }
        }

        /// <summary>
        /// Gets or sets 3D geometry that will be displayed in the <see cref="Viewport3D"/>.
        /// </summary>
        public Model3DGroup Model
        {
            get
            {
                return this.model;
            }

            set
            {
                if (this.model != value)
                {
                    // clear the selection
                    this.selectedModel = new Model3DGroup();

                    this.model = value;

                    this.hitTestExecutor.Model = value;

                    this.userPivot = false;

                    this.OnPropertyChanged();
                }
            }
        }

        /// <summary>
        /// Gets or sets scene's <see cref="System.Windows.Media.Media3D.AmbientLight"/>.
        /// </summary>
        public AmbientLight AmbientLight
        {
            get
            {
                return this.ambientLight;
            }

            set
            {
                this.ambientLight = value;

                this.OnPropertyChanged();
            }
        }

        /// <summary>
        /// Gets or sets scene's <see cref="System.Windows.Media.Media3D.DirectionalLight"/>.
        /// </summary>
        public DirectionalLight DirectionalLight
        {
            get
            {
                return this.directionalLight;
            }

            set
            {
                this.directionalLight = value;

                this.OnPropertyChanged();
            }
        }

        /// <summary>
        /// Gets or sets the view's projection.
        /// </summary>
        public Projection Projection
        {
            get
            {
                return this.Camera.Projection;
            }

            set
            {
                if (this.Camera.Projection == value)
                {
                    return;
                }

                this.Camera.Projection = value;
                this.UpdatePivotMargin();
            }
        }

        /// <summary>
        /// Gets the Viewport
        /// Implements <see cref="IViewModelNavigation.Viewport"/>.
        /// </summary>
        public Viewport3D Viewport { get; private set; } = default(Viewport3D);

        /// <summary>
        /// Gets the command to handle ViewportSizeChanged events.
        /// </summary>
        public ICommand ViewportSizeChangedCommand => new BaseCommand<SizeChangedEventArgs>(this.ViewportSizeChangedAction);

        /// <summary>
        /// Gets the scene's Camera.
        /// Implements <see cref="IViewModelNavigation.Camera"/>.
        /// </summary>
        public Camera3D Camera { get; private set; } = new Camera3D(Projection.Perspective);

        /// <summary>
        /// Implements <see cref="IViewModelNavigation.HitTest"/>. Performs hit testing on the model.
        /// </summary>
        /// <param name="hitRay">The <see cref="ApertureRay"/> to use for the hit-testing.</param>
        /// <param name="selection">Filter the hits to the selection.</param>
        /// <param name="hit">The <see cref="Point3D"/> of the hit in world coordinates.</param>
        /// <returns>true if something was hit, false otherwise.</returns>
        public bool HitTest(ApertureRay hitRay, bool selection, out Point3D hit)
        {
            HitTestExecutor hitTester = this.hitTestExecutor;
            if (selection)
            {
                hitTester = new HitTestExecutor() { Model = this.selectedModel };
            }

            if (hitTester.HitTest(hitRay))
            {
                hit = hitTester.HitPoint;
                return true;
            }

            hit = default(Point3D);
            return false;
        }

        /// <summary>
        /// Start of a navigation transaction.
        /// </summary>
        public void BeginTransaction()
        {
            // Suspend the property changed events until...
            this.SuspendRaisingPropertyChangedEvents = true;
        }

        /// <summary>
        /// End of a navigation transaction.
        /// </summary>
        public void EndTransaction()
        {
            // Ensure that the pivot icon is drawn at the correct location.
            this.UpdatePivotMargin();

            // ... all the properties have been set to the final values for this frame and
            // then let the view see the changes.
            this.SuspendRaisingPropertyChangedEvents = false;
        }

        /// <summary>
        /// Sets the View property of the ViewModel once the control is loaded and the DataContext of the
        /// <see cref="Views.Viewport"/> has been set.
        /// </summary>
        /// <param name="viewport">The <see cref="Viewport3D"/> instance to use to view the model.</param>
        public void SetView(Viewport3D viewport)
        {
            this.Viewport = viewport;
        }

        /// <summary>
        /// Convert a 2D viewport <see cref="System.Windows.Point"/> to world coordinates.
        /// </summary>
        /// <param name="pt2D"><see cref="System.Windows.Point"/> on the viewport.</param>
        /// <returns>The <see cref="Point3D"/> in world coordinates.</returns>
        public Point3D ToWorldCoordinates(System.Windows.Point pt2D)
        {
            // Normalize the 2D point to the center of the viewport in normalized coordinates [-0.5,0.5]
            Point3D normalized = new Point3D((pt2D.X / this.Viewport.ActualWidth) - 0.5, 0.5 - (pt2D.Y / this.Viewport.ActualHeight), 0);

            double aspectRatio = this.Viewport.ActualWidth / this.Viewport.ActualHeight;

            // Offset from the center of the screen to the pointer position on the near plane
            Vector3D offset = (normalized.X * this.Camera.Width * this.Camera.RightDirection) + (normalized.Y * (this.Camera.Width / aspectRatio) * this.Camera.UpDirection);

            // Instead of the near plane it might be better to use either the projection plane distance or
            // possibly the target distance
            Point3D center = this.Camera.Position + (this.Camera.NearPlaneDistance * this.Camera.LookDirection);
            Point3D point = center + offset;
            return point;
        }

        /// <summary>
        /// Convert a 3D world coordinate to <see cref="System.Windows.Point"/> viewport coordinates.
        /// </summary>
        /// <param name="point"><see cref="Point3D"/> in world coordinates.</param>
        /// <returns>The <see cref="System.Windows.Point"/> in viewport coordinates.</returns>
        public System.Windows.Point ToViewportCoordinates(Point3D point)
        {
            // the point in camera coordinates
            MatrixTransform3D cameraToWorld = new MatrixTransform3D(this.Camera.Affine);
            Point3D pCC = cameraToWorld.Inverse.Transform(point);

            // Trace.WriteLine("Pivot position=" + pCC.ToString());

            // conversion factor for world units to viewport (screen coordinates)
            double world_to_viewport = this.Viewport.ActualWidth / this.Camera.Width;

            // vector from bottom left to viewport center in viewport coordinates
            Vector3D diagonal = new Vector3D(this.Viewport.ActualWidth / 2, this.Viewport.ActualHeight / 2, 0);

            if (this.Camera.IsPerspective)
            {
                // Plane equation of the projection plane in camera coordinates
                Vector3D normal = new Vector3D(0, 0, -1);
                double d = -this.Camera.NearPlaneDistance;

                double intersection = -d / Vector3D.DotProduct(normal, (Vector3D)pCC);

                // vector from bottom left to point projection on projection plane in viewport coordinates
                Vector3D projection = diagonal + (world_to_viewport * intersection * (Vector3D)pCC);

                return new System.Windows.Point(projection.X, this.Viewport.ActualHeight - projection.Y);
            }
            else
            {
                // vector from bottom left to point projection on projection plane in viewport coordinates
                Vector3D projection = diagonal + (world_to_viewport * (Vector3D)pCC);

                return new System.Windows.Point(projection.X, this.Viewport.ActualHeight - projection.Y);
            }
        }

        /// <summary>
        /// Clear the current selection.
        /// </summary>
        public void ClearSelection()
        {
            Model3DGroup selection = this.selectedModel;
            this.Viewport.ClearSelection();
            selection.Children.Clear();
        }

        /// <summary>
        /// Select the complete model.
        /// </summary>
        public void SelectAll()
        {
            this.Viewport.SelectAll();

            Model3DGroup selection = new Model3DGroup();
            foreach (var selectedObj in this.Viewport.GetSelection())
            {
                selection.Children.Add(selectedObj.SelectedModelPart);
            }

            this.SelectedModel = selection;
        }

        /// <summary>
        /// MouseLeftButtonDown action.
        /// </summary>
        /// <param name="e">The event data.</param>
        public void MouseLeftButtonDownAction(MouseButtonEventArgs e)
        {
            this.Viewport.MouseButtonDownSelection(e);

            Model3DGroup selection = new Model3DGroup();
            foreach (var selectedObj in this.Viewport.GetSelection())
            {
                selection.Children.Add(selectedObj.SelectedModelPart);
            }

            this.SelectedModel = selection;
        }

        /// <summary>
        /// The MouseButtonUpAction action for the middle mouse button sets or resets the user defined pivot.
        /// </summary>
        /// <param name="e">The event data.</param>
        public void MouseButtonUpAction(MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Middle)
            {
                return;
            }

            Point mousePosition = e.GetPosition(this.Viewport);
            Point3D clickPosition = this.ToWorldCoordinates(mousePosition);
            Vector3D direction;
            Point3D origin;
            if (this.Camera.IsPerspective)
            {
                direction = clickPosition - this.Camera.Position;
                direction.Normalize();
                origin = this.Camera.Position;
            }
            else
            {
                direction = this.Camera.LookDirection;
                origin = clickPosition - this.Camera.LookDirection;
            }

            ApertureRay ray = new ApertureRay(origin, direction, this.Camera.Width / this.Viewport.ActualWidth)
            {
                IsPerspective = this.Camera.IsPerspective,
                PlaneDistance = this.Camera.NearPlaneDistance
            };

            bool hit = this.hitTestExecutor.HitTest(ray);
            if (hit)
            {
                this.PivotPosition = this.hitTestExecutor.HitPoint;
            }

            this.IsUserPivot = hit;
        }

        /// <summary>
        /// Updates the pivot margin used to drawn the pivot icon at the correct 2D location.
        /// </summary>
        private void UpdatePivotMargin()
        {
            Point viewportPoint = default(Point);
            this.Viewport.Dispatcher.InvokeIfRequired(() => viewportPoint = this.ToViewportCoordinates(this.pivotPosition));

            var newMargin = default(Thickness);
            newMargin.Left = viewportPoint.X - 12;
            newMargin.Top = viewportPoint.Y - 13;

            this.PivotMargin = newMargin;
        }

        /// <summary>
        /// ViewportResized action.
        /// </summary>
        /// <param name="e">The event data.</param>
        private void ViewportSizeChangedAction(SizeChangedEventArgs e)
        {
        }

        private void OnFrameTimeChanged(FrameTimeChangedEventArgs args)
        {
            this.FrameTimeChanged?.Invoke(this, args);
        }

        private void CompositionTarget_Rendering(object sender, EventArgs e)
        {
            if (this.Animating)
            {
                RenderingEventArgs renderingEventArgs = e as RenderingEventArgs;
                if (this.renderingTime != renderingEventArgs.RenderingTime)
                {
                    this.renderingTime = renderingEventArgs.RenderingTime;

                    // Invoke OnFrameTimeChanged asynchronously to avoid recursion as this is effectively
                    // in the EndTransaction handler.
                    this.Viewport.Dispatcher.InvokeAsync(() => this.OnFrameTimeChanged(new FrameTimeChangedEventArgs(this.renderingTime.TotalMilliseconds)));
                }
            }
        }
    }
}