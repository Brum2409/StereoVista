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
    }
}