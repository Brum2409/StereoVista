// <copyright file="MainWindowViewModel.cs" company="3Dconnexion">
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
// $Id: MainWindowViewModel.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.ViewModels
{
    using System.Windows.Input;

    /// <summary>
    /// View-Model class for application's <see cref="MainWindow"/>.
    /// </summary>
    public class MainWindowViewModel : ViewModel
    {
        private readonly RibbonViewModel ribbonViewModel;
        private readonly ViewportViewModel viewportViewModel;
        private readonly ICommand formLoadedCommand;
        private MainExecutor executor;

        /// <summary>
        /// Initializes a new instance of the <see cref="MainWindowViewModel"/> class.
        /// </summary>
        public MainWindowViewModel()
            : this(new RibbonViewModel(), new ViewportViewModel())
        {
        }

        /// <summary>
        /// Initializes a new instance of the <see cref="MainWindowViewModel"/> class with
        /// predefined view-models.
        /// </summary>
        /// <param name="ribbonVM">The <see cref="RibbonViewModel"/> to use.</param>
        /// <param name="viewportVM">The <see cref="ViewportViewModel"/> to use.</param>
        public MainWindowViewModel(RibbonViewModel ribbonVM, ViewportViewModel viewportVM)
        {
            if (ribbonVM == null)
            {
                throw new System.ArgumentNullException(nameof(ribbonVM));
            }

            if (viewportVM == null)
            {
                throw new System.ArgumentNullException(nameof(viewportVM));
            }

            this.ribbonViewModel = ribbonVM;
            this.viewportViewModel = viewportVM;
            this.formLoadedCommand = new BaseCommand(this.FormLoadedAction);
        }

        /// <summary>
        /// Gets the view model of a ribbon control (top ribbon with buttons).
        /// </summary>
        public RibbonViewModel RibbonViewModel
        {
            get
            {
                return this.ribbonViewModel;
            }
        }

        /// <summary>
        /// Gets the view model of a viewport control (that manages 3D rendering).
        /// </summary>
        public ViewportViewModel ViewportViewModel
        {
            get
            {
                return this.viewportViewModel;
            }
        }

        /// <summary>
        /// Gets the command to invoke when the form has been loaded.
        /// </summary>
        public ICommand FormLoadedCommand
        {
            get
            {
                return this.formLoadedCommand;
            }
        }

        /// <summary>
        /// Gets the command that is used to handle MouseLeftButtonDown.
        /// </summary>
        public ICommand MouseLeftButtonDownCommand
        {
            get
            {
                return new BaseCommand<MouseButtonEventArgs>(this.viewportViewModel.MouseLeftButtonDownAction);
            }
        }

        /// <summary>
        /// Gets the command that is used to handle MouseUp.
        /// </summary>
        public ICommand MouseButtonUpCommand
        {
            get
            {
                return new BaseCommand<MouseButtonEventArgs>(this.viewportViewModel.MouseButtonUpAction);
            }
        }

        /// <summary>
        /// Gets the command that is used to clear model parts selection.
        /// </summary>
        public ICommand ClearSelectionCommand
        {
            get
            {
                return new BaseCommand(this.viewportViewModel.ClearSelection);
            }
        }

        /// <summary>
        /// Gets the command that is used to perform model parts selection.
        /// </summary>
        public ICommand SelectAllCommand
        {
            get
            {
                return new BaseCommand(this.viewportViewModel.SelectAll);
            }
        }

        private void FormLoadedAction()
        {
            this.executor = new MainExecutor(this);
        }
    }
}