// <copyright file="NavigationModel.cs" company="3Dconnexion">
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
// $Id: NavigationModel.cs 17485 2020-05-29 12:44:39Z mbonk $
//
// </history>

namespace TDx.TestNL.Navigation
{
    using System;
    using System.Collections.Generic;
    using System.ComponentModel;
    using System.Threading.Tasks;
    using System.Windows.Media.Media3D;
    using System.Windows.Threading;
    using TDx.SpaceMouse.Navigation3D;
    using TDx.TestNL.ViewModels;

    /// <summary>
    /// The <see cref="NavigationModel"/> class implements the <see cref="INavigation3D"/> interface.
    /// </summary>
    /// <remarks>The complete implementation of the <see cref="INavigation3D"/> interface is the union
    /// of the partial <see cref="NavigationModel"/> class implementations of the <see cref="IHit"/>, <see cref="IModel"/>,
    /// <see cref="IPivot"/>, <see cref="ISpace3D"/> and <see cref="IView"/> interfaces.</remarks>
    internal partial class NavigationModel : INavigation3D
    {
        private readonly IViewModelNavigation viewportVM;
        private readonly Navigation3D navigation3D;
        private readonly Dispatcher dispatcher;

        private bool enable = false;
        private string profile;

        /// <summary>
        /// Initializes a new instance of the <see cref="NavigationModel"/> class.
        /// </summary>
        /// <param name="viewportVM">The view model navigation interface<see cref="IViewModelNavigation"/>.</param>
        public NavigationModel(IViewModelNavigation viewportVM)
        {
            this.dispatcher = viewportVM.Camera.Dispatcher;
            this.viewportVM = viewportVM;
            this.viewportVM.PropertyChanged += this.ViewportViewModel_PropertyChanged;
            this.viewportVM.FrameTimeChanged += this.ViewportVM_FrameTimeChanged;
            this.navigation3D = new Navigation3D(this);
            this.navigation3D.ExecuteCommand += this.OnExecuteCommand;
            this.navigation3D.SettingsChanged += this.SettingsChangedHandler;
            this.navigation3D.TransactionChanged += this.TransactionChangedHandler;
            this.navigation3D.MotionChanged += this.MotionChangedHandler;
            this.navigation3D.KeyUp += this.KeyUpHandler;
            this.navigation3D.KeyDown += this.KeyDownHandler;
        }

        /// <summary>
        /// Is invoked when the user invokes an application command.
        /// </summary>
        public event EventHandler<CommandEventArgs> ExecuteCommand;

        /// <summary>
        /// Gets or sets a value indicating whether the navigation is enabled.
        /// </summary>
        /// <exception cref="System.DllNotFoundException">Cannot find the 3DxWare driver library.</exception>
        public bool Enable
        {
            get
            {
                return this.enable;
            }

            set
            {
                if (value != this.enable)
                {
                    if (value)
                    {
                        this.navigation3D.Open3DMouse(this.profile);
                    }
                    else
                    {
                        this.navigation3D.Close();
                    }

                    this.navigation3D.FrameTiming = Navigation3D.TimingSource.Application;
                    this.navigation3D.EnableRaisingEvents = value;
                    this.enable = value;
                }
            }
        }

        /// <summary>
        /// Gets or sets the name for the profile to use.
        /// </summary>
        public string Profile
        {
            get
            {
                return this.profile;
            }

            set
            {
                if (this.profile != value)
                {
                    this.profile = value;
                    if (this.enable)
                    {
                        this.navigation3D.Close();
                        this.navigation3D.Open3DMouse(this.profile);
                    }
                }
            }
        }

        /// <summary>
        /// Sets the model extents used by the navigation model instance.
        /// </summary>
        /// <remarks>Notify the navigation when the model has changed that the dimensions have changed.</remarks>
        public Rect3D ModelExtents
        {
            set
            {
                this.navigation3D.Properties.WriteAsync(PropertyNames.ModelExtents, value.AsBox());
            }
        }

        /// <summary>
        /// Gets or sets the active set of commands.
        /// </summary>
        public string ActiveCommands
        {
            get
            {
                return this.navigation3D.ActiveCommandSet;
            }

            set
            {
                this.navigation3D.ActiveCommandSet = value;
            }
        }

