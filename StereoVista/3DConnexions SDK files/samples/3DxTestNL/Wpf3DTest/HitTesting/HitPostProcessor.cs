// <copyright file="HitPostProcessor.cs" company="3Dconnexion">
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
// $Id: HitPostProcessor.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.HitTesting
{
    using System.Collections.Generic;
    using System.Linq;
    using System.Windows.Media.Media3D;

    /// <summary>
    /// Post processor for hit testing.
    /// </summary>
    internal class HitPostProcessor
    {
        private ApertureRay ray;

        private HitTestResult currentResult;
        private Point3D currentHitPoint;
        private Point3D potentialHit;
        private double rayRadius;
        private double hitDistance;

        /// <summary>
        /// Gets or sets the ray used for hit testing.
        /// </summary>
        public ApertureRay HitRay
        {
            get { return this.ray; }
            set { this.ray = value; }
        }

        /// <summary>
        /// Gets or sets
        /// </summary>
        private Point3D CurrentHitPoint
        {
            get
            {
                return this.currentHitPoint;
            }

            set
            {
                this.currentHitPoint = value;

                this.hitDistance = (this.ray.Origin - this.currentHitPoint).Length;
                this.rayRadius = this.ray.GetRadius(this.hitDistance);
            }
        }

        /// <summary>
        /// Gets the hit point.
        /// </summary>
        /// <param name="hitResults">List of hit results.</param>
        /// <param name="hitPoint">The point hit.</param>
        /// <returns>true if successful otherwise false.</returns>
        public bool GetHitPoint(IEnumerable<HitTestResult> hitResults, out Point3D hitPoint)
        {
            var hitPoints = new HashSet<Point3D>();
            bool pointFound = false;

            hitResults = hitResults.OrderBy(h => h.HitDistance);
            double? firstHitDistance = hitResults.FirstOrDefault()?.HitDistance;

            foreach (var result in hitResults)
            {
                if (result.HitDistance - firstHitDistance < result.Triangle.MaxLength || !pointFound)
                {
                    this.currentResult = result;
                    Point3D outParam;
                    pointFound = this.GetHitPoint(out outParam);
                    if (pointFound)
                    {
                        hitPoints.Add(outParam);
                    }
                }
                else
                {
                    break;
                }
            }

            hitPoint = this.PostProcessPoints(hitPoints);
            return pointFound;
        }

        private bool GetHitPoint(out Point3D hitPoint)
        {
            try
            {
                this.CurrentHitPoint = this.CalculateHitPoint(this.currentResult.HitDistance);

                if (this.currentResult.HitLocation == HitLocation.Sphere)
                {
                    hitPoint = this.CalculateSphereHit();
                    return true;
                }

                return this.CalculateHit(out hitPoint);
            }
            catch (System.Exception)
            {
                hitPoint = default(Point3D);
                return false;
            }
        }

        private Point3D CalculateHitPoint(double distance)
        {
            return this.ray.Origin + Vector3D.Multiply(distance, this.ray.Direction);
        }

        private Point3D CalculateSphereHit()
        {
            var sphereCenter = default(Point3D);
            var otherVertices = new Point3D[2];
            int arrayPosition = 0;
            double projectionLength;

            int index = this.GetVertexIndex(out projectionLength);

            for (int i = 0; i < 3; i++)
            {
                if (i == index)
                {
                    sphereCenter = this.currentResult.Triangle.Vertices[i];
                }
                else
                {
                    otherVertices[arrayPosition++] = this.currentResult.Triangle.Vertices[i];
                }
            }

            var pointsToTest = new Point3D[3];
            pointsToTest[0] = sphereCenter;

            for (int i = 0; i < 2; i++)
            {
                var direction = otherVertices[i] - sphereCenter;
                double length = this.rayRadius - projectionLength;
                pointsToTest[i + 1] = sphereCenter + Vector3D.Multiply(length, direction);
            }

            return this.PostProcessPoints(pointsToTest);
        }

        private int GetVertexIndex(out double projectionLength)
        {
            projectionLength = (this.currentHitPoint - this.currentResult.Triangle.Vertices[0]).Length;
            int vertex = 0;
            for (int i = 1; i < 3; i++)
            {
                double length = (this.currentHitPoint - this.currentResult.Triangle.Vertices[i]).Length;
                if (length < projectionLength)
                {
                    projectionLength = length;
                    vertex = i;
                }
            }

            if (projectionLength < this.rayRadius)
            {
                return vertex;
            }

            throw new System.Exception("Vertex is too far from the sphere center.");
        }

        private bool CalculateHit(out Point3D hitPoint)
        {
            Vector3D slopeDir = this.GetSlopeDirection();
            this.potentialHit = this.currentHitPoint + Vector3D.Multiply(this.rayRadius, slopeDir);

            if (this.currentResult.Triangle.Contains(this.potentialHit))
            {
                hitPoint = this.potentialHit;
                return true;
            }

            // Key: distance from projected point to projection point (to edge).
            // Value: projected point.
            var dictionary = new SortedDictionary<double, Point3D>();

            var triangle = this.currentResult.Triangle;

            this.AddProjectedPoint(dictionary, triangle.V1, triangle.V2);
            this.AddProjectedPoint(dictionary, triangle.V1, triangle.V3);
            this.AddProjectedPoint(dictionary, triangle.V2, triangle.V3);

            if (dictionary.Count > 0)
            {
                hitPoint = dictionary.First().Value;
                return true;
            }
            else
            {
                hitPoint = default(Point3D);
                return false;
            }
        }

        private void AddProjectedPoint(SortedDictionary<double, Point3D> dictionary, Point3D p1, Point3D p2)
        {
            Point3D projectedPoint = this.Project(this.potentialHit, p1, p2);

            double projectionDistance = (this.potentialHit - projectedPoint).Length;
            if (projectionDistance <= this.ray.GetRadius(this.hitDistance))
            {
                if (dictionary.ContainsKey(projectionDistance))
                {
                    dictionary[projectionDistance] = this.PostProcessPoints(new[] { projectedPoint, dictionary[projectionDistance] });
                }
                else
                {
                    dictionary[projectionDistance] = projectedPoint;
                }
            }
        }

        private Vector3D GetSlopeDirection()
        {
            var crossVector = Vector3D.CrossProduct(this.ray.Direction, this.currentResult.Triangle.Normal);
            if (crossVector.Length == 0)
            {
                return new Vector3D(0, 0, 0);   // That will prevent point from moving.
            }

            var slopeDir = Vector3D.CrossProduct(crossVector, this.currentResult.Triangle.Normal);
            slopeDir.Normalize();
            return slopeDir;
        }

        private Point3D Project(Point3D pointToProject, Point3D p1, Point3D p2)
        {
            Vector3D ab = p2 - p1;
            Vector3D ap = pointToProject - p1;
            double abSquared = Vector3D.DotProduct(ab, ab);

            double t = Vector3D.DotProduct(ap, ab) / abSquared;

            if (t < 0)
            {
                return p1;
            }
            else if (t > 1)
            {
                return p2;
            }
            else
            {
                return p1 + (t * ab);
            }
        }

        private Point3D PostProcessPoints(IEnumerable<Point3D> points)
        {
            double currentDistance = double.MaxValue;
            var currentPoint = default(Point3D);

            foreach (var pt in points)
            {
                var length = (this.ray.Origin - pt).Length;
                if (currentDistance > length)
                {
                    currentDistance = length;
                    currentPoint = pt;
                }
            }

            return currentPoint;
        }
    }
}