// <copyright file="RibbonViewModel.cs" company="3Dconnexion">
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
// $Id: RibbonViewModel.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.ViewModels
{
    using System;
    using System.Windows.Input;

    /// <summary>
    /// View-Model class for a top control with buttons.
    /// (<see cref="Views.RibbonView"/>)
    /// </summary>
    public class RibbonViewModel : ViewModel
    {
        private bool buttonsEnabled = true;

        /// <summary>
        /// Initializes a new instance of the <see cref="RibbonViewModel"/> class.
        /// </summary>
        public RibbonViewModel()
        {
            this.EnableRaisingEvents = true;
        }

        /// <summary>
        /// This event is called when the 'About'
        /// button is clicked.
        /// </summary>
        public event EventHandler About;

        /// <summary>
        /// This event is called when the 'clear selection'
        /// button is clicked.
        /// </summary>
        public event EventHandler ClearSelection;

        /// <summary>
        /// This event is called when 'Close file'
        /// button is clicked.
        /// </summary>
        public event EventHandler CloseFile;

        /// <summary>
        /// This event is called when 'Exit'
        /// button is clicked.
        /// </summary>
        public event EventHandler Exit;

        /// <summary>
        /// This event is called when 'Open file...'
        /// button is clicked.
        /// </summary>
        public event EventHandler OpenFile;

        /// <summary>
        /// This event is called when 'parallel camera projection'
        /// button is clicked.
        /// </summary>
        public event EventHandler ParallelProjection;

        /// <summary>
        /// This event is called when 'perspective camera projection'
        /// button is clicked.
        /// </summary>
        public event EventHandler PerspectiveProjection;

        /// <summary>
        /// This event is called when the 'select all'
        /// button is clicked.
        /// </summary>
        public event EventHandler SelectAll;

        /// <summary>
        /// Gets or sets a value indicating whether the events can be raised.
        /// </summary>
        public bool EnableRaisingEvents { get; set; }

        /// <summary>
        /// Gets or sets a value indicating whether the buttons are enabled.
        /// This boolean flag corresponds to IsEnabled property of the button.
        /// </summary>
        public bool ButtonsEnabled
        {
            get
            {
                return this.buttonsEnabled;
            }

            set
            {
                this.buttonsEnabled = value;

                this.OnPropertyChanged();
            }
        }

        /// <summary>
        /// Gets the command to invoke to open the about dialog.
        /// </summary>
        public ICommand AboutCommand
        {
            get
            {
                return new BaseCommand(this.OnAbout);
            }
        }

        /// <summary>
        /// Gets the command to invoke to clear the selection.
        /// </summary>
        public ICommand ClearSelectionCommand
        {
            get
            {
                return new BaseCommand(this.OnClearSelection);
            }
        }

        /// <summary>
        /// Gets the command to invoke to clear the selection.
        /// </summary>
        public ICommand CloseFileCommand
        {
            get
            {
                return new BaseCommand(this.OnCloseFile);
            }
        }

        /// <summary>
        /// Gets the command to invoke to exit the application.
        /// </summary>
        public ICommand ExitCommand
        {
            get
            {
                return new BaseCommand(this.OnExit);
            }
        }

        /// <summary>
        /// Gets the command to invoke to open a file.
        /// </summary>
        public ICommand OpenFileCommand
        {
            get
            {
                return new BaseCommand(this.OnOpenFile);
            }
        }

        /// <summary>
        /// Gets the command to invoke to change the camera to parallel projection.
        /// </summary>
        public ICommand ParallelViewCommand
        {
            get
            {
                return new BaseCommand(this.OnParallelProjection);
            }
        }

        /// <summary>
        /// Gets the command to invoke to change the camera to perspective projection.
        /// </summary>
        public ICommand PerspectiveViewCommand
        {
            get
            {
                return new BaseCommand(this.OnPerspectiveProjection);
            }
        }

        /// <summary>
        /// Gets the command to invoke to change the camera to perspective projection.
        /// </summary>
        public ICommand SelectAllCommand
        {
            get
            {
                return new BaseCommand(this.OnSelectAll);
            }
        }

        private void OnAbout()
        {
            if (this.EnableRaisingEvents)
            {
                this.About?.Invoke(this, EventArgs.Empty);
            }
        }

        private void OnClearSelection()
        {
            if (this.EnableRaisingEvents)
            {
                this.ClearSelection?.Invoke(this, EventArgs.Empty);
            }
        }

        private void OnCloseFile()
        {
            if (this.EnableRaisingEvents)
            {
                this.CloseFile?.Invoke(this, EventArgs.Empty);
            }
        }

        private void OnExit()
        {
            this.Exit?.Invoke(this, EventArgs.Empty);
        }

        private void OnOpenFile()
        {
            if (this.EnableRaisingEvents)
            {
                this.OpenFile?.Invoke(this, EventArgs.Empty);
            }
        }

        private void OnParallelProjection()
        {
            if (this.EnableRaisingEvents)
            {
                this.ParallelProjection?.Invoke(this, EventArgs.Empty);
            }
        }

        private void OnPerspectiveProjection()
        {
            if (this.EnableRaisingEvents)
            {
                this.PerspectiveProjection?.Invoke(this, EventArgs.Empty);
            }
        }

        private void OnSelectAll()
        {
            if (this.EnableRaisingEvents)
            {
                this.SelectAll?.Invoke(this, EventArgs.Empty);
            }
        }
    }
}