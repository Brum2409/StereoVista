// <copyright file="ExtensionMethods.cs" company="3Dconnexion">
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
// $Id: ExtensionMethods.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.Extensions
{
    using System.Windows.Media.Media3D;

    /// <summary>
    /// Class for Extension Methods.
    /// </summary>
    public static class ExtensionMethods
    {
        /// <summary>
        /// Epsilon for testing if zero.
        /// </summary>
        public const double Epsilon = 1e-6;

        /// <summary>
        /// Test if the absolute value is less than epsilon.
        /// </summary>
        /// <param name="d">value to test.</param>
        /// <returns>true is the d lies within the +/- epsilon.</returns>
        public static bool ConsideredZero(this double d)
        {
            return d > -Epsilon && d < Epsilon;
        }

        /// <summary>
        /// Test if the absolute difference of two values is less than epsilon.
        /// </summary>
        /// <param name="d">value to compare to.</param>
        /// <param name="other">second value to compare to.</param>
        /// <returns>true is the difference lies within the +/- epsilon.</returns>
        public static bool ConsideredEqual(this double d, double other)
        {
            return (d - other).ConsideredZero();
        }

        /// <summary>
        /// Test whether the Point3D value is NaN.
        /// </summary>
        /// <param name="value">The <see cref="Point3D"/> to test.</param>
        /// <returns>true if any component is NaN.</returns>
        public static bool IsNaN(this Point3D value)
        {
            return double.IsNaN(value.X) || double.IsNaN(value.Y) || double.IsNaN(value.Z);
        }
    }
}