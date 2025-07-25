// <copyright file="HitTestExecutor.cs" company="3Dconnexion">
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
// $Id: HitTestExecutor.cs 15519 2018-11-12 13:29:59Z mbonk $
//
// </history>

namespace TDx.TestNL.HitTesting
{
    using System.Collections.Generic;
    using System.Linq;
    using System.Windows.Media.Media3D;

    /// <summary>
    /// Class to perform surface hit testing
    /// </summary>
    internal class HitTestExecutor
    {
        private readonly HitPostProcessor hitPostProcessor;
        private IDictionary<MeshGeometry3D, IList<Triangle>> triangles;
        private Point3D hitPoint;
        private ApertureRay hitRay;
        private Model3DGroup model;

        public HitTestExecutor()
        {
            this.hitPostProcessor = new HitPostProcessor();
        }

        /// <summary>
        /// Gets or sets the model to perform hit-testing on.
        /// </summary>
        public Model3DGroup Model
        {
            get
            {
                return this.model;
            }

            set
            {
                this.model = value;
                this.LoadMeshData();
            }
        }

        public Point3D HitPoint
        {
            get { return this.hitPoint; }
        }

        public bool HitTest(ApertureRay hitRay)
        {
            this.hitRay = hitRay;

            return this.model.Dispatcher.Invoke(this.GeometryHitTest);
        }

        private void LoadMeshData()
        {
            this.triangles = new Dictionary<MeshGeometry3D, IList<Triangle>>();
            if (this.model == null)
            {
                return;
            }

            foreach (GeometryModel3D child in this.model.Children)
            {
                var mesh = child.Geometry as MeshGeometry3D;
                this.triangles[mesh] = new List<Triangle>();

                for (int i = 0; i < mesh.TriangleIndices.Count - 2; i += 3)
                {
                    this.triangles[mesh].Add(new Triangle(
                        mesh.Positions[mesh.TriangleIndices[i]],
                        mesh.Positions[mesh.TriangleIndices[i + 1]],
                        mesh.Positions[mesh.TriangleIndices[i + 2]]));
                }
            }
        }

        private bool GeometryHitTest()
        {
            var result = new HashSet<HitTestResult>();

            var intersectedMeshes = this.triangles.Keys.Where(m => this.RayAabbIntersected(m.Bounds));
            foreach (var mesh in intersectedMeshes)
            {
                foreach (var triangle in this.triangles[mesh])
                {
                    HitTestResult hitResult = triangle.HitTest(this.hitRay);
                    if (hitResult.HitLocation != HitLocation.None)
                    {
                        result.Add(hitResult);
                    }
                }
            }

            this.hitPostProcessor.HitRay = this.hitRay;
            bool success = this.hitPostProcessor.GetHitPoint(result, out this.hitPoint);

            return success;
        }

        private bool RayAabbIntersected(Rect3D box)
        {
            double xMin, xMax, yMin, yMax, zMin, zMax;

            var bounds = new[]
            {
                new Point3D(box.X, box.Y, box.Z),
                new Point3D(box.X + box.SizeX, box.Y + box.SizeY, box.Z + box.SizeZ)
            };

            var closeDist = (this.hitRay.Origin - bounds[0]).Length;
            var farDist = (this.hitRay.Origin - bounds[0]).Length;

            double radius = this.hitRay.GetRadius((closeDist + farDist) / 2);

            bounds[0] = Point3D.Add(bounds[0], new Vector3D(-radius, -radius, -radius));
            bounds[1] = Point3D.Add(bounds[1], new Vector3D(radius, radius, radius));

            var invertedDir = new Vector3D(
                1 / this.hitRay.Direction.X,
                1 / this.hitRay.Direction.Y,
                1 / this.hitRay.Direction.Z);

            int[] sign = new[]
            {
                invertedDir.X < 0 ? 1 : 0,
                invertedDir.Y < 0 ? 1 : 0,
                invertedDir.Z < 0 ? 1 : 0
            };

            xMin = (bounds[sign[0]].X - this.hitRay.Origin.X) * invertedDir.X;
            xMax = (bounds[1 - sign[0]].X - this.hitRay.Origin.X) * invertedDir.X;
            yMin = (bounds[sign[1]].Y - this.hitRay.Origin.Y) * invertedDir.Y;
            yMax = (bounds[1 - sign[1]].Y - this.hitRay.Origin.Y) * invertedDir.Y;

            if ((xMin > yMax) || (yMin > xMax))
            {
                return false;
            }

            if (yMin > xMin)
            {
                xMin = yMin;
            }

            if (yMax < xMax)
            {
                xMax = yMax;
            }

            zMin = (bounds[sign[2]].Z - this.hitRay.Origin.Z) * invertedDir.Z;
            zMax = (bounds[1 - sign[2]].Z - this.hitRay.Origin.Z) * invertedDir.Z;

            if ((xMin > zMax) || (zMin > xMax))
            {
                return false;
            }

            if (zMin > xMin)
            {
                xMin = zMin;
            }

            if (zMax < xMax)
            {
                xMax = zMax;
            }

            return true;
        }
    }
}