// <copyright file="ExtensionMethods.cs" company="3Dconnexion">
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
    using System;
#if TRACE
    using System.Diagnostics;
#endif
    using OpenTK;
    using Point3 = OpenTK.Vector3;

    /// <summary>
    /// Extension methods
    /// </summary>
    internal static class ExtensionMethods
    {
        /// <summary>
        /// Calculates the first forward intersection of a <see cref="Ray3"/> with an axis aligned
        /// bounding <see cref="Box3"/>.
        /// </summary>
        /// <param name="r">The <see cref="Ray3"/>.</param>
        /// <param name="box">The axis aligned bounding <see cref="Box3"/>.</param>
        /// <param name="p">The <see cref="Point3"/> of intersection.</param>
        /// <returns>true if there is an intersection, otherwise false.</returns>
        /// <remarks>
        /// Based on "An Efficient and Robust Ray–Box Intersection Algorithm." Authors: Amy Williams,
        /// Steve Barrus, R. Keith Morley, Peter Shirley. University of Utah.
        /// </remarks>
        public static bool Intersects(this Ray3 r, Box3 box, out Point3 p)
        {
            p = default(Point3);

            float tmin, tmax, tymin, tymax, tzmin, tzmax;

            float divx = 1 / r.Direction.X;
            if (divx >= 0)
            {
                tmin = (box.Min.X - r.Origin.X) * divx;
                tmax = (box.Max.X - r.Origin.X) * divx;
            }
            else
            {
                tmin = (box.Max.X - r.Origin.X) * divx;
                tmax = (box.Min.X - r.Origin.X) * divx;
            }

            float divy = 1 / r.Direction.Y;
            if (divy >= 0)
            {
                tymin = (box.Min.Y - r.Origin.Y) * divy;
                tymax = (box.Max.Y - r.Origin.Y) * divy;
            }
            else
            {
                tymin = (box.Max.Y - r.Origin.Y) * divy;
                tymax = (box.Min.Y - r.Origin.Y) * divy;
            }

            if ((tmin > tymax) || (tymin > tmax))
            {
                return false;
            }

            if (tymin > tmin)
            {
                tmin = tymin;
            }

            if (tymax < tmax)
            {
                tmax = tymax;
            }

            float divz = 1 / r.Direction.Z;
            if (divz >= 0)
            {
                tzmin = (box.Min.Z - r.Origin.Z) * divz;
                tzmax = (box.Max.Z - r.Origin.Z) * divz;
            }
            else
            {
                tzmin = (box.Max.Z - r.Origin.Z) * divz;
                tzmax = (box.Min.Z - r.Origin.Z) * divz;
            }

            if ((tmin > tzmax) || (tzmin > tmax))
            {
                return false;
            }

            if (tzmin > tmin)
            {
                tmin = tzmin;
            }

            if (tzmax < tmax)
            {
                tmax = tzmax;
            }

            if (tmin < 0)
            {
                tmin = tmax;
                if (tmin < 0)
                {
                    return false;
                }
            }

            p = r.Origin + (tmin * r.Direction);

            return true;
        }
    }
}
