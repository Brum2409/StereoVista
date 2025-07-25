// <copyright file="InteractiveCommand.cs" company="3Dconnexion">
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
// $Id: InteractiveCommand.cs 17356 2020-04-21 07:02:08Z mbonk $
//
// </history>

namespace TDx.TestNL.ViewModels.Utils
{
    using System;
    using System.Reflection;
    using System.Windows;
    using System.Windows.Input;
    using Microsoft.Xaml.Behaviors;

    /// <summary>
    /// This class is used to call WPF commands and pass them arguments.
    /// </summary>
    public class InteractiveCommand : TriggerAction<DependencyObject>
    {
        /// <summary>
        ///  Using a DependencyProperty as the backing store for Command.  This enables animation, styling, binding, etc...
        /// </summary>
        public static readonly DependencyProperty CommandProperty =
            DependencyProperty.Register("Command", typeof(ICommand), typeof(InteractiveCommand), new UIPropertyMetadata(null));

        private string commandName;

        /// <summary>
        /// Gets or sets the name of the command
        /// </summary>
        public string CommandName
        {
            get
            {
                this.ReadPreamble();
                return this.commandName;
            }

            set
            {
                if (this.CommandName != value)
                {
                    this.WritePreamble();
                    this.commandName = value;
                    this.WritePostscript();
                }
            }
        }

        /// <summary>
        /// Gets or sets the command
        /// </summary>
        public ICommand Command
        {
            get { return (ICommand)this.GetValue(CommandProperty); }
            set { this.SetValue(CommandProperty, value); }
        }

        /// <summary>
        /// Invokes the action.
        /// </summary>
        /// <param name="parameter">The parameter to the action. If the action does not require a
        /// parameter, the parameter may be set to a null reference.parameter may be set to a null
        /// reference.</param>
        protected override void Invoke(object parameter)
        {
            if (this.AssociatedObject != null)
            {
                ICommand command = this.ResolveCommand();
                if ((command != null) && command.CanExecute(parameter))
                {
                    command.Execute(parameter);
                }
            }
        }

        private ICommand ResolveCommand()
        {
            ICommand command = null;
            if (this.Command != null)
            {
                return this.Command;
            }

            if (this.AssociatedObject != null)
            {
                foreach (PropertyInfo info in this.AssociatedObject.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
                {
                    if (typeof(ICommand).IsAssignableFrom(info.PropertyType) && string.Equals(info.Name, this.CommandName, StringComparison.Ordinal))
                    {
                        command = (ICommand)info.GetValue(this.AssociatedObject, null);
                    }
                }
            }

            return command;
        }
    }
}