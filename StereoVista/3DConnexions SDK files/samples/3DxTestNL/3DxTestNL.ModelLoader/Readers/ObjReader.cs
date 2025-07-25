// <copyright file="ObjReader.cs" company="3Dconnexion">
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
// $Id: ObjReader.cs 15149 2018-07-02 08:29:31Z mbonk $
//
// </history>

namespace TDx.TestNL.ModelLoader.Readers
{
    using System;
    using System.Collections.Generic;
    using System.ComponentModel;
    using System.IO;
    using System.Windows;
    using System.Windows.Media.Media3D;
    using TDx.TestNL.ModelLoader.Geometry;
    using ModelMaterial = Visualization.Material;

    /// <summary>
    /// This implementation is an adapted version of the LoadObj method
    /// from the 3DxViewer project.
    /// </summary>
    public class ObjReader : Reader
    {
        /* OBJ file tags */
        private const string O = "o";
        private const string G = "g";
        private const string V = "v";
        private const string VT = "vt";
        private const string VN = "vn";
        private const string F = "f";
        private const string MTL = "mtllib";
        private const string UML = "usemtl";

        /* MTL file tags */
        private const string NML = "newmtl";
        private const string NS = "Ns"; // Shininess
        private const string KA = "Ka"; // Ambient component (not supported)
        private const string KD = "Kd"; // Diffuse component
        private const string KS = "Ks"; // Specular component
        private const string D = "d";   // Transparency (not supported)
        private const string TR = "Tr"; // Same as 'd'
        private const string ILLUM = "illum"; // Illumination model. 1 - diffuse, 2 - specular
        private const string MAPKD = "map_Kd"; // Diffuse texture (other textures are not supported)

        private string mtlLib;

        protected override void WorkerCallback(object sender, DoWorkEventArgs e)
        {
            this.Model = new Geometry.Model3D();

            IList<Mesh> meshes = this.ReadGeometryData();

            if (this.mtlLib != null)
            {
                string mtlPath = System.IO.Path.Combine(System.IO.Path.GetDirectoryName(this.Path), this.mtlLib);
                if (File.Exists(mtlPath))
                {
                    this.Model.Materials = ReadMaterialData(mtlPath);
                }
            }

            this.Model.Meshes = meshes;

            e.Result = this.Model;
        }

        private static void AddMesh(IList<Mesh> meshes, IList<Point3D> vertices, IList<Vector3D> normals, IList<Point> uvs, IList<FaceIndices> indices, string materialName)
        {
            bool hasNormals = normals.Count > 0;
            bool hasUVs = uvs.Count > 0;
            int count = indices.Count;

            var mesh = new Mesh
            {
                MaterialName = materialName,
                Facets = new int[count],
                Vertices = new Point3D[count]
            };
            if (hasNormals)
            {
                mesh.Normals = new Vector3D[count];
            }

            if (hasUVs)
            {
                mesh.UVs = new Point[count];
            }

            for (int i = 0; i < count; i++)
            {
                int vi = indices[i].vi >= 0 ? indices[i].vi : indices[i].vi + 1 + vertices.Count;
                int vn = indices[i].vn >= 0 ? indices[i].vn : indices[i].vn + 1 + normals.Count;
                int vu = indices[i].vu >= 0 ? indices[i].vu : indices[i].vu + 1 + uvs.Count;

                if (vi > vertices.Count)
                {
                    throw new IndexOutOfRangeException("Vertex index is out of range.");
                }

                mesh.Facets[i] = i;
                mesh.Vertices[i] = vertices[vi];
                if (hasNormals)
                {
                    mesh.Normals[i] = normals[vn];
                }

                if (hasUVs)
                {
                    mesh.UVs[i] = uvs[vu];
                }
            }

            meshes.Add(mesh);
        }

        private static void AddFace(IList<FaceIndices> indices, string[] p)
        {
            if (p.Length < 4)
            {
                return;
            }

            var faces = new FaceIndices[p.Length - 1];
            for (int i = p.Length - 1; i >= 1; i--)
            {
                string[] c = p[i].Trim().Split('/');
                if (string.IsNullOrEmpty(c[0]))
                {
                    continue;
                }

                var fi = new FaceIndices
                {
                    vi = int.Parse(c[0]) - 1
                };
                if (c.Length > 1 && c[1] != string.Empty)
                {
                    fi.vu = int.Parse(c[1]) - 1;
                }

                if (c.Length > 2 && c[2] != string.Empty)
                {
                    fi.vn = int.Parse(c[2]) - 1;
                }

                faces[i - 1] = fi;
            }

            for (int i = 0; i < faces.Length - 2; i++)
            {
                indices.Add(faces[0]);
                indices.Add(faces[i + 1]);
                indices.Add(faces[i + 2]);
            }
        }

