// <copyright file="Mesh.cs" company="3Dconnexion">
// ------------------------------------------------------------------------------------
// Copyright (c) 2018 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer
// Kit", including all accompanying documentation, and is protected by intellectual
// property laws. All use of the 3Dconnexion Software Developer Kit is subject to the
// License Agreement found in the "LicenseAgreementSDK.txt" file.
// All rights not expressly granted by 3Dconnexion are reserved.
// ------------------------------------------------------------------------------------
// </copyright>
// <history>
// *************************************************************************************
// File History
//
// $Id: Mesh.cs 15000 2018-05-15 12:04:24Z mbonk $
//
// </history>

namespace TDx.TestNL.ModelLoader.Geometry
{
    using System.Windows;
    using System.Windows.Media.Media3D;

    public class Mesh
    {
        public Point3D[] Vertices { get; set; }

        /// <summary>
        /// Gets or sets the indexes in the <see cref="Vertices"/> array that define
        /// the triangles' vertices.
        /// </summary>
        public int[] Facets { get; set; }

        public Vector3D[] Normals { get; set; }

        /// <summary>
        /// Gets or sets the texture coordinates.
        /// </summary>
        public Point[] UVs { get; set; }

        public string MaterialName { get; set; }
    }
}