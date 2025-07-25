// <copyright file="Pivot.cs" company="3Dconnexion">
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

namespace TDx.GettingStarted
{
    using System;
    using System.Drawing;
    using System.Drawing.Imaging;
    using OpenTK.Graphics.OpenGL;
    using TDx.GettingStarted.Properties;
    using Point3 = OpenTK.Vector3;

    /// <summary>
    /// Class that represents the rotation pivot
    /// </summary>
    internal class Pivot
    {
        private readonly byte[] data;
        private readonly bool topDown;
        private readonly int width;
        private readonly int height;

        /// <summary>
        /// Initializes a new instance of the <see cref="Pivot"/> class.
        /// </summary>
        public Pivot()
        {
            Bitmap bmp = Resources.Pivot;
            BitmapData bmpData = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height), ImageLockMode.ReadOnly, System.Drawing.Imaging.PixelFormat.Format32bppArgb);

            this.topDown = bmpData.Stride > 0;
            this.width = bmp.Width;
            this.height = bmp.Height;

            // Declare an array to hold the bytes of the bitmap.
            int bytes = Math.Abs(bmpData.Stride) * bmp.Height;
            this.data = new byte[bytes];

            // Copy the RGB values into the array.
            System.Runtime.InteropServices.Marshal.Copy(bmpData.Scan0, this.data, 0, bytes);

            // Unlock the bits.
            bmp.UnlockBits(bmpData);
        }

        /// <summary>
        /// Gets or sets the position of the rotation pivot in world (GL_MODELVIEW) space.
        /// </summary>
        public Point3 Position { get; set; }

        /// <summary>
        /// Gets the image to display.
        /// </summary>
        public Bitmap Image => Resources.Pivot;

        /// <summary>
        /// Gets or sets a value indicating whether the pivot is visible.
        /// </summary>
        public bool Visible { get; set; } = false;

        /// <summary>
        /// Gets or sets a value indicating whether gets a value indicating whether the pivot position is read only or can be changed.
        /// </summary>
        public bool ReadOnly { get; set; } = false;

        /// <summary>
        /// Draw the Pivot in the current OpenGL context.
        /// </summary>
        public void Draw()
        {
            if (!this.Visible)
            {
                return;
            }

            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);
            GL.Enable(EnableCap.Blend);
            GL.DepthMask(false);
            GL.Disable(EnableCap.DepthTest);
            GL.RasterPos3(this.Position.X, this.Position.Y, this.Position.Z);
            if (this.topDown)
            {
                GL.PixelZoom(1, -1);
                GL.Bitmap(0, 0, 0, 0, -(this.width >> 1), this.height >> 1, this.data);
                GL.DrawPixels(this.width, this.height, OpenTK.Graphics.OpenGL.PixelFormat.Bgra, PixelType.UnsignedByte, this.data);
            }
            else
            {
                GL.PixelZoom(1, 1);
                GL.Bitmap(0, 0, 0, 0, -(this.width >> 1), -(this.height >> 1), this.data);
                GL.DrawPixels(this.width, this.height, OpenTK.Graphics.OpenGL.PixelFormat.Bgra, PixelType.UnsignedByte, this.data);
            }

            GL.Enable(EnableCap.DepthTest);
            GL.DepthMask(true);
        }
    }
}
