// <copyright file="Form1.cs" company="3Dconnexion">
// -------------------------------------------------------------------------------------------------
// Copyright (c) 2020 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer Kit",
// including all accompanying documentation, and is protected by intellectual property laws. All use
// of the 3Dconnexion Software Developer Kit is subject to the License Agreement found in the
// "LicenseAgreementSDK.txt" file.
// All rights not expressly granted by 3Dconnexion are reserved.
// -------------------------------------------------------------------------------------------------
// </copyright>
// <history>
// *************************************************************************************************
//
// $Id$
//
// 06/10/20 MSB Based on Mouse3DTest by 3Dconnexion forum user formware from (www.formware.co)
// </history>
#pragma warning disable SA1124 // DoNotUseRegions
namespace TDx.GettingStarted
{
    using System;
    using System.Diagnostics;
    using System.Drawing;
    using System.Runtime.InteropServices;
    using System.Windows.Forms;
    using Navigation;
    using OpenTK;
    using OpenTK.Graphics.OpenGL;
    using TDx.SpaceMouse.Navigation3D;
    using Point3 = OpenTK.Vector3;

    /// <summary>
    /// The form that contains the 3D viewport.
    /// </summary>
    public partial class Form1 : Form, IViewModel
    {
        // state is kept in these variables.
        private readonly Camera3D camera = new Camera3D
        {
            Eye = new Vector3(3, -3, 3),
            Target = new Vector3(0, 0, 0),
            Up = new Vector3(-1, 1, 1),
            Projection = Projection.Perspective,
            FieldOfView = (float)Math.PI / 4,
            NearPlaneDistance = 0.01f,
            FarPlaneDistance = 1000,
        };

        private readonly Model3D model = new Model3D();

        private readonly Pivot pivot = new Pivot();

        private readonly NavigationModel navigationModel;

        private readonly DebugProc logger = LogOpenGl;

        private bool loaded = false;

        private bool animating = false;

        /// <summary>
        /// Initializes a new instance of the <see cref="Form1"/> class.
        /// </summary>
        public Form1()
        {
            this.navigationModel = new NavigationModel(this)
            {
                Profile = "GettingStarted",
                Enable = this.loaded,
            };

            this.InitializeComponent();
        }

        #region IViewModel

        /// <inheritdoc/>
        bool IViewModel.Animating
        {
            get => this.animating;
            set
            {
                if (this.animating != value)
                {
                    this.animating = value;
                    if (!this.animating)
                    {
                        // Ensure everything is up to date.
                        this.glControl1.Invalidate();
                    }
                }
            }
        }

        /// <inheritdoc/>
        Camera3D IViewModel.Camera => this.camera;

        /// <inheritdoc/>
        Box3 IViewModel.ModelBounds => this.model.Bounds;

        /// <inheritdoc/>
        bool IViewModel.PivotReadOnly => this.pivot.ReadOnly;

        /// <inheritdoc/>
        bool IViewModel.PivotVisible
        {
            get => this.pivot.Visible;

            set
            {
                this.pivot.Visible = value;
                if (!this.animating)
                {
                    // refresh.
                    this.glControl1.Invalidate();
                }
            }
        }

        /// <inheritdoc/>
        Point3 IViewModel.PivotPosition
        {
            get => this.pivot.Position;
            set => this.pivot.Position = value;
        }

        /// <inheritdoc/>
        GLControl IViewModel.Viewport => this.glControl1;

        /// <inheritdoc/>
        void IViewModel.BeginTransaction()
        {
        }

        /// <inheritdoc/>
        void IViewModel.EndTransaction()
        {
            this.SetPerspectiveandView(false);
        }

        #endregion IViewModel

        #region existing 3d code sets view matrices and draws

        /// <summary>
        /// main method that updates the opengl viewmatrix based upon UP/EYE/TARGET
        /// </summary>
        /// <param name="setviewport">true, set the viewport dimensions.</param>
        public void SetPerspectiveandView(bool setviewport)
        {
            // if required.
            if (setviewport)
            {
                GL.Viewport(0, 0, this.glControl1.Width, this.glControl1.Height); // Use all of the glControl painting area
            }

            // projection matrix
            var aspect_ratio = this.glControl1.Width / (float)this.glControl1.Height;
            var projection = Matrix4.CreatePerspectiveFieldOfView(this.camera.FieldOfView, aspect_ratio, this.camera.NearPlaneDistance, this.camera.FarPlaneDistance); // set aspect ratio to overcome scaling
            GL.MatrixMode(MatrixMode.Projection); // only call LoadIdentity/GL.Frustum/GL.Ortho/GL.LoadMatrix here
            GL.LoadMatrix(ref projection);

            // view matrix.
            var viewMatrix = Matrix4.LookAt(this.camera.Eye, this.camera.Target, this.camera.Up); // look at center.
            GL.MatrixMode(MatrixMode.Modelview);
            GL.LoadMatrix(ref viewMatrix);

            // refresh.
            this.glControl1.Invalidate();
        }