        /// <summary>
        /// Add commands to the sets of commands.
        /// </summary>
        /// <param name="commands">The <see cref="CommandTree"/> to add.</param>
        public void AddCommands(CommandTree commands)
        {
            Task t = Task.Run(() =>
            {
                List<Image> images = new List<Image>();
                foreach (CommandSet set in commands)
                {
                    this.GetImages(set, images);
                }
                if (images.Count > 0)
                {
                    this.navigation3D.AddImages(images);
                }
            });

            this.navigation3D.AddCommands(commands);

            t.Wait();
        }

        /// <summary>
        /// Add a set of commands to the sets of commands.
        /// </summary>
        /// <param name="commands">The <see cref="CommandSet"/> to add.</param>
        public void AddCommandSet(CommandSet commands)
        {
            CommandTree tree = new CommandTree
            {
                commands
            };

            this.AddCommands(tree);
        }

        /// <summary>
        /// Add to the images available to the 3D mouse.
        /// </summary>
        /// <param name="images">The <see cref="List{Image}"/> containing the images to add.</param>
        public void AddImages(List<Image> images)
        {
            this.navigation3D.AddImages(images);
        }

        private void OnExecuteCommand(object sender, CommandEventArgs eventArgs)
        {
            this.dispatcher.InvokeIfRequired(() => this.ExecuteCommand?.Invoke(sender, eventArgs));
        }

        private void KeyDownHandler(object sender, KeyEventArgs eventArgs)
        {
        }

        private void KeyUpHandler(object sender, KeyEventArgs eventArgs)
        {
        }

        private void MotionChangedHandler(object sender, MotionEventArgs eventArgs)
        {
            this.dispatcher.InvokeAsync(() =>
            {
                this.viewportVM.Animating = eventArgs.IsNavigating;
                if (!eventArgs.IsNavigating)
                {
                    // Camera/Object navigation has ended
                    // Set the locally stored selection transform to identity
                    this.selectionTransform.Matrix = Matrix3D.Identity;
                }
            });
        }

        private void SettingsChangedHandler(object sender, EventArgs eventArgs)
        {
        }

        private void TransactionChangedHandler(object sender, TransactionEventArgs eventArgs)
        {
            if (eventArgs.IsBegin)
            {
                this.viewportVM.BeginTransaction();
            }
            else
            {
                this.viewportVM.EndTransaction();
            }
        }

        /// <summary>
        /// Handle view model properties changes that the navigation needs to know about.
        /// </summary>
        /// <param name="sender">The sender of the event.</param>
        /// <param name="e">The <see cref="PropertyChangedEventArgs"/>.</param>
        private void ViewportViewModel_PropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            switch (e.PropertyName)
            {
                case nameof(ViewportViewModel.Model):
                    {
                        this.selectionTransform.Matrix = Matrix3D.Identity;
                        this.ModelExtents = this.viewportVM.Model != null ? this.viewportVM.Model.Bounds : default(Rect3D);
                        break;
                    }

                case nameof(ViewportViewModel.SelectedModel):
                    {
                        this.navigation3D.Properties.WriteAsync(PropertyNames.SelectionEmpty, this.viewportVM.SelectedModel.Children.Count == 0);
                        break;
                    }

                case nameof(ViewportViewModel.IsUserPivot):
                    {
                        this.navigation3D.Properties.WriteAsync(PropertyNames.PivotUser, this.viewportVM.IsUserPivot);
                        break;
                    }
            }
        }

        /// <summary>
        /// Handle the viewport frame time changed event.
        /// </summary>
        /// <param name="sender">The sender of the event.</param>
        /// <param name="e">A <see cref="FrameTimeChangedEventArgs"/> contains the event data.</param>
        private void ViewportVM_FrameTimeChanged(object sender, FrameTimeChangedEventArgs e)
        {
            this.navigation3D.FrameTime = e.TimeStamp;
        }

        private void GetImages(CommandTreeNode set, List<Image> images)
        {
            foreach (CommandTreeNode node in set.Children)
            {
                if (node is Command)
                {
                    Command command = node as Command;
                    if (command.Image != null)
                    {
                        if (command.Image.Id == node.Id)
                        {
                            images.Add(command.Image);
                        }
                        else
                        {
                            Image image = command.Image.Copy();
                            image.Id = node.Id;
                            images.Add(image);
                        }
                    }
                }
                else
                {
                    this.GetImages(node, images);
                }
            }
        }
    }
}