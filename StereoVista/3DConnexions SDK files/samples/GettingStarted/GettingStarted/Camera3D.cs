// <copyright file="Camera3D.cs" company="3Dconnexion">
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
// $Id$
//
// </history>

namespace TDx.GettingStarted
{
    using OpenTK;
    using Point3 = OpenTK.Vector3;

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
    /// Class to hold the camera data.
    /// </summary>
    public class Camera3D
    {
        /// <summary>
        /// Gets or sets the vertical Field of View of the Camera in radians.
        /// </summary>
        public float FieldOfView { get; set; }

        /// <summary>
        /// Gets a value indicating whether the camera uses a perspective projection.
        /// </summary>
        public bool IsPerspective => this.Projection == Projection.Perspective;

        /// <summary>
        /// Gets or sets the camera projection
        /// </summary>
        public Projection Projection { get; set; }

        /// <summary>
        /// Gets or sets the up direction vector of the <see cref="Camera3D"/>.
        /// </summary>
        public Vector3 Up { get; set; }

        /// <summary>
        /// Gets or sets the position the <see cref="Camera3D"/> is pointing towards.
        /// </summary>
        public Point3 Target { get; set; }

        /// <summary>
        /// Gets or sets camera's position in world coordinates.
        /// </summary>
        public Point3 Eye { get; set; }

        /// <summary>
        /// Gets or sets a value that specifies the distance from the camera of the camera's far clip plane.
        /// </summary>
        public float FarPlaneDistance { get; set; }

        /// <summary>
        /// Gets or sets a value that specifies the distance from the camera of the camera's near clip plane.
        /// </summary>
        public float NearPlaneDistance { get; set; }
    }
}
