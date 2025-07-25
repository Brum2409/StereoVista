// <copyright file="Model3D.cs" company="3Dconnexion">
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
// $Id: Model3D.cs 15000 2018-05-15 12:04:24Z mbonk $
//
// </history>

namespace TDx.TestNL.ModelLoader.Geometry
{
    using System.Collections.Generic;
    using TDx.TestNL.ModelLoader.Visualization;

    /// <summary>
    /// Container for a 3D model
    /// </summary>
    public class Model3D
    {
        /// <summary>
        /// Initializes a new instance of the <see cref="Model3D"/> class.
        /// </summary>
        public Model3D()
        {
            this.Meshes = new List<Mesh>();
            this.Materials = new Dictionary<string, Material>();
        }

        /// <summary>
        /// Gets or sets the models meshes
        /// </summary>
        public IList<Mesh> Meshes { get; set; }

        /// <summary>
        /// Gets or sets the models materials.
        /// </summary>
        public IDictionary<string, Material> Materials { get; set; }

        /// <summary>
        /// Gets the material associated with a material name.
        /// </summary>
        /// <param name="name">The name of the <see cref="Material"/>.</param>
        /// <returns>The <see cref="Material"/>.</returns>
        public Material GetMaterial(string name)
        {
            if (!string.IsNullOrEmpty(name))
            {
                if (this.Materials.ContainsKey(name))
                {
                    return this.Materials[name];
                }
            }

            return Material.DefaultMaterial;
        }
    }
}