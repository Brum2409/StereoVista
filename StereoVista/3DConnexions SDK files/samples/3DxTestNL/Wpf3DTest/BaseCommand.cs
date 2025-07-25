// <copyright file="BaseCommand.cs" company="3Dconnexion">
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
// $Id: BaseCommand.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL
{
    using System;
    using System.Windows.Input;

    /// <summary>
    /// Base command class
    /// </summary>
    public class BaseCommand : ICommand
    {
        private readonly Action execute;

        private readonly Func<bool> canExecute;

        /// <summary>
        /// Initializes a new instance of the <see cref="BaseCommand"/> class.
        /// </summary>
        /// <param name="execute">Action that is executed by the command.</param>
        public BaseCommand(Action execute)
            : this(execute, null)
        {
        }

        /// <summary>
        /// Initializes a new instance of the <see cref="BaseCommand"/> class.
        /// </summary>
        /// <param name="execute">Action that is executed by the command.</param>
        /// <param name="canExecute">The execution condition.</param>
        public BaseCommand(Action execute, Func<bool> canExecute)
        {
            this.execute = execute;
            this.canExecute = canExecute;
        }

        /// <summary>
        /// Occurs when changes occur that affect whether or not the command should execute.
        /// </summary>
        public event EventHandler CanExecuteChanged
        {
            add { CommandManager.RequerySuggested += value; }
            remove { CommandManager.RequerySuggested -= value; }
        }

        /// <summary>
        /// Determines whether the command can execute in its current state.
        /// </summary>
        /// <param name="parameter"> Data used by the command. If the command does not require data to be passed,
        /// this object can be set to null.</param>
        /// <returns>Returns true if this command can be executed; otherwise, false.</returns>
        public bool CanExecute(object parameter)
        {
            return this.canExecute == null || this.canExecute();
        }

        /// <summary>
        /// Defines the method to be called when the command is invoked.
        /// </summary>
        /// <param name="parameter">Data used by the command. If the command does not require data to be passed,
        /// this object can be set to null.</param>
        public void Execute(object parameter)
        {
            this.execute();
        }
    }
}