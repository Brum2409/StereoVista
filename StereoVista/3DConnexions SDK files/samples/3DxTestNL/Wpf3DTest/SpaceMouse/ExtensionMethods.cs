// <copyright file="ExtensionMethods.cs" company="3Dconnexion">
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
// $Id: ExtensionMethods.cs 20398 2023-09-05 07:02:10Z mbonk $
//
// </history>
namespace TDx.TestNL.Navigation
{
    using System;
    using System.Windows.Controls;
    using System.Windows.Media.Media3D;
    using System.Windows.Threading;
    using TDx.SpaceMouse.Navigation3D;

    /// <summary>
    /// Extension methods.
    /// </summary>
    public static class ExtensionMethods
    {
        /// <summary>
        /// Calls an invoke method if the caller is on a different thread to the one the control was created on.
        /// </summary>
        /// <typeparam name="T"><see cref="Control"/> type.</typeparam>
        /// <param name="c">A control derived instance.</param>
        /// <param name="action">The <see cref="Action{T}"/> to perform.</param>
        public static void InvokeIfRequired<T>(this T c, Action<T> action)
            where T : Control
        {
            if (c.Dispatcher.CheckAccess())
            {
                action(c);
            }
            else
            {
                c.Dispatcher.Invoke(DispatcherPriority.Normal, new Action(() => action(c)));
            }
        }

        /// <summary>
        /// Executes the specified <see cref="Action"/> synchronously at the normal priority on the thread
        /// the Dispatcher is associated with.
        /// </summary>
        /// <param name="d">The <see cref="Dispatcher"/> to invoke the delegate on.</param>
        /// <param name="callback">A delegate to invoke through the dispatcher.</param>
        public static void InvokeIfRequired(this Dispatcher d, Action callback)
        {
            if (d.CheckAccess())
            {
                callback();
            }
            else
            {
                d.Invoke(callback, DispatcherPriority.Normal);
            }
        }

        /// <summary>
        /// Executes the specified <see cref="Func{TResult}"/> synchronously at the normal priority on the thread
        /// the Dispatcher is associated with.
        /// </summary>
        /// <typeparam name="TResult">The return value type of the specified delegate.</typeparam>
        /// <param name="d">The <see cref="Dispatcher"/> to invoke the delegate on.</param>
        /// <param name="callback">A delegate to invoke through the dispatcher.</param>
        /// <returns>The value returned by <paramref name="callback"/>.</returns>
        public static TResult InvokeIfRequired<TResult>(this Dispatcher d, Func<TResult> callback)
        {
            if (d.CheckAccess())
            {
                return callback();
            }
            else
            {
                return d.Invoke(callback, DispatcherPriority.Normal);
            }
        }

        /// <summary>
        /// Conversion from <see cref="Matrix3D"/> to <see cref="Matrix"/>.
        /// </summary>
        /// <param name="m">The <see cref="Matrix3D"/> to convert.</param>
        /// <returns>A <see cref="Matrix"/>.</returns>
        public static Matrix AsMatrix(this Matrix3D m) => new Matrix(m.M11, m.M12, m.M13, m.M14, m.M21, m.M22, m.M23, m.M24, m.M31, m.M32, m.M33, m.M34, m.OffsetX, m.OffsetY, m.OffsetZ, m.M44);

        /// <summary>
        /// Conversion from <see cref="Matrix"/> to <see cref="Matrix3D"/>.
        /// </summary>
        /// <param name="m">The <see cref="Matrix"/> to convert.</param>
        /// <returns>A <see cref="Matrix3D"/>.</returns>
        public static Matrix3D AsMatrix3D(this TDx.SpaceMouse.Navigation3D.Matrix m) => new Matrix3D(m.M11, m.M12, m.M13, m.M14, m.M21, m.M22, m.M23, m.M24, m.M31, m.M32, m.M33, m.M34, m.M41, m.M42, m.M43, m.M44);

        /// <summary>
        /// Conversion from <see cref="Box"/> to <see cref="Rect3D"/>.
        /// </summary>
        /// <param name="box">The <see cref="Box"/> to convert.</param>
        /// <returns>A <see cref="Rect3D"/>.</returns>
        public static Rect3D AsRect3D(this Box box)
        {
            return new Rect3D(box.Min.X, box.Min.Y, box.Min.Z, box.Max.X - box.Min.X, box.Max.Y - box.Min.Y, box.Max.Z - box.Min.Z);
        }

        /// <summary>
        /// User-defined conversion from <see cref="Rect3D"/> to <see cref="Box"/>.
        /// </summary>
        /// <param name="rect">The <see cref="Rect3D"/> to convert.</param>
        /// <returns>A <see cref="Box"/>.</returns>
        public static Box AsBox(this Rect3D rect)
        {
            {
                if (rect.IsEmpty)
                {
                    return new Box()
                    {
                        Min = default(Point),
                        Max = default(Point),
                    };
                }

                return new Box(rect.X, rect.Y, rect.Y, rect.X + rect.SizeX, rect.Y + rect.SizeY, rect.Z + rect.SizeZ);
            }
        }

        /// <summary>
        /// User-defined conversion from <see cref="Point"/> to <see cref="Point3D"/>.
        /// </summary>
        /// <param name="p">The <see cref="Point"/> to convert.</param>
        /// <returns>A <see cref="Point3D"/>.</returns>
        public static Point3D AsPoint3D(this Point p) => new Point3D(p.X, p.Y, p.Z);

        /// <summary>
        /// User-defined conversion from <see cref="Point3D"/> to <see cref="Point"/>.
        /// </summary>
        /// <param name="point">The <see cref="Point3D"/> to convert.</param>
        /// <returns>A <see cref="Point"/>.</returns>
        public static Point AsPoint(this Point3D point) => new Point(point.X, point.Y, point.Z);

        /// <summary>
        /// Conversion from <see cref="Vector"/> to <see cref="Vector3D"/>.
        /// </summary>
        /// <param name="v">The <see cref="Vector"/> to convert.</param>
        /// <returns>A <see cref="Vector3D"/>.</returns>
        public static Vector3D AsVector3D(this Vector v) => new Vector3D(v.X, v.Y, v.Z);

        /// <summary>
        /// Conversion from <see cref="Vector3D"/> to <see cref="Vector"/>.
        /// </summary>
        /// <param name="v">The <see cref="Vector3D"/> to convert.</param>
        /// <returns>A <see cref="Vector3D"/>.</returns>
        public static Vector AsVector(this Vector3D v) => new Vector(v.X, v.Y, v.Z);

        /// <summary>
        /// Converts a <see cref="System.Drawing.Image"/> to a byte array.
        /// </summary>
        /// <param name="image">The image as a <see cref="System.Drawing.Image"/>.</param>
        /// <returns>The <see cref="System.Drawing.Image"/> object converted to an array of bytes.</returns>
        public static byte[] ToByteArray(this System.Drawing.Image image)
        {
            System.Drawing.ImageConverter converter = new System.Drawing.ImageConverter();
            return (byte[])converter.ConvertTo(image, typeof(byte[]));
        }
    }
}