// <copyright file="Camera3D.cs" company="3Dconnexion">
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
// $Id: Camera3D.cs 15519 2018-11-12 13:29:59Z mbonk $
//
// </history>

namespace TDx.TestNL.ViewModels.Utils
{
    using System;
    using System.Windows.Media.Media3D;
    using System.Windows.Threading;

    /// <summary>
    /// View and camera projection types
    /// </summary>
    public enum Projection
    {
        /// <summary>
        /// Perspective projection
        /// </summary>
        Perspective,

        /// <summary>
        /// Orthographic projection
        /// </summary>
        Orthographic
    }

    /// <summary>
    /// Wrapper class to encapsulate orthographic and perspective cameras
    /// </summary>
    public class Camera3D : NotifyPropertyChanged
    {
        private Projection projection = Projection.Perspective;

        private ProjectionCamera camera;
        private PerspectiveCamera perspectiveCamera;
        private OrthographicCamera orthographicCamera;

        private double projectionPlaneDistance = 5;

        /// <summary>
        /// Initializes a new instance of the <see cref="Camera3D"/> class.
        /// </summary>
        public Camera3D()
        {
            Vector3D upDirection = new Vector3D(0, 1, 0);
            Vector3D lookDirection = new Vector3D(0, 0, -1);
            Point3D position = new Point3D(0, 0, 5);

            double fov = 45;

            double nearPlaneDistance = 0.01;
            double farPlaneDistance = 1000;

            // The world width on the projection plane
            double width = Math.Tan(fov * 0.5 * Math.PI / 180.0) * nearPlaneDistance * 2;

            this.perspectiveCamera = new PerspectiveCamera(position, lookDirection, upDirection, fov)
            {
                NearPlaneDistance = nearPlaneDistance,
                FarPlaneDistance = farPlaneDistance
            };

            this.orthographicCamera = new OrthographicCamera(position, lookDirection, upDirection, width)
            {
                NearPlaneDistance = double.NegativeInfinity,
                FarPlaneDistance = farPlaneDistance
            };

            this.camera = this.projection == Projection.Perspective ? (ProjectionCamera)this.perspectiveCamera : (ProjectionCamera)this.orthographicCamera;
        }

        /// <summary>
        /// Initializes a new instance of the <see cref="Camera3D"/> class.
        /// </summary>
        /// <param name="projection">Sets the initial default <see cref="Projection"/></param>
        public Camera3D(Projection projection)
            : this()
        {
            this.projection = projection;
        }

        /// <summary>
        /// Gets the current <see cref="ProjectionCamera"/>
        /// </summary>
        public ProjectionCamera Camera
        {
            get
            {
                return this.camera;
            }

            private set
            {
                this.camera = value;

                this.OnPropertyChanged();
            }
        }

        /// <summary>
        /// Gets the <see cref="Dispatcher"/> associated with the <see cref="Camera3D"/> instance.
        /// </summary>
        public Dispatcher Dispatcher
        {
            get
            {
                return this.Camera.Dispatcher;
            }
         }

        /// <summary>
        /// Gets or sets the world width of the <see cref="OrthographicCamera"/> on the projection plane
        /// Gets the world width of the <see cref="PerspectiveCamera"/> on the projection plane
        /// </summary>
        public double Width
        {
            get
            {
                if (this.projection == Projection.Orthographic)
                {
                    return this.orthographicCamera.Width;
                }
                else
                {
                    double width = Math.Tan(this.perspectiveCamera.FieldOfView * 0.5 * Math.PI / 180.0) * this.NearPlaneDistance * 2;
                    return width;
                }
            }

            set
            {
                if (this.projection == Projection.Orthographic)
                {
                    this.orthographicCamera.Width = value;
                }
                else
                {
                    throw new InvalidOperationException("Cannot set the width of a perspective camera, use FOV instead");
                }
            }
        }

        /// <summary>
        /// Gets or sets the Field of View of the <see cref="PerspectiveCamera"/> in degrees. For an <see cref="OrthographicCamera"/>
        /// sets the view width.
        /// </summary>
        public double Fov
        {
            get
            {
                if (this.projection == Projection.Perspective)
                {
                    return this.perspectiveCamera.FieldOfView;
                }
                else
                {
                    double fov = Math.Atan2(this.orthographicCamera.Width, this.projectionPlaneDistance * 2) * 2 * 180.0 / Math.PI;
                    return fov;
                }
            }

            set
            {
                this.perspectiveCamera.FieldOfView = value;
                double width = Math.Tan(this.perspectiveCamera.FieldOfView * 0.5 * Math.PI / 180.0) * this.projectionPlaneDistance * 2;
                this.orthographicCamera.Width = width;
            }
        }

