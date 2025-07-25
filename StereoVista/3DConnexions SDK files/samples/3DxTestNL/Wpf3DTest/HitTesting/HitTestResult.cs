// <copyright file="HitTestResult.cs" company="3Dconnexion">
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
// $Id: HitTestResult.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.HitTesting
{
    /// <summary>
    /// The location of the hit.
    /// </summary>
    internal enum HitLocation
    {
        /// <summary>
        /// No hit.
        /// </summary>
        None,

        /// <summary>
        /// <see cref="Triangle"/>.
        /// </summary>
        Triangle,

        /// <summary>
        /// <see cref="Cylinder"/>.
        /// </summary>
        Cylinder,

        /// <summary>
        /// <see cref="Sphere"/>
        /// </summary>
        Sphere
    }

    internal class HitTestResult
    {
        public double HitDistance { get; set; }

        public HitLocation HitLocation { get; set; }

        public Triangle Triangle { get; set; }

        public override int GetHashCode()
        {
            return this.Triangle.GetHashCode();
        }

        public override bool Equals(object obj)
        {
            var other = obj as HitTestResult;
            if (other == null)
            {
                return false;
            }

            return this.Triangle.Equals(other.Triangle);
        }
    }
}