// <copyright file="Application.cs" company="3Dconnexion">
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
    using System;
    using System.Collections.Generic;
    using System.IO;
    using System.Windows.Forms;
    using TDx.SpaceMouse.Navigation3D;

    /// <summary>
    /// Tha application <see cref="Form"/>.
    /// </summary>
    public partial class Application : Form
    {
        private readonly TDx.SpaceMouse.ActionInput.ActionInput actionInput = new TDx.SpaceMouse.ActionInput.ActionInput();
        private Dictionary<string, EventHandler> applicationCommands;

        /// <summary>
        /// Initializes a new instance of the <see cref="Application"/> class.
        /// </summary>
        public Application()
        {
            this.InitializeComponent();
            this.actionInput.ExecuteCommand += this.ActionInput_ExecuteCommand;
            this.actionInput.KeyDown += this.ActionInput_KeyDown;
            this.actionInput.KeyUp += this.ActionInput_KeyUp;
            this.actionInput.SettingsChanged += this.ActionInput_SettingsChanged;
            this.actionInput.EnableRaisingEvents = true;
            this.actionInput.Open3DMouse("ActionInput Sample");

            this.ExportApplicationCommands();
            this.ExportCommandImages();
        }

        /// <inheritdoc/>
        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            this.actionInput.Close();
            base.OnFormClosed(e);
        }

        private void ExportApplicationCommands()
        {
            this.applicationCommands = new Dictionary<string, EventHandler>()
            {
                { "ID_EXIT", (s, e) => this.Exit() },
                { "ID_ABOUT", (s, e) => this.About() },
            };

            // A CommandSet can also be considered to be a buttonbank, a menubar, or a set of toolbars
            CommandSet menuBar = new CommandSet("Default", "Ribbon");

            // Create some categories / menus / tabs to the menu
            Category fileMenu = new Category("FileMenu", "File");
            fileMenu.Add(new Command("ID_OPEN", "Open...", "Open a file."));
            fileMenu.Add(new Command("ID_CLOSE", "Close", "Close the current document."));
            fileMenu.Add(new Command("ID_EXIT", "Exit", "Exit the appliction."));
            menuBar.Add(fileMenu);

            Category selectMenu = new Category("SelectMenu", "Selection");
            selectMenu.Add(new Command("ID_SELECTALL", "Select All", "Select the whole document."));
            selectMenu.Add(new Command("ID_CLEARSELECTION", "Clear Selection", "Revert the current selection."));
            menuBar.Add(selectMenu);

            Category viewsMenu = new Category("ViewsMenu", "View");
            viewsMenu.Add(new Command("ID_PARALLEL", "Parallel", "Set the view to an orthographic projection"));
            viewsMenu.Add(new Command("ID_PERSPECTIVE", "Perspective", "Set the view to a perspective projection"));
            menuBar.Add(viewsMenu);

            Category helpMenu = new Category("HelpMenu", "Help");
            helpMenu.Add(new Command("ID_ABOUT", "About...", "Display information about the application"));
            menuBar.Add(helpMenu);

            this.actionInput.AddCommands(new CommandTree() { menuBar });
            this.actionInput.ActiveCommandSet = menuBar.Id;
        }

        private void ExportCommandImages()
        {
            List<Image> images = new List<Image>()
            {
                Image.FromData(Properties.Resources._3dx_viewer_icon_menu_open_hover.ToByteArray(), 0, "ID_OPEN"),
                Image.FromData(Properties.Resources._3dx_viewer_icon_close_std.ToByteArray(), 0, "ID_CLOSE"),
                Image.FromData(Properties.Resources._3dx_viewer_icon_menu_quit_hover.ToByteArray(), 0, "ID_EXIT"),
                Image.FromResource(@"c:\windows\system32\shell32.dll", "#3", "#24", 0, "ID_ABOUT"),
                Image.FromData(Properties.Resources._3dx_viewer_icon_select_all_std.ToByteArray(), 0, "ID_SELECTALL"),
                Image.FromData(Properties.Resources._3dx_viewer_icon_clear_selection_std.ToByteArray(), 0, "ID_CLEARSELECTION"),
                Image.FromData(Properties.Resources._3dx_viewer_icon_perspective_view_std.ToByteArray(), 0, "ID_PERSPECTIVE"),
                Image.FromData(Properties.Resources._3dx_viewer_icon_parallel_view_std.ToByteArray(), 0, "ID_PARALLEL"),
            };

            this.actionInput.AddImages(images);
        }

        private void About()
        {
            MessageBox.Show(
                this,
                "About command invoked.\n\n********************************************************\n*           3Dconnexion ActionInput Sample             *\n*   Copyright (c) 3Dconnexion. All rights reserved.    *\n********************************************************\n\n",
                "About");
        }

        private void Exit()
        {
            MessageBox.Show(this, "Exit command invoked.", "Exit");
            this.Close();
        }

        private void ActionInput_SettingsChanged(object sender, EventArgs e)
        {
            MessageBox.Show(this, "A setting has changed.", "SettingsChanged Event");
        }

        private void ActionInput_KeyUp(object sender, TDx.SpaceMouse.Navigation3D.KeyEventArgs e)
        {
            string message = "KeyEventArgs\nKey=" + e.Key.ToString() + "\nIsUp=" + e.IsUp.ToString() + "\nTimeStamp=" + e.TimeStamp.ToString() + "\nHandled=" + e.Handled.ToString();
            MessageBox.Show(this, message, "KeyUp Event");
        }

        private void ActionInput_KeyDown(object sender, TDx.SpaceMouse.Navigation3D.KeyEventArgs e)
        {
            string message = "KeyEventArgs\nKey=" + e.Key.ToString() + "\nIsDown=" + e.IsDown.ToString() + "\nTimeStamp=" + e.TimeStamp.ToString() + "\nHandled=" + e.Handled.ToString();
            MessageBox.Show(this, message, "KeyDown Event");
        }

        private void ActionInput_ExecuteCommand(object sender, TDx.SpaceMouse.Navigation3D.CommandEventArgs e)
        {
            EventHandler handler;
            if (string.IsNullOrEmpty(e.Command))
            {
                return;
            }

            if (this.applicationCommands.TryGetValue(e.Command, out handler))
            {
                handler(this, e);
                e.Handled = true;
            }

            string message = "CommandEventArgs\nCommand=" + e.Command + "\nTimeStamp=" + e.TimeStamp.ToString() + "\nHandled=" + e.Handled.ToString();

            try
            {
                MessageBox.Show(this, message, "ExecuteCommand Event");
            }
            catch (ObjectDisposedException)
            {
                // Because of the exit command.
            }
        }
    }
}