        private static IDictionary<string, ModelMaterial> ReadMaterialData(string path)
        {
            var materials = new Dictionary<string, ModelMaterial>();
            var material = new ModelMaterial();
            string materialName = null;

            using (StreamReader reader = new StreamReader(path))
            {
                while (reader.Peek() > -1)
                {
                    string l = reader.ReadLine().Trim();

                    if (l == string.Empty || l.IndexOf("#") != -1 || l == "\r")
                    {
                        continue;
                    }

                    string[] p = l.Split(" ".ToCharArray(), StringSplitOptions.RemoveEmptyEntries);

                    switch (p[0].Trim())
                    {
                        case NML:
                            if (materialName != null && !materials.ContainsKey(materialName))
                            {
                                materials.Add(materialName, material);
                            }

                            material = new ModelMaterial();
                            materialName = p[1].Trim();
                            break;

                        case KA:
                            material.Ambient = l.ToColor();
                            break;

                        case KD:
                            material.Diffuse = l.ToColor();
                            break;

                        case KS:
                            material.Specular = l.ToColor();
                            break;

                        case NS:
                            material.Shininess = double.Parse(p[1]) / 1000;
                            break;

                        case D:
                        case TR:
                            material.Alpha = double.Parse(p[1]);
                            break;

                        case MAPKD:
                            material.TexturePath = p[1].Trim();
                            break;

                        case ILLUM:
                            material.IllumType = int.Parse(p[1]);
                            break;
                    }
                }
            }

            if (materialName != null && !materials.ContainsKey(materialName))
            {
                materials.Add(materialName, material);
            }

            return materials;
        }

        private IList<Mesh> ReadGeometryData()
        {
            var meshes = new List<Mesh>();
            var vertices = new List<Point3D>();
            var normals = new List<Vector3D>();
            var uvs = new List<Point>();
            var indices = new List<FaceIndices>();
            string materialName = null;

            using (StreamReader reader = new StreamReader(this.Path))
            {
                long length = reader.BaseStream.Length;
                while (reader.Peek() > -1)
                {
                    string line = reader.ReadLine().Trim();
                    if (line.IndexOf("#") != -1 || line == "\r" || line == string.Empty)
                    {
                        continue;
                    }

                    while (line.EndsWith(@"\"))
                    {
                        string endLine = reader.ReadLine().Trim();
                        if (endLine != null)
                        {
                            line = line.Replace(@"\", endLine);
                        }
                    }

                    string[] entry = line.Split(" ".ToCharArray(), StringSplitOptions.RemoveEmptyEntries);

                    switch (entry[0].Trim())
                    {
                        case O:
                        case G:
                            if (indices.Count > 0)
                            {
                                AddMesh(meshes, vertices, normals, uvs, indices, materialName);
                            }

                            materialName = null;
                            indices.Clear();
                            break;

                        case V:
                            vertices.Add(entry.ToPoint3D());
                            break;

                        case VT:
                            uvs.Add(entry.ToPoint());
                            break;

                        case VN:
                            normals.Add((Vector3D)entry.ToPoint3D());
                            break;

                        case F:
                            AddFace(indices, entry);
                            break;

                        case MTL:
                            this.mtlLib = line.Substring(MTL.Length + 1).Trim();
                            break;

                        case UML:
                            materialName = entry.Length > 1 ? entry[1].Trim() : null;
                            break;
                    }

                    if (this.Worker.CancellationPending)
                    {
                        return null;
                    }
                }

                if (indices.Count > 0)
                {
                    AddMesh(meshes, vertices, normals, uvs, indices, materialName);
                }
            }

            return meshes;
        }

        public struct FaceIndices
        {
#pragma warning disable SA1307 // Accessible fields must begin with upper-case letter
#pragma warning disable IDE1006 // Naming Styles
            public int vi;
            public int vu;
            public int vn;
#pragma warning restore IDE1006 // Naming Styles
#pragma warning restore SA1307 // Accessible fields must begin with upper-case letter
        }
    }
}