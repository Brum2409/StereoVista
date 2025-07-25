// <copyright file="Reader.cs" company="3Dconnexion">
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

namespace TDx.TestNL.ModelLoader.Readers
{
    using System;
    using System.ComponentModel;
    using TDx.TestNL.ModelLoader.Geometry;

    /// <summary>
    /// This class is an adapted version of Loader from
    /// 3DxViewer project.
    /// </summary>
    public abstract class Reader : IModelProvider, IDisposable
    {
        public event EventHandler<ModelReaderEventArgs> LoadingChanged;

        /// <inheritdoc/>
        public Model3D Model { get; protected set; }

        public bool CompletionFlag { get; private set; }

        public string Path { get; private set; }

        protected BackgroundWorker Worker { get; set; }

        /// <inheritdoc/>
        public void Dispose()
        {
            GC.SuppressFinalize(this.Worker);
            this.Worker.Dispose();
        }

        public void Load(string path)
        {
            this.Path = path;

            this.Worker = new BackgroundWorker
            {
                WorkerSupportsCancellation = false
            };
            this.Worker.RunWorkerCompleted += this.WorkerCompletedCallback;
            this.Worker.DoWork += this.WorkerCallback;
            this.Worker.RunWorkerAsync();
        }

        /// <summary>
        /// Worker
        /// </summary>
        /// <param name="sender">The source of the event.</param>
        /// <param name="e">An object that contains the event data.</param>
        protected abstract void WorkerCallback(object sender, DoWorkEventArgs e);

        private void WorkerCompletedCallback(object sender, RunWorkerCompletedEventArgs e)
        {
            if (e.Error == null)
            {
                this.Model = e.Result as Model3D;
                this.CompletionFlag = true;
            }

            this.LoadingChanged?.Invoke(this, new ModelReaderEventArgs(success: e.Error == null, finished: true));
        }
    }
}