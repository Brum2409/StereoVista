// <copyright file="ModelCallbacks.cs" company="3Dconnexion">
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
// $Id: ModelCallbacks.cs 17485 2020-05-29 12:44:39Z mbonk $
//
// </history>

namespace TDx.TestNL.Navigation
{
    using System.Diagnostics;
    using System.Windows.Media.Media3D;
    using TDx.SpaceMouse.Navigation3D;
    using Application = System.Windows.Application;

    /// <summary>
    /// Implements the callbacks for the IModel interface
    /// </summary>
    internal partial class NavigationModel : IModel
    {
        private MatrixTransform3D selectionTransform = new MatrixTransform3D();

        /// <summary>
        /// Is called when the Navigation3D instance needs to get the extents of the model.
        /// </summary>
        /// <returns>The extents of the model in world coordinates.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">There is no model in the scene.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Box IModel.GetModelExtents()
        {
            return this.dispatcher.InvokeIfRequired(() => this.GetExtents(this.viewportVM.Model).AsBox());
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to get the extents of the selection.
        /// </summary>
        /// <returns>The extents of the selection in world coordinates.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">There is no selection.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Box IModel.GetSelectionExtents()
        {
            return this.dispatcher.InvokeIfRequired(() => this.GetExtents(this.viewportVM.SelectedModel).AsBox());
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to get the extents of the selection.
        /// </summary>
        /// <returns>true if the selection set is empty, otherwise false.</returns>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        bool IModel.IsSelectionEmpty()
        {
            return this.dispatcher.InvokeIfRequired(() => this.viewportVM.SelectedModel.Children.Count == 0);
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to set the selections's transform matrix.
        /// </summary>
        /// <param name="transform">The selection's transform <see cref="Matrix"/> in world coordinates.</param>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        void IModel.SetSelectionTransform(Matrix transform)
        {
            if (this.viewportVM.SelectedModel == null)
            {
                throw new System.InvalidOperationException("Nothing is selected.");
            }

            this.dispatcher.InvokeIfRequired(() =>
            {
                // Trace.WriteLine("Matrix transform=" + transform.ToString());
                Matrix3D inverseTransform = this.selectionTransform.Matrix;
                inverseTransform.Invert();

                this.selectionTransform.Matrix = transform.AsMatrix3D();

                Matrix3D delta = inverseTransform * this.selectionTransform.Matrix;

                Trace.WriteLine("Matrix delta=" + delta.ToString());
                foreach (var model in this.viewportVM.SelectedModel.Children)
                {
                    Matrix3D matrix = model.Transform.Value;

                    model.Transform = new MatrixTransform3D(model.Transform.Value * delta);
                }
            });

            // throw new System.InvalidOperationException("Editing the model is not supported.");
        }

        /// <summary>
        /// Is called when the Navigation3D instance needs to get the selections's transform matrix.
        /// </summary>
        /// <returns>The selection's transform <see cref="Matrix"/> in world coordinates or null.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">There is no selection.</exception>
        /// <exception cref="System.InvalidOperationException">The call is invalid for the object's current state.</exception>
        /// <exception cref="System.NotImplementedException">The requested method or operation is not implemented.</exception>
        Matrix IModel.GetSelectionTransform()
        {
            if (this.viewportVM.SelectedModel == null)
            {
                throw new TDx.SpaceMouse.Navigation3D.NoDataException();
            }

            return this.dispatcher.InvokeIfRequired(() =>
            {
                Trace.WriteLine("Selection Transform=" + this.selectionTransform.Matrix.ToString());
                return this.selectionTransform.Matrix.AsMatrix();
            });
        }

        /// <summary>
        /// Gets a Rect3D that specifies the axis-aligned bounding box of the <see cref="Model3DGroup"/>.
        /// </summary>
        /// <param name="group">The <see cref="Model3DGroup"/>.</param>
        /// <returns>A <see cref="Rect3D"/> bounding box for the group.</returns>
        /// <exception cref="TDx.SpaceMouse.Navigation3D.NoDataException">There is no model.</exception>
        private Rect3D GetExtents(Model3DGroup group)
        {
            if (group == null)
            {
                throw new TDx.SpaceMouse.Navigation3D.NoDataException();
            }
            else
            {
                return group.Bounds;
            }
        }
    }
}