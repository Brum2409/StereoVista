// <copyright file="ModelReaderEventArgs.cs" company="3Dconnexion">
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
// $Id: ModelReaderEventArgs.cs 15149 2018-07-02 08:29:31Z mbonk $
//
// </history>
namespace TDx.TestNL.ModelLoader.Readers
{
    /// <summary>
    /// Provides the event data used by the model reader events
    /// </summary>
    public class ModelReaderEventArgs
    {
        /// <summary>
        /// Initializes a new instance of the <see cref="ModelReaderEventArgs"/> class.
        /// </summary>
        /// <param name="success">True if successful, otherwise false.</param>
        /// <param name="finished">True if finished, otherwise false.</param>
        public ModelReaderEventArgs(bool success, bool finished)
        {
            this.Success = success;
            this.Finished = finished;
        }

        /// <summary>
        /// Gets a value indicating whether the state is success
        /// </summary>
        public bool Success { get; private set; }

        /// <summary>
        /// Gets a value indicating whether the reader is finished
        /// </summary>
        public bool Finished { get; private set; }
    }
}