// <copyright file="Box3.cs" company="3Dconnexion">
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
    using Point3 = OpenTK.Vector3;

    /// <summary>
    /// Represents a 3D Box.
    /// </summary>
    public struct Box3
    {
        /// <summary>
        /// Gets or sets the bounds vertex with the smallest x, y and z values.
        /// </summary>
        public Point3 Min { get; set; }

        /// <summary>
        /// Gets or sets the bounds vertex with the largest x, y and z values.
        /// </summary>
        public Point3 Max { get; set; }
    }
}