        /// <summary>
        /// Gets a value indicating whether the camera uses a perspective projection
        /// </summary>
        public bool IsPerspective
        {
            get
            {
                return this.projection == Projection.Perspective;
            }
        }

        /// <summary>
        /// Gets or sets the camera projection
        /// </summary>
        public Projection Projection
        {
            get
            {
                return this.projection;
            }

            set
            {
                if (value != this.projection)
                {
                    ProjectionCamera projectionCamera = value == Projection.Perspective ? (ProjectionCamera)this.perspectiveCamera : (ProjectionCamera)this.orthographicCamera;
                    projectionCamera.LookDirection = this.camera.LookDirection;
                    projectionCamera.UpDirection = this.camera.UpDirection;
                    projectionCamera.Position = this.camera.Position;
                    this.projection = value;
                    this.Camera = projectionCamera;
                }
            }
        }

        /// <summary>
        /// Gets or sets the up direction vector of the <see cref="ProjectionCamera"/>
        /// </summary>
        public Vector3D UpDirection
        {
            get
            {
                return this.camera.UpDirection;
            }

            set
            {
                this.camera.UpDirection = value;
            }
        }

        /// <summary>
        /// Gets or sets the look direction of the <see cref="ProjectionCamera"/>
        /// </summary>
        public Vector3D LookDirection
        {
            get
            {
                return this.camera.LookDirection;
            }

            set
            {
                this.camera.LookDirection = value;
            }
        }

        /// <summary>
        /// Gets <see cref="ProjectionCamera"/>'s right direction vector
        /// by getting cross product of <see cref="LookDirection"/>
        /// and <see cref="UpDirection"/>
        /// </summary>
        public Vector3D RightDirection
        {
            get
            {
                return Vector3D.CrossProduct(this.camera.LookDirection, this.camera.UpDirection);
            }
        }

        /// <summary>
        /// Gets or sets camera's position in world coordinates.
        /// </summary>
        public Point3D Position
        {
            get
            {
                return this.camera.Position;
            }

            set
            {
                this.camera.Position = value;
            }
        }

        /// <summary>
        /// Gets or sets camera's near plane distance.
        /// </summary>
        public double NearPlaneDistance
        {
            get
            {
                return this.perspectiveCamera.NearPlaneDistance;
            }

            set
            {
                this.perspectiveCamera.NearPlaneDistance = value;
            }
        }

        /// <summary>
        /// Gets or sets camera's far plane distance.
        /// </summary>
        public double FarPlaneDistance
        {
            get
            {
                return this.camera.FarPlaneDistance;
            }

            set
            {
                this.camera.FarPlaneDistance = value;
            }
        }

        /// <summary>
        /// Gets or sets the camera's affine matrix
        /// </summary>
        public Matrix3D Affine
        {
            get
            {
                Vector3D right = this.RightDirection;
                Matrix3D @value = new Matrix3D(right.X, right.Y, right.Z, 0,
                    this.UpDirection.X, this.UpDirection.Y, this.UpDirection.Z, 0,
                    -this.LookDirection.X, -this.LookDirection.Y, -this.LookDirection.Z, 0,
                    this.Position.X, this.Position.Y, this.Position.Z, 1);

                if (!@value.IsAffine)
                {
                    throw new ArithmeticException("Matrix is not Affine");
                }

                return @value;
            }

            set
            {
                if (!value.IsAffine)
                {
                    throw new ArithmeticException("Matrix is not Affine");
                }

                this.UpDirection = new Vector3D(value.M21, value.M22, value.M23);
                this.LookDirection = new Vector3D(-value.M31, -value.M32, -value.M33);
                this.Position = new Point3D(value.OffsetX, value.OffsetY, value.OffsetZ);
            }
        }

        /// <summary>
        /// Implicit conversion to <see cref="ProjectionCamera"/>
        /// </summary>
        /// <param name="camera3D">The <see cref="Camera3D"/> to convert.</param>
        /// <remarks>This accesses the <see cref="Camera3D"/>'s <see cref="Camera"/> property.</remarks>
        public static implicit operator ProjectionCamera(Camera3D camera3D)
        {
            return camera3D.Camera;
        }
    }
}