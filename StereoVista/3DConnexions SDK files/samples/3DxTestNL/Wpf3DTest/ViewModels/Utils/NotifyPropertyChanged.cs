// <copyright file="NotifyPropertyChanged.cs" company="3Dconnexion">
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
// $Id: NotifyPropertyChanged.cs 15499 2018-11-06 07:31:28Z mbonk $
//
// </history>

namespace TDx.TestNL.ViewModels.Utils
{
    using System.Collections.Generic;
    using System.ComponentModel;
    using System.Runtime.CompilerServices;

    /// <summary>
    /// Abstract implementation of the <see cref="INotifyPropertyChanged"/> interface
    /// </summary>
    public abstract class NotifyPropertyChanged : INotifyPropertyChanged
    {
        private HashSet<string> changedProperties = new HashSet<string>();

        private bool suspendRaisingPropertyChangedEvents = false;

        /// <summary>
        ///  Occurs when a dependency property value changes
        /// </summary>
        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>
        /// Gets or sets a value indicating whether raising property changed events is suspended.
        /// </summary>
        public bool SuspendRaisingPropertyChangedEvents
        {
            get
            {
                return this.suspendRaisingPropertyChangedEvents;
            }

            set
            {
                if (this.suspendRaisingPropertyChangedEvents == value)
                {
                    return;
                }

                this.suspendRaisingPropertyChangedEvents = value;
                if (!this.suspendRaisingPropertyChangedEvents)
                {
                    HashSet<string> changedProperties = this.changedProperties;
                    this.changedProperties = new HashSet<string>();
                    foreach (string propertyName in changedProperties)
                    {
                        this.OnPropertyChanged(propertyName);
                    }
                }
            }
        }

        /// <summary>
        /// Invoke whenever the effective value of any dependency property has been updated.
        /// The specific dependency property that changed is reported in the event data.
        /// </summary>
        /// <param name="propertyName">The name of the property that changed.</param>
        protected void OnPropertyChanged([CallerMemberName] string propertyName = null)
        {
            if (this.suspendRaisingPropertyChangedEvents)
            {
                this.changedProperties.Add(propertyName);
            }
            else
            {
                this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
            }
        }
    }
}