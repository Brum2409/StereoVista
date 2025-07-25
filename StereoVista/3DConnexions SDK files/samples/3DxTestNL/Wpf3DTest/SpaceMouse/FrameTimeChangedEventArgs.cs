// <copyright file="FrameTimeChangedEventArgs.cs" company="3Dconnexion">
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
// $Id: FrameTimeChangedEventArgs.cs 15519 2018-11-12 13:29:59Z mbonk $
//
// </history>

namespace TDx.TestNL.Navigation
{
    /// <summary>
    /// Provides data related to the <see cref="IViewModelNavigation.FrameTimeChanged"/> event.
    /// </summary>
    public class FrameTimeChangedEventArgs : System.EventArgs
    {
        /// <summary>
        /// Initializes a new instance of the <see cref="FrameTimeChangedEventArgs"/> class.
        /// </summary>
        /// <param name="timestamp">The time stamp in milliseconds.</param>
        public FrameTimeChangedEventArgs(double timestamp)
        {
            this.TimeStamp = timestamp;
        }

        /// <summary>
        /// Gets the time stamp in milliseconds.
        /// </summary>
        public double TimeStamp { get; private set; }
    }
}