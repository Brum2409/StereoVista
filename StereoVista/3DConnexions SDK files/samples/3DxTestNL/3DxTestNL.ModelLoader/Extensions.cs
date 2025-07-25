// <copyright file="Extensions.cs" company="3Dconnexion">
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
// $Id: Extensions.cs 15000 2018-05-15 12:04:24Z mbonk $
//
// </history>

namespace TDx.TestNL.ModelLoader
{
    using System;
    using System.Windows;
    using System.Windows.Media;
    using System.Windows.Media.Media3D;

    public static class Extensions
    {
        public static Point3D ToPoint3D(this string[] entry)
        {
            if (entry != null && entry.Length > 3)
            {
                return new Point3D(double.Parse(entry[1]), double.Parse(entry[2]), double.Parse(entry[3]));
            }

            if (entry != null)
            {
                throw new ArgumentNullException(nameof(entry));
            }
            else
            {
                throw new ArgumentException("Unable to parse argument string to Vector3D (length doesn't match).", nameof(entry));
            }
        }

        public static Point ToPoint(this string[] entry)
        {
            if (entry != null && entry.Length > 2)
            {
                return new Point(double.Parse(entry[1]), double.Parse(entry[2]));
            }

            if (entry != null)
            {
                throw new ArgumentNullException(nameof(entry));
            }
            else
            {
                throw new ArgumentException("Unable to parse argument string to Vector3D (length doesn't match).", nameof(entry));
            }
        }

        public static Color ToColor(this string line)
        {
            if (line != null)
            {
                string[] entry = line.Split(" ".ToCharArray(), StringSplitOptions.RemoveEmptyEntries);
                if (entry.Length > 3)
                {
                    return Color.FromArgb(255, ToByteRange(double.Parse(entry[1])), ToByteRange(double.Parse(entry[2])), ToByteRange(double.Parse(entry[3])));
                }
            }

            throw new ArgumentNullException(nameof(line));
        }

        private static byte ToByteRange(double value)
        {
            value = FitToRange(value, 0, 1);
            return (byte)(255 * value);
        }

        private static double FitToRange(double value, double minVal, double maxVal)
        {
            value = (value < minVal) ? minVal : (value > maxVal) ? maxVal : value;
            return value;
        }
    }
}