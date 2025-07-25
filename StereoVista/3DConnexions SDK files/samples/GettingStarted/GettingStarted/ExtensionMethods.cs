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

        /// <summary>
        /// Calculates the first forward intersection of a <see cref="ConeBeam3"/> with an axis aligned
        /// bounding <see cref="Box3"/>.
        /// </summary>
        /// <param name="c">The <see cref="ConeBeam3"/>.</param>
        /// <param name="box">The axis aligned bounding <see cref="Box3"/>.</param>
        /// <param name="p">The <see cref="Point3"/> of intersection.</param>
        /// <returns>true if there is an intersection, otherwise false.</returns>
        /// <remarks>
        /// Based on <see cref="Intersects(Ray3, Box3, out Point3)"/>. If a direct hit fails the
        /// vector is adjusted within the bounds of the beam to hit an edge.
        /// </remarks>
        public static bool Intersects(this ConeBeam3 c, Box3 box, out Point3 p)
        {
            Ray3 r = new Ray3
            {
                Direction = c.Direction,
                Origin = c.Origin,
            };

            p = default(Point3);

            float tmin, tmax, txmin, txmax, tymin, tymax, tzmin, tzmax;
            float xmin, xmax, ymin, ymax, zmin, zmax;

            float divx = 1 / r.Direction.X;
            if (divx >= 0)
            {
                xmin = box.Min.X - r.Origin.X;
                xmax = box.Max.X - r.Origin.X;
            }
            else
            {
                xmin = box.Max.X - r.Origin.X;
                xmax = box.Min.X - r.Origin.X;
            }

            txmin = xmin * divx;
            txmax = xmax * divx;

            float divy = 1 / r.Direction.Y;
            if (divy >= 0)
            {
                ymin = box.Min.Y - r.Origin.Y;
                ymax = box.Max.Y - r.Origin.Y;
            }
            else
            {
                ymin = box.Max.Y - r.Origin.Y;
                ymax = box.Min.Y - r.Origin.Y;
            }

            tymin = ymin * divy;
            tymax = ymax * divy;

            if ((txmin > tymax) || (tymin > txmax))
            {
                // Cannot hit the box directly, we need to adjust the ray direction.
                float dx = txmin > tymax ? xmin : xmax;
                float dy = txmin > tymax ? ymax : ymin;

                // Calculate t so that the normal to the line hits the (x, y) edge.
                float t = ((r.Direction.X * dx) + (r.Direction.Y * dy)) / ((r.Direction.X * r.Direction.X) + (r.Direction.Y * r.Direction.Y));

                // The length of the normal
                float d = Math.Abs(((r.Direction.X * t) - dx) / r.Direction.Y);
                if (t * c.Radius < d)
                {
#if TRACE
                    Trace.WriteLine("Outside cone t=" + t + ", t*c.Radius=" + (t * c.Radius) + ", d=" + d);
#endif

                    // Outside of the cone
                    return false;
                }

                // Adjust the ray so that it hits the edge
                r.Direction = new Vector3(dx / t, dy / t, r.Direction.Z);

                tymin = ymin * t / dy;
                tymax = ymax * t / dy;
                txmin = xmin * t / dx;
                txmax = xmax * t / dx;
            }

            tmin = tymin > txmin ? tymin : txmin;
            tmax = tymax < txmax ? tymax : txmax;

            float divz = 1 / r.Direction.Z;
            if (divz >= 0)
            {
                zmin = box.Min.Z - r.Origin.Z;
                zmax = box.Max.Z - r.Origin.Z;
            }
            else
            {
                zmin = box.Max.Z - r.Origin.Z;
                zmax = box.Min.Z - r.Origin.Z;
            }

            tzmin = zmin * divz;
            tzmax = zmax * divz;

            if ((tmin > tzmax) || (tzmin > tmax))
            {
                // Cannot hit the box directly, we need to adjust the ray direction.
                float dw, wdir;
                float dz;
                bool isY;
                if (tmin > tzmax)
                {
                    dz = zmax;
                    isY = tymin > txmin;
                    dw = isY ? ymin : xmin;
                    wdir = isY ? r.Direction.Y : r.Direction.X;
                }
                else
                {
                    dz = zmin;
                    isY = tymax < txmax;
                    dw = isY ? ymax : xmax;
                    wdir = isY ? r.Direction.Y : r.Direction.X;
                }

                // Calculate t so that the normal to the line hits the (w, z) edge.
                float t = ((wdir * dw) + (r.Direction.Z * dz)) / ((wdir * wdir) + (r.Direction.Z * r.Direction.Z));

                // The length of the normal
                float d = Math.Abs(((wdir * t) - dw) / r.Direction.Z);
                if (t * c.Radius < d)
                {
#if TRACE
                    Trace.WriteLine("Outside cone t=" + t + ", t*c.Radius=" + (t * c.Radius) + ", d=" + d);
#endif

                    // Outside of the cone
                    return false;
                }

                if (isY)
                {
                    // Adjust the ray so that it hits the edge
                    r.Direction = new Vector3(r.Direction.X, dw / t, dz / t);
                    if (tmin > tzmax)
                    {
                        // tmin = tymin;
                        tmin = ymin * t / dw;
                    }
                    else
                    {
                        // tmax = tymax;
                        tmax = ymax * t / dw;
                    }
                }
                else
                {
                    // Adjust the ray so that it hits the corner
                    r.Direction = new Vector3(dw / t, r.Direction.Y, dz / t);
                    if (tmin > tzmax)
                    {
                        // tmin = txmin;
                        tmin = xmin * t / dw;
                    }
                    else
                    {
                        // tmax = txmax;
                        tmax = xmax * t / dw;
                    }
                }

                tzmin = zmin * t / dz;
                tzmax = zmax * t / dz;
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
#if TRACE
            Point3 pc = c.Origin + (tmin * c.Direction);
            Vector3 delta = pc - p;
            if (delta.Length > tmin * c.Radius)
            {
                Trace.WriteLine("Outside cone delta=" + delta + ", tmin * c.Radius=" + (tmin * c.Radius) + ", delta.Length=" + delta.Length);
            }
#endif
            return true;
        }
    }
}
