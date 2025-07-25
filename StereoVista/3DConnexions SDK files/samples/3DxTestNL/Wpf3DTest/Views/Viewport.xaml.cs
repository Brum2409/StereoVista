// <copyright file="Viewport.xaml.cs" company="3Dconnexion">
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
// $Id: Viewport.xaml.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.Views
{
    using System.Windows;
    using System.Windows.Controls;
    using TDx.TestNL.ViewModels;

    /// <summary>
    /// Interaction logic for Viewport.xaml.
    /// </summary>
    public partial class Viewport : UserControl
    {
        /// <summary>
        /// Initializes a new instance of the <see cref="Viewport"/> class.
        /// </summary>
        public Viewport()
        {
            this.InitializeComponent();
        }

        private void UserControl_Loaded(object sender, RoutedEventArgs e)
        {
            if (this.DataContext == null)
            {
                return;
            }

            var vm = this.DataContext as ViewportViewModel;

            vm.SetView(this.viewport);
        }
    }
}