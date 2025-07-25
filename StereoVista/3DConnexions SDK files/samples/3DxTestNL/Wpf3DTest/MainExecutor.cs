// <copyright file="MainExecutor.cs" company="3Dconnexion">
// -------------------------------------------------------------------------------------
// Copyright (c) 2018-2021 3Dconnexion. All rights reserved.
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
// $Id: MainExecutor.cs 20398 2023-09-05 07:02:10Z mbonk $
//
// </history>

namespace TDx.TestNL
{
    using System;
    using System.Collections.Generic;
    using System.Diagnostics;
    using System.IO;
    using System.Reflection;
    using System.Windows;
    using Microsoft.Win32;
    using TDx.SpaceMouse.Navigation3D;
    using TDx.TestNL.ModelLoader;
    using TDx.TestNL.ModelLoader.Readers;
    using TDx.TestNL.Navigation;
    using TDx.TestNL.ViewModels;
    using TDx.TestNL.ViewModels.Utils;
    using ProjectResources = TDx.TestNL.Properties.Resources;

    /// <summary>
    /// This class is instantiated when application starts and handles all calls and initialization.
    /// </summary>
    internal class MainExecutor
    {
        private MainWindowViewModel mainWindowVM;
        private NavigationModel navigationModel;
        private Dictionary<string, EventHandler> applicationCommands;

        /// <summary>
        /// Initializes a new instance of the <see cref="MainExecutor"/> class.
        /// </summary>
        /// <param name="mainWindowVM">The <see cref="MainWindowViewModel"/> View-Model instance for application's main window.</param>
        public MainExecutor(MainWindowViewModel mainWindowVM)
        {
            this.mainWindowVM = mainWindowVM;

            // set event handlers for ribbon buttons that are provided via the <see cref="RibbonViewModel"/>.
            this.mainWindowVM.RibbonViewModel.ClearSelection += (s, e) => this.mainWindowVM.ViewportViewModel.ClearSelection();
            this.mainWindowVM.RibbonViewModel.CloseFile += this.CloseFileHandler;
            this.mainWindowVM.RibbonViewModel.OpenFile += this.OpenFileHandler;
            this.mainWindowVM.RibbonViewModel.ParallelProjection += (s, e) => this.mainWindowVM.ViewportViewModel.Projection = Projection.Orthographic;
            this.mainWindowVM.RibbonViewModel.PerspectiveProjection += (s, e) => this.mainWindowVM.ViewportViewModel.Projection = Projection.Perspective;
            this.mainWindowVM.RibbonViewModel.SelectAll += (s, e) => this.mainWindowVM.ViewportViewModel.SelectAll();
            this.mainWindowVM.RibbonViewModel.Exit += this.ExitHandler;
            this.mainWindowVM.RibbonViewModel.About += this.AboutHandler;

            // Initialize the SpaceMouse navigation model
            try
            {
                this.navigationModel = new NavigationModel(mainWindowVM.ViewportViewModel)
                {
                    Profile = Constants.ProgramName,
                    Enable = true
                };
                this.navigationModel.ExecuteCommand += this.ExecuteCommandHandler;

                // Export the applications commands for the 3Dconnexion Settings Utility
                this.ExportApplicationCommands();
            }
            catch (System.DllNotFoundException e)
            {
                MessageBox.Show(e.Message + "\n\nThe required 3Dconnexion 3DxWare driver may not be installed.", "SpaceMouse Navigation Model failed to initialize");
            }
        }

        private void ExportApplicationCommands()
        {
            this.applicationCommands = new Dictionary<string, EventHandler>()
            {
                { "ID_OPEN", this.OpenFileHandler },
                { "ID_CLOSE", this.CloseFileHandler },
                { "ID_EXIT", this.ExitHandler },
                { "ID_ABOUT", this.AboutHandler },
                { "ID_SELECTALL", (s, e) => this.mainWindowVM.ViewportViewModel.SelectAll() },
                { "ID_CLEARSELECTION", (s, e) => this.mainWindowVM.ViewportViewModel.ClearSelection() },
                { "ID_PARALLEL", (s, e) => this.mainWindowVM.ViewportViewModel.Projection = Projection.Orthographic },
                { "ID_PERSPECTIVE", (s, e) => this.mainWindowVM.ViewportViewModel.Projection = Projection.Perspective }
            };

            // A CommandSet can also be considered to be a buttonbank, a menubar, or a set of toolbars
            CommandSet menuBar = new CommandSet("Default", "Ribbon");

            // Create some categories / menus / tabs to the menu
            Category fileMenu = new Category("FileMenu", "File");
            fileMenu.Add(new Command("ID_OPEN", ProjectResources.OpenFile, ProjectResources.ToolTipOpenFile, Image.FromData(ProjectResources.OpenIcon.ToByteArray())));
            fileMenu.Add(new Command("ID_CLOSE", ProjectResources.CloseFile, ProjectResources.ToolTipCloseFile, Image.FromData(ProjectResources.CloseIcon.ToByteArray())));
            fileMenu.Add(new Command("ID_EXIT", ProjectResources.Exit, null, Image.FromData(ProjectResources.QuitIcon.ToByteArray())));
            menuBar.Add(fileMenu);

            Category selectMenu = new Category("SelectMenu", "Selection");
            selectMenu.Add(new Command("ID_SELECTALL", ProjectResources.SelectAll, null, Image.FromData(ProjectResources.SelectAllIcon.ToByteArray())));
            selectMenu.Add(new Command("ID_CLEARSELECTION", ProjectResources.ClearSelection, null, Image.FromData(ProjectResources.ClearSelectionIcon.ToByteArray())));
            menuBar.Add(selectMenu);

            Category viewsMenu = new Category("ViewsMenu", "View");
            viewsMenu.Add(new Command("ID_PARALLEL", ProjectResources.ParallelView, ProjectResources.ToolTipParallelView, Image.FromData(ProjectResources.ParallelViewIcon.ToByteArray())));
            viewsMenu.Add(new Command("ID_PERSPECTIVE", ProjectResources.PerspectiveView, ProjectResources.ToolTipPerspectiveView, Image.FromData(ProjectResources.PerspectiveViewIcon.ToByteArray())));
            menuBar.Add(viewsMenu);

            string homePath = this.Get3DxWareHomeDirectory();
            Category helpMenu = new Category("HelpMenu", "Help");
            helpMenu.Add(new Command("ID_ABOUT", ProjectResources.About, ProjectResources.ToolTipAbout, Image.FromResource(Path.Combine(homePath, @"en-US\3DxService.dll"), "#2", "#172")));
            menuBar.Add(helpMenu);

            this.navigationModel.AddCommandSet(menuBar);

            this.navigationModel.ActiveCommands = menuBar.Id;
        }

