// <copyright file="ModelSelector.cs" company="3Dconnexion">
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
// $Id: ModelSelector.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.ViewModels.Utils
{
    using System.Collections.Generic;
    using System.Linq;
    using System.Windows;
    using System.Windows.Controls;
    using System.Windows.Input;
    using System.Windows.Media;
    using System.Windows.Media.Media3D;

    /// <summary>
    /// Encapsulates model selection logic.
    /// </summary>
    internal static class ModelSelector
    {
        /// <summary>
        /// Attached property for the <see cref="Viewport3D"/>.
        /// </summary>
        public static readonly DependencyProperty SelectionProperty = DependencyProperty.RegisterAttached(
          "Selection",
          typeof(HashSet<SelectedObject>),
          typeof(Viewport3D),
          new FrameworkPropertyMetadata(new HashSet<SelectedObject>(), FrameworkPropertyMetadataOptions.AffectsRender));

        /// <summary>
        /// Sets the selection property of the <see cref="UIElement"/>
        /// </summary>
        /// <param name="element">The <see cref="Viewport3D"/> instance.</param>
        /// <param name="value">The <see cref="HashSet{SelectedObject}"/> of the selected objects.</param>
        public static void SetSelection(UIElement element, HashSet<SelectedObject> value)
        {
            element.SetValue(SelectionProperty, value);
        }

        /// <summary>
        /// Gets the selection property of the <see cref="UIElement"/>
        /// </summary>
        /// <param name="element">The <see cref="Viewport3D"/> instance.</param>
        /// <returns>The <see cref="HashSet{SelectedObject}"/> of the selected objects.</returns>
        public static HashSet<SelectedObject> GetSelection(UIElement element)
        {
            return (HashSet<SelectedObject>)element.GetValue(SelectionProperty);
        }

        /// <summary>
        /// Gets the selection property of the <see cref="Viewport3D"/>
        /// </summary>
        /// <param name="viewport">The <see cref="Viewport3D"/> instance.</param>
        /// <returns>The <see cref="HashSet{SelectedObject}"/> of the selected objects.</returns>
        public static HashSet<SelectedObject> GetSelection(this Viewport3D viewport)
        {
            return (HashSet<SelectedObject>)viewport.GetValue(SelectionProperty);
        }

        /// <summary>
        /// Performs 2D hit testing to determine whether a model part was selected and
        /// also handles multi-selection logic.
        /// </summary>
        /// <param name="viewport">The <see cref="Viewport3D"/> instance containing the model</param>
        /// <param name="e">The <see cref="MouseButtonEventArgs"/> for the mouse button.</param>
        public static void MouseButtonDownSelection(this Viewport3D viewport, MouseButtonEventArgs e)
        {
            HashSet<SelectedObject> currentSelection = GetSelection(viewport);

            Point mousePosition = e.GetPosition(viewport);

            bool isControlPressed = Keyboard.IsKeyDown(Key.LeftCtrl) || Keyboard.IsKeyDown(Key.RightCtrl);

            // Perform the hit test.
            HitTestResult result =
                VisualTreeHelper.HitTest(viewport, mousePosition);

            // See if we hit a model.
            if (result is RayMeshGeometry3DHitTestResult)
            {
                GeometryModel3D selectedModelPart = (GeometryModel3D)(result as RayMeshGeometry3DHitTestResult).ModelHit;

                SelectedObject alreadySelected = currentSelection.FirstOrDefault(sp => sp.GetHashCode() == selectedModelPart.GetHashCode());

                if (alreadySelected != null)
                {
                    int count = currentSelection.Count;
                    if (!isControlPressed)
                    {
                        viewport.ClearSelection();
                        if (count > 1)
                        {
                            SelectedObject selectedObj = new SelectedObject(selectedModelPart);
                            currentSelection.Add(selectedObj);
                        }
                    }
                    else
                    {
                        currentSelection.Remove(alreadySelected);
                        alreadySelected.ResetColor();
                    }
                }
                else
                {
                    if (!isControlPressed)
                    {
                        viewport.ClearSelection();
                    }

                    SelectedObject selectedObj = new SelectedObject(selectedModelPart);
                    currentSelection.Add(selectedObj);
                }
            }
            else if (!isControlPressed)
            {
                viewport.ClearSelection();
            }
        }

        /// <summary>
        /// Clear the <see cref="Viewport3D"/> instances selection set.
        /// </summary>
        /// <param name="viewport">The <see cref="Viewport3D"/> this method extends</param>
        public static void ClearSelection(this Viewport3D viewport)
        {
            HashSet<SelectedObject> currentSelection = GetSelection(viewport);
            if (currentSelection != null)
            {
                foreach (SelectedObject so in currentSelection)
                {
                    so.ResetColor();
                }

                currentSelection.Clear();
            }
        }

        /// <summary>
        /// Select the complete model in the <see cref="Viewport3D"/> instance.
        /// </summary>
        /// <param name="viewport">The <see cref="Viewport3D"/> this method extends</param>
        public static void SelectAll(this Viewport3D viewport)
        {
            viewport.ClearSelection();

            HashSet<SelectedObject> currentSelection = GetSelection(viewport);
            foreach (ModelVisual3D visual3D in viewport.Children)
            {
                SelectModel(visual3D.Content, currentSelection);
            }
        }

        private static void SelectModel(Model3D model, HashSet<SelectedObject> currentSelection)
        {
            if (model is GeometryModel3D)
            {
                SelectedObject selectedObj = new SelectedObject(model as GeometryModel3D);
                currentSelection.Add(selectedObj);
            }
            else if (model is System.Windows.Media.Media3D.Model3DGroup)
            {
                Model3DGroup model3DGroup = model as Model3DGroup;
                foreach (Model3D child in model3DGroup.Children)
                {
                    SelectModel(child, currentSelection);
                }
            }
        }
    }
}