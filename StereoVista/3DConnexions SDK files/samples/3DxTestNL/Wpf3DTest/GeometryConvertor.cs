// <copyright file="GeometryConvertor.cs" company="3Dconnexion">
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
// $Id: GeometryConvertor.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL
{
    using System.Windows;
    using System.Windows.Media;
    using System.Windows.Media.Media3D;
    using TDx.TestNL.ModelLoader;
    using ModelData = TDx.TestNL.ModelLoader.Geometry.Model3D;

    /// <summary>
    /// This class converts data read by <see cref="IModelProvider"/>
    /// (in particular, <see cref="TDx.TestNL.ModelLoader.Readers.Reader"/> derived classes)
    /// to WPF understandable <see cref="Model3DGroup"/> instances.
    /// </summary>
    internal static class GeometryConvertor
    {
        /// <summary>
        /// Creates a new <see cref="Model3DGroup"/> instance from model data.
        /// </summary>
        /// <param name="modelProvider">The model provider instance.</param>
        /// <returns>A new <see cref="Model3DGroup"/> instance.</returns>
        public static Model3DGroup Model3DGroup(IModelProvider modelProvider)
        {
            var result = new Model3DGroup();

            try
            {
                ModelData model = modelProvider.Model;
                foreach (var mesh in model.Meshes)
                {
                    var wpfMesh = new MeshGeometry3D
                    {
                        // the model provider's guarantees are facets and vertices
                        Positions = new Point3DCollection(mesh.Vertices),
                        TriangleIndices = new Int32Collection(mesh.Facets),

                        // UVs and Normals are optional
                        TextureCoordinates = mesh.UVs != null ? new PointCollection(mesh.UVs) : null,
                        Normals = mesh.Normals != null ? new Vector3DCollection(mesh.Normals) : null
                    };

                    string materialName = mesh.MaterialName;
                    Material meshMaterial = new DiffuseMaterial(new SolidColorBrush(model.GetMaterial(materialName).Diffuse));

                    // Disable back face culling by specifying a back material
                    result.Children.Add(new GeometryModel3D(wpfMesh, meshMaterial) { BackMaterial = meshMaterial });
                }
            }
            catch
            {
                MessageBox.Show("The was an error loading the model. Please try a different model.", "Geometry loader");
            }

            return result;
        }
    }
}