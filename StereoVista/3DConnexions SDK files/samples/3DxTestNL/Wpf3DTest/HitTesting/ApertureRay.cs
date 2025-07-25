// <copyright file="ApertureRay.cs" company="3Dconnexion">
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
// $Id: ApertureRay.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.HitTesting
{
    using System.Windows.Media.Media3D;

    /// <summary>
    /// Ray class used for hit testing.
    /// </summary>
    public class ApertureRay
    {
        private readonly Point3D origin;
        private readonly Vector3D direction;
        private readonly double aperture;

        private double planeDistance;
        private double tangentAlpha;

        /// <summary>
        /// Initializes a new instance of the <see cref="ApertureRay"/> class.
        /// </summary>
        /// <param name="origin">Specifies the starting point of the Ray.</param>
        /// <param name="direction">Unit vector specifying the direction the Ray is pointing.</param>
        /// <param name="aperture">The diameter of the ray on the near plane.</param>
        /// <exception cref="System.ArgumentException">The aperture is 0.0 or negative.</exception>
        public ApertureRay(Point3D origin, Vector3D direction, double aperture)
        {
            if (aperture <= 0.0)
            {
                throw new System.ArgumentException("Invalid aperture value");
            }

            this.origin = origin;
            this.direction = direction;
            this.aperture = aperture;
        }

        /// <summary>
        /// Gets the starting point of the Ray.
        /// </summary>
        public Point3D Origin => this.origin;

        /// <summary>
        /// Gets the unit vector specifying the direction the Ray is pointing.
        /// </summary>
        public Vector3D Direction => this.direction;

        /// <summary>
        /// Gets or sets a value indicating whether ray is a cone.
        /// </summary>
        public bool IsPerspective { get; set; }

        /// <summary>
        /// Gets or sets the distance to the aperture.
        /// </summary>
        public double PlaneDistance
        {
            get
            {
                return this.planeDistance;
            }

            set
            {
                this.planeDistance = value;

                this.tangentAlpha = (this.aperture / 2) / this.planeDistance;
            }
        }

        /// <summary>
        /// Retrieves the radius of the ray at the distance from the ray origin.
        /// </summary>
        /// <param name="distance">distance from the ray origin.</param>
        /// <returns>radius of the ray.</returns>
        public double GetRadius(double distance) => this.IsPerspective ? (this.tangentAlpha * distance) + (this.aperture / 2) : this.aperture / 2;
    }
}