        private void AboutHandler(object sender, EventArgs e)
        {
            this.mainWindowVM.RibbonViewModel.ButtonsEnabled = false;

            Assembly assembly = Assembly.GetExecutingAssembly();
            string version = AssemblyName.GetAssemblyName(assembly.Location).Version.ToString();

            var versionInfo = FileVersionInfo.GetVersionInfo(Assembly.GetEntryAssembly().Location);
            string copyright = versionInfo.LegalCopyright;
            string trademarks = versionInfo.LegalTrademarks;

            string message = Constants.ProgramName + " Version " + version + "\n" + copyright + ". " + trademarks;
            MessageBox.Show(message, Application.Current.MainWindow.Title);

            this.mainWindowVM.RibbonViewModel.ButtonsEnabled = true;
        }

        private void CloseFileHandler(object sender, EventArgs e)
        {
            this.mainWindowVM.ViewportViewModel.Model = null;
        }

        private void ExecuteCommandHandler(object sender, CommandEventArgs e)
        {
            EventHandler handler;
            if (this.applicationCommands.TryGetValue(e.Command, out handler))
            {
                handler(this.mainWindowVM.RibbonViewModel, e);
                e.Handled = true;
            }
        }

        private void ExitHandler(object sender, EventArgs e)
        {
            Application.Current.MainWindow.Close();
        }

        private void OpenFileHandler(object sender, EventArgs e)
        {
            // Disable the events during model loading.
            this.mainWindowVM.RibbonViewModel.EnableRaisingEvents = false;

            string filePath;

            var ofd = new OpenFileDialog
            {
                Filter = ModelLoader.Constants.SupportedFormatsFilter
            };

            bool? result = ofd.ShowDialog();
            if (result == true)
            {
                this.mainWindowVM.ViewportViewModel.Model = null;
                filePath = ofd.FileName;
                using (var reader = new ObjReader())
                {
                    // Subscribe for the LoadingChangedCallback to track
                    // when the model is loaded.
                    reader.LoadingChanged += this.LoadingChangedCallback;

                    // Read the model from file.
                    reader.Load(filePath);
                }
            }
            else
            {
                // Re-enable the events.
                this.mainWindowVM.RibbonViewModel.EnableRaisingEvents = true;
            }
        }

        // Callback method for model loading progress changes.
        private void LoadingChangedCallback(object sender, ModelReaderEventArgs e)
        {
            if (e.Finished)
            {
                if (e.Success)
                {
                    var modelProvider = sender as IModelProvider;
                    var model = GeometryConvertor.Model3DGroup(modelProvider);
                    this.mainWindowVM.ViewportViewModel.Model = model;
                }
                else
                {
                    MessageBox.Show(
                        ProjectResources.ModelLoadingFailed,
                        ProjectResources.Error,
                        MessageBoxButton.OK,
                        MessageBoxImage.Error);
                }

                // Model loading has finished. Re-enable the ribbon
                this.mainWindowVM.RibbonViewModel.EnableRaisingEvents = true;
            }
        }

        private string Get3DxWareHomeDirectory()
        {
            string softwareKeyName = string.Empty;
            string homeDirectory = string.Empty;

            if (IntPtr.Size == 8)
            {
                softwareKeyName = @"HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\3Dconnexion\3DxWare";
            }
            else
            {
                softwareKeyName = @"HKEY_LOCAL_MACHINE\SOFTWARE\3Dconnexion\3DxWare";
            }

            object regValue = Microsoft.Win32.Registry.GetValue(softwareKeyName, "Home Directory", null);
            if (regValue != null)
            {
                homeDirectory = regValue.ToString();
            }

            return homeDirectory;
        }
    }
}