// <copyright file="SelectedObject.cs" company="3Dconnexion">
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
// $Id: SelectedObject.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.ViewModels.Utils
{
    using System.Windows.Media;
    using System.Windows.Media.Media3D;

    /// <summary>
    /// This class wraps <see cref="GeometryModel3D"/> to extend its
    /// functionality with selection support.
    /// </summary>
    internal class SelectedObject
    {
        private static Brush selectedColor = (Brush)new BrushConverter().ConvertFrom("#ff00aae6");
        private readonly GeometryModel3D selectedModel;
        private Brush unselectedColor;

        /// <summary>
        /// Initializes a new instance of the <see cref="SelectedObject"/> class.
        /// </summary>
        /// <param name="model">The <see cref="GeometryModel3D"/> model that is selected.</param>
        public SelectedObject(GeometryModel3D model)
        {
            this.selectedModel = model;

            var diffuseMaterial = model.Material as DiffuseMaterial;
            this.unselectedColor = diffuseMaterial.Brush;
            diffuseMaterial.Brush = selectedColor;
        }

        /// <summary>
        /// Gets the selected geometry model
        /// </summary>
        public Model3D SelectedModelPart => this.selectedModel;

        /// <summary>
        /// Returns the hash code for the selected GeometryMode3D instance.
        /// </summary>
        /// <returns>A 32-bit signed integer hash code.</returns>
        public override int GetHashCode()
        {
            return this.selectedModel.GetHashCode();
        }

        /// <summary>
        /// Rest the color to unselected
        /// </summary>
        public void ResetColor()
        {
            (this.selectedModel.Material as DiffuseMaterial).Brush = this.unselectedColor;
        }
    }
}