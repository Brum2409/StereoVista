// <copyright file="ExtensionMethods.cs" company="3Dconnexion">
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
// File History
//
// $Id: ExtensionMethods.cs 15519 2018-11-12 13:29:59Z mbonk $
//
// </history>
namespace TDx.GettingStarted.Navigation
{
    using System;
    using System.Windows.Threading;

    /// <summary>
    /// Extension methods.
    /// </summary>
    public static class ExtensionMethods
    {
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
        /// Explicitly convert a <see cref="TDx.SpaceMouse.Navigation3D.Point"/> to a <see cref="OpenTK.Vector3"/>.
        /// </summary>
        /// <param name="point">The <see cref="TDx.SpaceMouse.Navigation3D.Point"/> to convert.</param>
        /// <returns>A <see cref="OpenTK.Vector3"/>.</returns>
        public static OpenTK.Vector3 AsPoint3(this TDx.SpaceMouse.Navigation3D.Point point)
        {
            return new OpenTK.Vector3((float)point.X, (float)point.Y, (float)point.Z);
        }

        /// <summary>
        /// Explicitly convert a <see cref="TDx.SpaceMouse.Navigation3D.Vector"/> to a <see cref="OpenTK.Vector3"/>.
        /// </summary>
        /// <param name="vector">The <see cref="TDx.SpaceMouse.Navigation3D.Vector"/> to convert.</param>
        /// <returns>A <see cref="OpenTK.Vector3"/>.</returns>
        public static OpenTK.Vector3 AsVector3(this TDx.SpaceMouse.Navigation3D.Vector vector)
        {
            return new OpenTK.Vector3((float)vector.X, (float)vector.Y, (float)vector.Z);
        }

        /// <summary>
        /// Explicitly convert a <see cref="OpenTK.Vector3"/> to a <see cref="TDx.SpaceMouse.Navigation3D.Point"/>.
        /// </summary>
        /// <param name="point">The <see cref="OpenTK.Vector3"/> to convert.</param>
        /// <returns>A <see cref="TDx.SpaceMouse.Navigation3D.Point"/>.</returns>
        public static TDx.SpaceMouse.Navigation3D.Point AsPoint(this OpenTK.Vector3 point)
        {
            return new TDx.SpaceMouse.Navigation3D.Point(point.X, point.Y, point.Z);
        }
    }
}