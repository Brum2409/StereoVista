// <copyright file="ConeBeam3.cs" company="3Dconnexion">
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
namespace TDx.GettingStarted
{
    using OpenTK;
    using Point3 = OpenTK.Vector3;

    /// <summary>
    /// Represents a 3D Right Cone.
    /// </summary>
    internal class ConeBeam3
    {
        /// <summary>
        /// Gets or sets the origin of the beam.
        /// </summary>
        public Point3 Origin { get; set; }

        /// <summary>
        /// Gets or sets the beam's direction.
        /// </summary>
        public Vector3 Direction { get; set; }

        /// <summary>
        /// Gets or sets the beam's normalized radius.
        /// </summary>
        public float Radius { get; set; }
    }
}
