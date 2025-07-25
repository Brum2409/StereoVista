// <copyright file="Triangle.cs" company="3Dconnexion">
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
// $Id: Triangle.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.HitTesting
{
    using System;
    using System.Linq;
    using System.Windows.Media.Media3D;
    using TDx.TestNL.Extensions;

    internal class Triangle : IEquatable<Triangle>
    {
        private readonly Point3D[] vertices;

        private Vector3D? normal;
        private ApertureRay hitRay;

        public Triangle()
        {
            this.vertices = new Point3D[3];
        }

        public Triangle(Point3D[] vertices)
        {
            this.vertices = vertices;
        }

        public Triangle(Point3D p1, Point3D p2, Point3D p3)
            : this()
        {
            this.vertices[0] = p1;
            this.vertices[1] = p2;
            this.vertices[2] = p3;
        }

        public Point3D[] Vertices => this.vertices;

        public Point3D V1 => this.vertices[0];

        public Point3D V2 => this.vertices[1];

        public Point3D V3 => this.vertices[2];

        public Vector3D Normal
        {
            get
            {
                if (!this.normal.HasValue)
                {
                    this.CalculateNormal();
                }

                return this.normal.Value;
            }
        }

        public double MaxLength
        {
            get
            {
                Vector3D e1 = this.V2 - this.V1;
                Vector3D e2 = this.V3 - this.V1;
                Vector3D e3 = this.V3 - this.V2;

                double[] lengths = { e1.Length, e2.Length, e3.Length };
                return lengths.Max();
            }
        }

        public static double Area(Point3D p1, Point3D p2, Point3D p3) => Vector3D.CrossProduct(p2 - p1, p3 - p1).Length / 2;

        public bool Equals(Triangle other)
        {
            int equalities = 0;

            foreach (var p1 in this.Vertices)
            {
                foreach (var p2 in other.Vertices)
                {
                    if (p1 == p2)
                    {
                        equalities++;
                        break;
                    }
                }
            }

            return equalities == 3;
        }

        public override bool Equals(object obj)
        {
            var other = obj as Triangle;
            if (other == null)
            {
                return false;
            }

            if (this.GetHashCode() != other.GetHashCode())
            {
                return false;
            }

            return this.Equals(other);
        }

        public override int GetHashCode()
        {
            unchecked
            {
                var hash = this.V1.GetHashCode() ^ this.V2.GetHashCode() ^ this.V3.GetHashCode();
                return hash;
            }
        }

        /// <summary>
        /// Perform hit testing on this triangle.
        /// </summary>
        /// <param name="ray">Ray that is supposed to hot triangle.</param>
        /// <returns>Distance along the ray where the hit occurred. '-1' if there was no hit.</returns>
        public HitTestResult HitTest(ApertureRay ray)
        {
            this.hitRay = ray;

            if (!this.BoundingTest())
            {
                return new HitTestResult { HitLocation = HitLocation.None };
            }

            var result = new HitTestResult { Triangle = this };
            double hitDistance;

            hitDistance = this.TriangleTest();
            result.HitLocation = HitLocation.Triangle;

            if (hitDistance == -1)
            {
                hitDistance = this.TestEdges();
                result.HitLocation = HitLocation.Cylinder;
            }

            if (hitDistance == -1)
            {
                hitDistance = this.TestVertices();
                result.HitLocation = HitLocation.Sphere;
            }

            if (hitDistance == -1)
            {
                result.HitLocation = HitLocation.None;
            }

            result.Triangle = this;
            result.HitDistance = hitDistance;

            return result;
        }

        public void CalculateNormal()
        {
            Vector3D e1 = this.V2 - this.V1;
            Vector3D e2 = this.V3 - this.V1;

            this.normal = Vector3D.CrossProduct(e1, e2);
            this.normal.Value.Normalize();
        }

        public bool Contains(Point3D pt)
        {
            double s = Area(this.V1, this.V2, this.V3);

            double s1 = Area(pt, this.V2, this.V3);
            double s2 = Area(this.V1, pt, this.V3);
            double s3 = Area(this.V1, this.V2, pt);

            return s.ConsideredEqual(s1 + s2 + s3);
        }

        private bool BoundingTest()
        {
            // Source: https://en.wikipedia.org/wiki/Circumscribed_circle.
            // Find Circumscribed sphere.
            Vector3D vBA = this.V1 - this.V2;
            Vector3D vCB = this.V2 - this.V3;
            Vector3D vCA = this.V1 - this.V3;

            double lenBAcrossCB = Vector3D.CrossProduct(vBA, vCB).Length;

            double lenBA = vBA.Length;
            double lenCB = vCB.Length;
            double lenCA = vCA.Length;

            double denominator = 2 * lenBAcrossCB * lenBAcrossCB;

            double alpha = (lenCB * lenCB * Vector3D.DotProduct(vBA, vCA)) / denominator;
            double beta = (lenCA * lenCA * Vector3D.DotProduct(this.V2 - this.V1, vCB)) / denominator;
            double gamma = (lenBA * lenBA * Vector3D.DotProduct(this.V3 - this.V1, this.V3 - this.V2)) / denominator;

            double radius = (lenBA * lenCB * lenCA) / (2 * lenBAcrossCB);
            var center = (Point3D)(
                Vector3D.Multiply((Vector3D)this.V1, alpha) +
                Vector3D.Multiply((Vector3D)this.V2, beta) +
                Vector3D.Multiply((Vector3D)this.V3, gamma));

            return this.SphereTest(center, radius + (this.hitRay.GetRadius((this.hitRay.Origin - center).Length) * 0.5)) != -1;
        }

        private double TriangleTest()
        {
            double a, f, u, v;
            Vector3D h, s, q;

            Vector3D edge1 = this.V2 - this.V1;
            Vector3D edge2 = this.V3 - this.V1;
            h = Vector3D.CrossProduct(this.hitRay.Direction, edge2);
            a = Vector3D.DotProduct(edge1, h);

            if (a.ConsideredZero())
            {
                return -1;
            }

            f = 1 / a;
            s = this.hitRay.Origin - this.V1;
            u = f * Vector3D.DotProduct(s, h);

            if (u < 0 || u > 1)
            {
                return -1;
            }

            q = Vector3D.CrossProduct(s, edge1);
            v = f * Vector3D.DotProduct(this.hitRay.Direction, q);

            if (v < 0 || u + v > 1)
            {
                return -1;
            }

            double t = f * Vector3D.DotProduct(edge2, q);

            return t > ExtensionMethods.Epsilon ? t : -1;
        }

        private double TestEdges()
        {
            double hitDistance = this.CylinderTest(this.V1, this.V2);

            if (hitDistance == -1)
            {
                hitDistance = this.CylinderTest(this.V2, this.V3);
            }

            if (hitDistance == -1)
            {
                hitDistance = this.CylinderTest(this.V1, this.V3);
            }

            return hitDistance;
        }

        private double CylinderTest(Point3D p1, Point3D p2)
        {
            Vector3D vAB = p2 - p1;
            Vector3D vAO = this.hitRay.Origin - p1;
            Vector3D vAOcrossAB = Vector3D.CrossProduct(vAO, vAB);
            Vector3D rayDirCrossAB = Vector3D.CrossProduct(this.hitRay.Direction, vAB);

            double firstDistance = (p1 - this.hitRay.Origin).Length;
            double secondDistance = (p2 - this.hitRay.Origin).Length;

            double radius = this.hitRay.GetRadius((firstDistance + secondDistance) / 2);
            double vAB2 = Vector3D.DotProduct(vAB, vAB);

            double a = Vector3D.DotProduct(rayDirCrossAB, rayDirCrossAB);
            double b = 2 * Vector3D.DotProduct(rayDirCrossAB, vAOcrossAB);
            double c = Vector3D.DotProduct(vAOcrossAB, vAOcrossAB) - (radius * radius * vAB2);

            return this.FindHitDistanceFromEquation(a, b, c);
        }

        private double TestVertices()
        {
            double hitDistance = this.SphereTest(this.V1);

            if (hitDistance == -1)
            {
                hitDistance = this.SphereTest(this.V2);
            }

            if (hitDistance == -1)
            {
                hitDistance = this.SphereTest(this.V3);
            }

            return hitDistance;
        }

        private double SphereTest(Point3D o)
        {
            double radius = this.hitRay.GetRadius((o - this.hitRay.Origin).Length);
            return this.SphereTest(o, radius);
        }

        private double SphereTest(Point3D o, double radius)
        {
            Vector3D originsSegment = this.hitRay.Origin - o;

            double b = 2 * Vector3D.DotProduct(this.hitRay.Direction, originsSegment);
            double c = Vector3D.DotProduct(originsSegment, originsSegment) - (radius * radius);

            return this.FindHitDistanceFromEquation(1, b, c);
        }

        private double FindHitDistanceFromEquation(double a, double b, double c)
        {
            if (a != 1)
            {
                b = b / a;
                c = c / a;
            }

            double discriminant = (b * b) - (4 * c);
            if (discriminant < 0)
            {
                return -1;
            }

            double result = (-b - Math.Sqrt(discriminant)) / 2;

            if (result < ExtensionMethods.Epsilon)
            {
                if (discriminant.ConsideredZero())
                {
                    return -1;
                }
                else
                {
                    result = (-b + Math.Sqrt(discriminant)) / 2;
                }
            }

            return result > ExtensionMethods.Epsilon ? result : -1;
        }
    }
}