        #region opengl logger

        private static void LogOpenGl(DebugSource source, DebugType type, int id, DebugSeverity severity, int i, IntPtr message, IntPtr userparam)
        {
            if (message != IntPtr.Zero)
            {
                string error = Marshal.PtrToStringAnsi(message);

                if (!error.Contains("API_ID_LINE_WIDTH"))
                {
                    if (Debugger.IsAttached)
                    {
                        Debug.Write("opengl: " + error);
                    }
                }
            }
        }

        #endregion opengl logger

        /// <summary>
        /// The main paint function.
        /// </summary>
        /// <param name="sender">The sender of the event.</param>
        /// <param name="e">The <see cref="PaintEventArgs"/>.</param>
        private void GlControl1_Paint(object sender, PaintEventArgs e)
        {
            if (!this.loaded)
            {
                return;
            }

            // check and clear
            this.glControl1.MakeCurrent();
            GL.ClearColor(Color.LightBlue);
            GL.Clear(ClearBufferMask.ColorBufferBit | ClearBufferMask.DepthBufferBit | ClearBufferMask.StencilBufferBit);

            // draw axis
            int axissize = 7;
            GL.LineWidth(5);
            GL.Begin(PrimitiveType.Lines);
            GL.Color3(Color.Red);
            GL.Vertex3(0, 0, 0);
            GL.Vertex3(axissize, 0, 0);
            GL.Color3(Color.Green);
            GL.Vertex3(0, 0, 0);
            GL.Vertex3(0, axissize, 0);
            GL.Color3(Color.Blue);
            GL.Vertex3(0, 0, 0);
            GL.Vertex3(0, 0, axissize);
            GL.End();

            // draw the model.
            this.model.Draw();

            // draw the pivot.
            this.pivot.Draw();

            // swap the buffers.
            this.glControl1.SwapBuffers();
        }

        #endregion existing 3d code sets view matrices and draws

        /// <summary>
        /// load the GL control.
        /// </summary>
        /// <param name="sender">The sender of the event.</param>
        /// <param name="e">The <see cref="EventArgs"/>.</param>
        private void GlControl1_Load(object sender, EventArgs e)
        {
            this.glControl1.MakeCurrent();
            GL.Disable(EnableCap.CullFace);
            GL.Enable(EnableCap.DepthTest);
            this.loaded = true;
            this.navigationModel.Enable = true;
            GL.Enable(EnableCap.DebugOutput);
            GL.DebugMessageCallback(this.logger, IntPtr.Zero);
        }

        private void Form1_FormClosed(object sender, FormClosedEventArgs e)
        {
        }

        #region event handlers for form

        private void GlControl1_Resize(object sender, EventArgs e)
        {
            this.SetPerspectiveandView(true);
        }

        private void Form1_Shown(object sender, EventArgs e)
        {
            this.SetPerspectiveandView(true);
        }

        private void GlControl1_MouseClick(object sender, MouseEventArgs e)
        {
            // Let the user set a fixed pivot.
            if (e.Button == MouseButtons.Middle)
            {
                // Calculate the position on the near plane in view coordinates
                float worldHeight = (float)(2 * this.camera.NearPlaneDistance * Math.Tan(this.camera.FieldOfView * 0.5));
                float aspectRatio = this.glControl1.AspectRatio;
                float worldWidth = worldHeight * aspectRatio;
                Vector3 dir = new Vector3(e.X - (this.glControl1.Width / 2), (this.glControl1.Height / 2) - e.Y, -1);
                Vector3 scale = new Vector3(worldWidth / this.glControl1.Width, worldHeight / this.glControl1.Height, this.camera.NearPlaneDistance);

                // The point on the near plane
                dir *= scale;

                // Because the view has (0,0,0) at the center of the view we just need to normalize to get the
                // vector pointing into the view.
                dir.Normalize();

                // Convert the direction to world coordinates
                Matrix4 cameraTM = Matrix4.LookAt(this.camera.Eye, this.camera.Target, this.camera.Up).Inverted();
                Ray3 hitRay = new Ray3()
                {
                    Origin = this.camera.Eye,
                    Direction = Vector3.TransformVector(dir, cameraTM),
                };

                // If the user hits something set a new center of rotation
                if (this.pivot.ReadOnly = hitRay.Intersects(this.model.Bounds, out Point3 hit))
                {
                    this.pivot.Position = hit;
                }

                this.navigationModel.Properties.Write(PropertyNames.PivotUser, this.pivot.ReadOnly);
            }
        }

        private void GlControl1_MouseMove(object sender, MouseEventArgs e)
        {
            // SetPerspectiveandView(false);
        }

        #endregion event handlers for form
    }
}
