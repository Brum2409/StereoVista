// <copyright file="ExtensionMethods.cs" company="3Dconnexion">
// -------------------------------------------------------------------------------------
// Copyright (c) 2022 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer
// Kit", including all accompanying documentation, and is protected by intellectual
// property laws. All use of the 3Dconnexion Software Developer Kit is subject to the
// License Agreement found in the "LicenseAgreementSDK.txt" file.
// All rights not expressly granted by 3Dconnexion are reserved.
// -------------------------------------------------------------------------------------
// </copyright>

namespace ActionInput

{
    /// <summary>
    /// Extension methods.
    /// </summary>
    public static class ExtensionMethods
    {
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