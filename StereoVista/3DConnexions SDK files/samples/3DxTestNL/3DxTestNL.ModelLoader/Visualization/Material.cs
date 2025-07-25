// <copyright file="Material.cs" company="3Dconnexion">
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
// $Id: Material.cs 15000 2018-05-15 12:04:24Z mbonk $
//
// </history>

namespace TDx.TestNL.ModelLoader.Visualization
{
    using System.Windows.Media;

    public class Material
    {
        private static Material defaultMaterial;

        public static Material DefaultMaterial
        {
            get
            {
                if (defaultMaterial == null)
                {
                    defaultMaterial = new Material
                    {
                        Name = "Default",
                        Diffuse = Colors.LightGray,
                        Specular = Colors.White,
                        Ambient = Colors.White
                    };
                }

                return defaultMaterial;
            }
        }

        public string Name { get; set; }

        public Color Ambient { get; set; }

        public Color Diffuse { get; set; }

        public Color Specular { get; set; }

        public double Shininess { get; set; }

        public double Alpha { get; set; }

        public string TexturePath { get; set; }

        public int IllumType { get; set; }
    }
}