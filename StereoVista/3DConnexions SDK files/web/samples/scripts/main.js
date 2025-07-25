/**
 * Copyright (c) 2013-2018 3Dconnexion. All rights reserved.
 * License:
 *   This file in licensed under the terms of the '3Dconnexion
 *   Software Development Kit' license agreement found in the
 *   'LicenseAgreementSDK.txt' file.
 *   All rights not expressly granted by 3Dconnexion are reserved.
 *
 * $Revision: 21321 $
 */
requirejs(["three", "3dconnexion.min"], function () {
    const PERSPECTIVE = true;
    function GL() {
        this.gl = {};
        this.model = null;
        this.renderer = null;
        this.camera = null;
        this.scene = null;
        this.grid = null;
        this.axes = null;

        this.animating = false;

        this.initGL = function (container) {
            const self = this;
            try {
                self.gl.viewportWidth = container.offsetWidth;
                self.gl.viewportHeight = container.offsetHeight;
                const aspect = self.gl.viewportWidth / self.gl.viewportHeight;

                self.gl.fov = 33; // vertical fov
                self.gl.frustumNear = 0.01;
                self.gl.frustumFar = 1000;
                self.gl.left =
                    -8 * Math.tan((self.gl.fov * Math.PI) / 360) * aspect;
                self.gl.right = -self.gl.left;
                self.gl.bottom = self.gl.left / aspect;
                self.gl.top = -self.gl.bottom;

                // Create a WebGL renderer
                self.renderer = new THREE.WebGLRenderer({ antialias: true });
                self.renderer.setClearColor(0x000000, 1);

                // set the renderer viewport
                self.renderer.setSize(
                    self.gl.viewportWidth,
                    self.gl.viewportHeight
                );

                // attach the renderer supplied DOM element to the container
                container.appendChild(self.renderer.domElement);
            } catch (e) {
                // do nothing
            }
            if (!this.renderer) {
                alert("Could not initialize WebGL, sorry :-(");
            }
        };

        this.webGLStart = function () {
            const self = this;
            const container = document.getElementById("container");

            self.initGL(container);

            self.scene = new THREE.Scene();
            self.grid = new THREE.GridHelper(5, 0.5);
            self.grid.setColors(
                new THREE.Color(0x888888),
                new THREE.Color(0x444444)
            );
            self.scene.add(self.grid);
            self.axes = new THREE.AxisHelper(1);
            self.scene.add(self.axes);

            // move the grid down a bit
            self.grid.translateY(-0.75);

            if (PERSPECTIVE)
                self.camera = new THREE.PerspectiveCamera(
                    self.gl.fov,
                    self.gl.viewportWidth / self.gl.viewportHeight,
                    self.gl.frustumNear,
                    self.gl.frustumFar
                );
            else
                self.camera = new THREE.OrthographicCamera(
                    self.gl.left,
                    self.gl.right,
                    self.gl.top,
                    self.gl.bottom,
                    -self.gl.frustumFar,
                    self.gl.frustumFar
                );

            // move the camera to [0,0,10]
            self.camera.position.set(0, 0, 10);
            self.camera.lookAt(self.scene.position);

            // add the camera to the scene
            self.scene.add(self.camera);

            // load the model and add it to the scene
            self.model = self.loadModel();
            self.scene.add(self.model);

            // draw
            self.updateScene();

            // initialize the 3d mouse
            self.init3DMouse();
        };

        this.loadModel = function () {
            const pyramidGeometry = new THREE.CylinderGeometry(
                0,
                1.5,
                1.5,
                4,
                false
            );
            for (let i = 0; i < pyramidGeometry.faces.length; i++) {
                if (pyramidGeometry.faces[i] instanceof THREE.Face4) {
                    pyramidGeometry.faces[i].vertexColors[0] = new THREE.Color(
                        0xff0000
                    );
                    if (i % 2 == 0) {
                        pyramidGeometry.faces[i].vertexColors[1] =
                            new THREE.Color(0x00ff00);
                        pyramidGeometry.faces[i].vertexColors[2] =
                            new THREE.Color(0x0000ff);
                    } else {
                        pyramidGeometry.faces[i].vertexColors[1] =
                            new THREE.Color(0x0000ff);
                        pyramidGeometry.faces[i].vertexColors[2] =
                            new THREE.Color(0x00ff00);
                    }
                    pyramidGeometry.faces[i].vertexColors[3] = new THREE.Color(
                        0xff0000
                    );
                } else {
                    pyramidGeometry.faces[i].vertexColors[0] = new THREE.Color(
                        0xff0000
                    );
                    pyramidGeometry.faces[i].vertexColors[1] = new THREE.Color(
                        0x00ff00
                    );
                    pyramidGeometry.faces[i].vertexColors[2] = new THREE.Color(
                        0x0000ff
                    );
                }
            }

            const pyramidMaterial = new THREE.MeshBasicMaterial({
                vertexColors: THREE.VertexColors,
                side: THREE.DoubleSide,
            });

            // move the geometry away from [0,0,0]
            const transform = new THREE.Matrix4();
            transform.makeTranslation(-1.5, 0.0, 4.0);
            pyramidGeometry.applyMatrix(transform);

            pyramidGeometry.computeBoundingBox();

            const model = new THREE.Mesh(pyramidGeometry, pyramidMaterial);
            return model;
        };

        // update the scene after a change
        this.updateScene = function () {
            // if we are not animating request an animation frame
            // otherwise do nothing as the scene will be redrawn in the animation loop
            const self = this;
            if (!self.animating) {
                window.requestAnimationFrame(self.render.bind(self));
            }
        };

        // the callback that results in the scene being rendered
        this.render = function (time) {
            const self = this;
            if (self.animating) {
                // Initiate a new frame transaction by updating the controller with the frame time.
                self.spaceMouse.update3dcontroller({
                    frame: { time: time },
                });
                // Request an animation frame for the next incoming transaction data.
                window.requestAnimationFrame(self.render.bind(self));
            }

            // Render the current scene.
            self.renderer.render(self.scene, self.camera);
        };

        ///////////////////////////////////////////////////////////////////////
        // the 3dconnexion.js callbacks
        // getCoordinateSystem is queried to determine the coordinate system of the application
        // described as X to the right, Y-up and Z out of th screen
        // Normally this will be the inverse of the pose of the front view
        this.getCoordinateSystem = function () {
            // In this sample the cs has X to the right, Y-up, and Z out of the screen
            return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
            // The following would be for X to the right, Z-up, Y into the screen
            // return [1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1];
        };

        // getConstructionPlane is queried in orthographic projections to distinguish between 3D and 2D projections
        // In an axes aligned projection with the camera looking down the normal of the construction plane.
        // Effectively this means that in the orthographic projection in this sample 3D mouse rotations will be disabled
        // when the top or bottom view is selected.
        this.getConstructionPlane = function () {
            const self = this;

            // a point on the construction plane
            const origin = new THREE.Vector3(0, 0, 0);
            origin.applyMatrix4(self.grid.matrixWorld);

            // In this sample the up-axis is the y-axis
            const yAxis = new THREE.Vector3();
            self.grid.matrixWorld.extractBasis(
                new THREE.Vector3(),
                yAxis,
                new THREE.Vector3()
            );
            const d0 = yAxis.dot(origin);
            // return the plane equation as an array
            return [yAxis.x, yAxis.y, yAxis.z, -d0];
        };

        // getFov is called when the navlib requests the fov
        // in three.js the fov is in degrees, the 3dconnexion lib uses radians
        // in three.js the fov is the vertical fov.
        // In this example we return the diagonal fov
        this.getFov = function () {
            const fov =
                2 *
                Math.atan(
                    Math.tan2(this.camera.fov * Math.PI, 360) *
                        Math.sqrt(1 + this.camera.aspect * this.camera.aspect)
                );
            if (TRACE_MESSAGES) console.log("fov=" + (fov * 180) / Math.PI);

            return fov;
        };

        this.getFrontView = function () {
            // In this sample the front view has X to the right, Y-up, and Z out of the screen
            return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
            // The following would be for X to the right, Z-up, Y into the screen
            // return [1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,0,1];
        };

        // getLookAt is called when the navlib needs to know if a ray fired into the screen
        // hits a surface of the model.
        // origin: is the origin of the ray
        // direction: is the rays's direction
        // aperture: is the diameter of the ray
        // onlySelection: true - only attempt hits on the selection set, false - everything
        this.getLookAt = function () {
            const self = this;

            // Create a raycaster
            const raycaster = new THREE.Raycaster(
                self.look.origin,
                self.look.direction,
                self.gl.frustumNear,
                self.gl.frustumFar
            );
            raycaster.precision = self.look.aperture / 2;
            raycaster.linePrecision = self.look.aperture / 2;

            // do the hit-testing
            const intersects = raycaster.intersectObjects(self.scene.children);
            if (intersects.length > 0) {
                for (let i = 0, l = intersects.length; i < l; ++i) {
                    // skip the grid
                    if (intersects[i].object === self.grid) continue;
                    // skip the axes
                    if (intersects[i].object === self.axes) continue;
                    // skip invisible objects
                    if (!intersects[i].object.visible) continue;

                    const lookAt = new THREE.Vector3();
                    lookAt.copy(self.look.direction);
                    lookAt.multiplyScalar(intersects[0].distance);
                    lookAt.add(self.look.origin);
                    if (TRACE_MESSAGES)
                        console.log(
                            "lookAt=[" +
                                lookAt.x +
                                ", " +
                                lookAt.y +
                                ", " +
                                lookAt.z +
                                "]"
                        );
                    return lookAt.toArray();
                }
            }
            // If nothing was hit return nothing
            return null;
        };

        // getModelExtents is called when the navlib requests the bounding box
        // of the model
        this.getModelExtents = function () {
            const boundingBox = this.model.geometry.boundingBox;
            return [
                boundingBox.min.x,
                boundingBox.min.y,
                boundingBox.min.z,
                boundingBox.max.x,
                boundingBox.max.y,
                boundingBox.max.z,
            ];
        };

        // getPerspective is called when the navlib needs to know the projection
        this.getPerspective = function () {
            return PERSPECTIVE;
        };

        // getPivotPosition is called when the navlib needs to know where the application's rotation pivot is located
        // in this example we return the center of the geometry's bounding box
        this.getPivotPosition = function () {
            const volumeCenter = new THREE.Vector3();
            volumeCenter.addVectors(
                this.model.geometry.boundingBox.min,
                this.model.geometry.boundingBox.max
            );
            volumeCenter.divideScalar(2);
            if (TRACE_MESSAGES)
                console.log(
                    "pivot=[" +
                        volumeCenter.x +
                        ", " +
                        volumeCenter.y +
                        ", " +
                        volumeCenter.z +
                        "]"
                );
            return volumeCenter.toArray();
        };

        // getPointerPosition is called when the navlib needs to know where the
        // mouse pointer is on the projection/near plane
        this.getPointerPosition = function () {
            const self = this;

            const canvas = document.getElementById("container");
            const rect = canvas.getBoundingClientRect();

            // position of the mouse in the canvas (windows [0,0] is at the top-left of the screen, opengl[0,0] is at the bottom-left)
            // the position is tracked relative to the window so we need to subtract the relative position of the canvas
            // Setting z=0 puts the mouse on the near plane.
            const pos_opengl = new THREE.Vector3(
                window.mouseX - rect.left,
                self.gl.viewportHeight - (window.mouseY - rect.top),
                0.0
            );
            if (TRACE_MESSAGES)
                console.log(
                    "Mouse Position =[" +
                        pos_opengl.x +
                        ", " +
                        pos_opengl.y +
                        ", " +
                        pos_opengl.z +
                        "]"
                );

            // three.js has screen coordinates that are in normalized device coordinates (-1,-1) bottom left and (1,1) top right.
            const pos = new THREE.Vector3(
                (pos_opengl.x / self.gl.viewportWidth) * 2.0 - 1.0,
                (pos_opengl.y / self.gl.viewportHeight) * 2.0 - 1.0,
                pos_opengl.z * 2.0 - 1
            );

            // make sure the matrices are up to date
            self.camera.updateProjectionMatrix();
            self.camera.updateMatrixWorld();

            pos.unproject(self.camera);
            if (TRACE_MESSAGES) {
                const screen = pos.clone();
                screen.project(self.camera);
                console.log(
                    "Screen Position =[" +
                        screen.x +
                        ", " +
                        screen.y +
                        ", " +
                        screen.z +
                        "]"
                );
                console.log(
                    "Pointer Position =[" +
                        pos.x +
                        ", " +
                        pos.y +
                        ", " +
                        pos.z +
                        "]"
                );
                if (!PERSPECTIVE) {
                    const worldMatrix = self.camera.matrixWorld.clone();
                    const bottomLeft = new THREE.Vector3(
                        self.camera.left,
                        self.camera.bottom,
                        -self.camera.far
                    );
                    bottomLeft.applyMatrix4(worldMatrix);
                    const topRight = new THREE.Vector3(
                        self.camera.right,
                        self.camera.top,
                        -self.camera.near
                    );
                    topRight.applyMatrix4(worldMatrix);
                    console.log(
                        "View world extents = [" +
                            bottomLeft.x +
                            ", " +
                            bottomLeft.y +
                            ", " +
                            bottomLeft.z +
                            "]" +
                            "[" +
                            topRight.x +
                            ", " +
                            topRight.y +
                            ", " +
                            topRight.z +
                            "]"
                    );
                }
            }
            return pos.toArray();
        };

        // this property returns whether the view can be rotated using the 3dmouse
        this.getViewRotatable = function () {
            return true;
        };

        // getViewExtents is called when the navlib requests the bounding box
        // of the view. This occurs in orthographic view projections
        this.getViewExtents = function () {
            return [
                this.camera.left,
                this.camera.bottom,
                -this.camera.far,
                this.camera.right,
                this.camera.top,
                -this.camera.near,
            ];
        };

        // getViewFrustum is called when the navlib requests the frustum of the view. This occurs in perspective view projections
        // three.js does not expose the frustum, so this needs to be calculated from the fov and the near plane.
        // Note the fov in three.js is the vertical fov.
        this.getViewFrustum = function () {
            const self = this;
            const tan_halffov = Math.tan((self.gl.fov * Math.PI) / 360);
            const bottom = -self.camera.near * tan_halffov;
            const left = bottom * self.camera.aspect;
            if (TRACE_MESSAGES)
                console.log(
                    "frustum=[" +
                        left +
                        ", " +
                        -left +
                        ", " +
                        bottom +
                        ", " +
                        -bottom +
                        ", " +
                        self.camera.near +
                        ", " +
                        self.camera.far +
                        "]"
                );
            return [
                left,
                -left,
                bottom,
                -bottom,
                self.camera.near,
                self.camera.far,
            ];
        };

        // getViewMatrix is called when the navlib requests the view matrix
        this.getViewMatrix = function () {
            // THREE.js matrices are column major (same as openGL)
            return this.camera.matrixWorld.toArray();
        };

        ///////////////////////////////////////////////////////////////////////////////////////////
        // Commands
        // this callback is called when a command, that was exported by setting the commands property,
        // is invoked by a button press on the 3dmouse
        this.setActiveCommand = function (id) {
            if (id) console.log("Id of command to execute= " + id);
        };

        ///////////////////////////////////////////////////////////////////////////////////////////
        // Hit test properties
        // these are used by getLookAt and set by the 3dconnexion navlib
        this.look = {
            origin: new THREE.Vector3(),
            direction: new THREE.Vector3(),
            aperture: 0.01,
            selection: false,
        };

        this.setLookFrom = function (data) {
            this.look.origin.set(data[0], data[1], data[2]);
        };

        this.setLookDirection = function (data) {
            this.look.direction.set(data[0], data[1], data[2]);
        };

        this.setLookAperture = function (data) {
            this.look.aperture = data;
        };

        this.setSelectionOnly = function (data) {
            this.look.selection = data;
        };

        ///////////////////////////////////////////////////////////////////////////////////////////
        // View properties
        // setViewExtents is called when the navlib needs to zoom the view
        // in an orthographic view projection
        this.setViewExtents = function (data) {
            const self = this;
            self.camera.left = data[0];
            self.camera.bottom = data[1];
            self.camera.right = data[3];
            self.camera.top = data[4];
            self.camera.updateProjectionMatrix();
        };

        // setViewMatrix is called when the navlib sets the view matrix
        this.setViewMatrix = function (data) {
            // Note data is a column major matrix
            const cameraMatrix = new THREE.Matrix4();

            cameraMatrix.fromArray(data);

            // update the camera
            cameraMatrix.decompose(
                this.camera.position,
                this.camera.quaternion,
                this.camera.scale
            );
            this.camera.updateMatrixWorld(true);
        };

        // setFov is called when the navlib sets the fov
        this.setFov = function (data) {
            this.gl.fov = (data * 180) / Math.PI;
        };

        // setTransaction is called twice per frame
        // transaction >0 at the beginning of a frame change
        // transaction ===0 at the end of a frame change
        this.setTransaction = function (transaction) {
            if (transaction === 0) {
                // request a redraw if not animating
                this.updateScene();
            }
        };

        // onStartMotion is called when the 3DMouse starts sending data
        this.onStartMotion = function () {
            const self = this;
            if (!self.animating) {
                self.animating = true;
                window.requestAnimationFrame(self.render.bind(self));
            }
        };

        // onStopMotion is called when the 3DMouse stops sending data
        this.onStopMotion = function () {
            this.animating = false;
        };

        ///////////////////////////////////////////////////////////////////////
        // the 3dconnexion 3DMouse initialization
        this.init3DMouse = function () {
            const self = this;
            self.spaceMouse = new _3Dconnexion(self);
            if (!self.spaceMouse.connect()) {
                if (TRACE_MESSAGES)
                    console.log("Cannot connect to 3Dconnexion NL-Proxy");
            }
        };

        this.onConnect = function () {
            const self = this;
            // name is display to the user and used to identify the application
            // for the 3D mouse property panels.
            const name = "WebThreeJS Sample";
            // we need to pass in a focusable object
            // we can use the <div /> if it has a tabindex
            const canvas = document.getElementById("container");

            self.spaceMouse.create3dmouse(canvas, name);
        };

        this.on3dmouseCreated = function () {
            const self = this;
            const actionTree = new _3Dconnexion.ActionTree();
            const actionImages = new _3Dconnexion.ImageCache();

            // set ourselves as the timing source for the animation frames
            self.spaceMouse.update3dcontroller({
                frame: { timingSource: 1 },
            });

            actionImages.onload = function () {
                self.spaceMouse.update3dcontroller({
                    images: actionImages.images,
                });
            };

            // An actionset can also be considered to be a buttonbank, a menubar, or a set of toolbars
            // Define a unique string for the action set to be able to specify the active action set
            // Because we only have one action set use the 'Default' action set id to not display the label
            const buttonBank = actionTree.push(
                new _3Dconnexion.ActionSet("Default", "Custom action set")
            );
            getApplicationCommands(buttonBank, actionImages);

            // Expose the commands to 3Dxware and specify the active buttonbank / action set
            self.spaceMouse.update3dcontroller({
                commands: { activeSet: "Default", tree: actionTree },
            });
        };

        this.onDisconnect = function (reason) {
            if (TRACE_MESSAGES)
                console.log("3Dconnexion NL-Proxy disconnected " + reason);
        };
    }

    function demoStart() {
        const container = document.getElementById("container");
        container.focus();
        const webGL = new GL();
        webGL.webGLStart();
    }

    // this function fills the action and images structures that are exposed
    // to the 3Dconnexion button configuration editor
    function getApplicationCommands(buttonBank, images) {
        // Add a couple of categories / menus / tabs to the buttonbank/menubar/toolbar
        // Use the categories to group actions so that the user can find them easily
        const fileNode = buttonBank.push(
            new _3Dconnexion.Category("CAT_ID_FILE", "File")
        );
        const editNode = buttonBank.push(
            new _3Dconnexion.Category("CAT_ID_EDIT", "Edit")
        );

        // Add menu items / actions
        fileNode.push(new _3Dconnexion.Action("ID_OPEN", "Open", "Open file"));
        fileNode.push(
            new _3Dconnexion.Action("ID_CLOSE", "Close", "Close file")
        );
        fileNode.push(
            new _3Dconnexion.Action("ID_EXIT", "Exit", "Exit program")
        );

        // Add menu items / actions
        editNode.push(
            new _3Dconnexion.Action("ID_UNDO", "Undo", "Shortcut is Ctrl + Z")
        );
        editNode.push(
            new _3Dconnexion.Action("ID_REDO", "Redo", "Shortcut is Ctrl + Y")
        );
        editNode.push(
            new _3Dconnexion.Action("ID_CUT", "Cut", "Shortcut is Ctrl + X")
        );
        editNode.push(
            new _3Dconnexion.Action("ID_COPY", "Copy", "Shortcut is Ctrl + C")
        );
        editNode.push(
            new _3Dconnexion.Action("ID_PASTE", "Paste", "Shortcut is Ctrl + V")
        );

        // Now add the images to the cache and associate it with the menu item by using the same id as the menu item / action
        // These images will be shown in the 3Dconnexion properties editor and in the UI elements which display the
        // active button configuration of the 3dmouse
        images.push(
            _3Dconnexion.ImageItem.fromURL("images/open.png", "ID_OPEN")
        );
        images.push(
            _3Dconnexion.ImageItem.fromURL("images/close.png", "ID_CLOSE")
        );
        images.push(
            _3Dconnexion.ImageItem.fromURL("images/exit.png", "ID_EXIT")
        );
        images.push(
            _3Dconnexion.ImageItem.fromURL("images/Macro_Cut.png", "ID_CUT")
        );
        images.push(
            _3Dconnexion.ImageItem.fromURL("images/Macro_Copy.png", "ID_COPY")
        );
        images.push(
            _3Dconnexion.ImageItem.fromURL("images/Macro_Paste.png", "ID_PASTE")
        );
        images.push(
            _3Dconnexion.ImageItem.fromURL("images/Macro_Undo.png", "ID_UNDO")
        );
        images.push(
            _3Dconnexion.ImageItem.fromURL("images/Macro_Redo.png", "ID_REDO")
        );
    }

    // this function tracks the mouse in the window so that we can
    // query the mouse position outside of an event. The mouse position
    // is cached in the global window
    document.addEventListener("mousemove", function (e) {
        window.mouseX = e.pageX;
        window.mouseY = e.pageY;
        if ("TRACE_MOUSE_MESSAGES" in window && TRACE_MOUSE_MESSAGES)
            console.log("mouse=[" + mouseX + ", " + +mouseY + "]");
    });

    demoStart();
});
