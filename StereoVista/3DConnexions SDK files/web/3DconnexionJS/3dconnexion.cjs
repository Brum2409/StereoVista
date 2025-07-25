/**
 * @license 3DconnexionJS
 *
 * Copyright (c) 2013-2024 3Dconnexion. All rights reserved.
 * License:
 *   This file is licensed under the terms of the "3Dconnexion
 *   Software Development Kit" license agreement:
 *   http://www.3dconnexion.com/service/software-developer/licence-agreement.html
 *   All rights not expressly granted by 3Dconnexion are reserved.
 *
 * $Revision: 21321 $
 */
/**
 * Protocol layer
 *
 * @author 3dconnexion / http://3dconnexion.com/
 * @author msb
 *
 * $Date: 2025-01-09 14:35:56 +0100 (Thu, 09 Jan 2025) $
 *
 * This implementation uses the Web Application Messaging Protocol (WAMP v1)
 * See http://www.wamp.ws/
 *
 * Dependencies:
 *
 *   autobahn.js
 *     Available from http://autobahn.ws/js
 *     Copyright 2011, 2012 Tavendo GmbH.
 *     Licensed under the MIT License.
 *     See license text at http://www.opensource.org/licenses/mit-license.php
 */
//////////////////////////////////////////////////////////////////////////////////////////
// History
// 12/06/22 SMB Added the ground plane and units properties.
// 07/08/20 MSB Removed the jQuery dependency.
// 10/24/18 MSB v0.6 Added support for client driven animation using the navlib properties
//              frame_timing_source_k and frame_time_k. To drive the animation the client
//              sets frame_timing_source_k = 1 and updates the frame_time_k with the time
//              passed into the method called back requestAnimationFrame.
// 01/30/17 MSB v0.5 breaking changes:
//              Changed names of mutators from 'put' to 'set' to match Java convention.
//              This version uses row vectors (column major order matrices) as the default.
//              Row major order can be specified by setting the rowMajorOrder option when
//              invoking create3dmouse();
//              Requires NLServer v1.3.0
// 10/19/17 MSB Added transaction and selection.affine properties
// 11/10/16 MSB Added support for action sets
//              Added on3dmouseCreated callback
// 09/14/15 MSB Added view.rotatable property
// 08/05/15 MSB Wrapped XHR error handler messages with the debug flag
// 07/31/15 MSB Set the asynchronous XHR timeout to 0 so that the network stack's timeout
//              value is used. Instead of the ontimeout event, onerror will now be fired.
// 06/26/15 MSB Chrome does not react to global.focus() so... follow Onshape's suggestion
//              to assume focus if the container being used is the global.
// 06/24/15 MSB If the focus is set to the document body set the focus to the
//              container in an attempt to fix the initial focus issue.
// 06/18/15 MSB Reverted back to blur and focus events
// 06/11/15 MSB Removed all jshint warnings and jslint errors
// 05/28/15 MSB Added views.front and coordinateSystem properties
// 05/13/15 MSB Fix: Use the bubbling jQuery focusin / focusout events to determine
//              3D Mouse focus. Try to determine if the client has focus using
//              the documents activeElement.
// 05/04/15 MSB Fix: During initialization only set focus property when the container
//              has focus and NOT when the document has focus.
// 04/27/15 MSB Renamed getFrustum to getViewFrustum, getTarget to getViewTarget
// 04/23/15 MSB Added getPivotPosition
// 04/22/15 MSB Use https query to get the port to use for the websocket connection.
// 04/17/15 MSB Added https query and only attempt a websocket connection if it
//              succeeds.
//              Added _3DCONNEXION_DEBUG flag. To enable the console messages
//              define the global variable _3DCONNEXION_DEBUG = true; before
//              including this source. To enable the autobahn messages define
//              the global variable AUTOBAHN_DEBUG = true; before calling
//              connect.
// 12/11/14 MSB Added a function call map
// 05/14/14 MSB Changed windowId property to the global object itself
// 03/27/14 MSB Always set the motion flag capability
// 03/26/14 MSB Added rudimentary button handling - The 3dconnexion Fit command
//              results in the clients fit() function being executed.
//

/*jshint unused: vars*/
/*jslint browser: true, devel: true, indent: 2, nomen: true, unparam: true*/

const global = globalThis;

const _3Dconnexion = function (client) {
    "use strict";
    this.version = "0.8.0"; // the _3Dconnexion client library version

    //////////////////////////////////////////////////////////////////////////////////////////
    // Connection callbacks
    this.client = client;

    //////////////////////////////////////////////////////////////////////////////////////////
    // Locals
    this.session = null; // the wamp session
    this.defport = 8181; // the http port
    this.connected = false; // websocket connexion status
    this.connexion = null; // the connexion id to the 3dmouse
    this._3dcontroller = null; // the instance of the 3dview controller
    this.viewport = null;
    this.host = "127.51.68.120";
    this.nlRpcUri = null;
    this.nlResourceUri = null;

    // constants
    this._EVENT_TYPEID_RPC = 0;
    this.V3DK_FIT = 0x1f;
    this.V3DK_MENU = 0x1e;

    // property function maps
    // These structures define the property and the method that is called to read and
    // update the value of the property
    this.fnUpdate = {
        motion: this.onMotion.bind(this),
        "events.keyPress": this.onKeyPress.bind(this),
        "events.keyRelease": this.onKeyRelease.bind(this),
    };

    ///////////////////////////////////////////////////////////////////////////////////
    // the client property maps
    this.clientFnRead = {
        "view.affine": client.getViewMatrix,
        "view.constructionPlane": client.getConstructionPlane,
        "view.extents": client.getViewExtents,
        "view.fov": client.getFov,
        "view.frustum": client.getViewFrustum,
        "view.perspective": client.getPerspective,
        "view.target": client.getViewTarget,
        "view.rotatable": client.getViewRotatable,
        // Model
        "model.extents": client.getModelExtents,
        "model.floorPlane": client.getFloorPlane,
        "model.unitsToMeters": client.getUnitsToMeters,
        // Pivot
        "pivot.position": client.getPivotPosition,
        // Hit testing
        "hit.lookat": client.getLookAt,
        // Selection
        "selection.affine": client.getSelectionAffine,
        "selection.empty": client.getSelectionEmpty,
        "selection.extents": client.getSelectionExtents,
        // Cursor
        "pointer.position": client.getPointerPosition,
        // world
        coordinateSystem: client.getCoordinateSystem,
        // predefined views
        "views.front": client.getFrontView,
        // frame
        "frame.timingSource": client.getFrameTimingSource,
        "frame.time": client.getFrameTime,
    };

    this.clientFnUpdate = {
        motion: client.setMoving,
        transaction: client.setTransaction,
        // View / Camera
        "view.affine": client.setViewMatrix,
        "view.extents": client.setViewExtents,
        "view.fov": client.setFov,
        "view.target": client.setTarget,
        // Commands
        "commands.activeCommand": client.setActiveCommand,
        // Pivot
        "pivot.position": client.setPivotPosition,
        "pivot.visible": client.setPivotVisible,
        // Hit testing
        "hit.lookfrom": client.setLookFrom,
        "hit.direction": client.setLookDirection,
        "hit.aperture": client.setLookAperture,
        "hit.selectionOnly": client.setSelectionOnly,
        // Selection
        "selection.affine": client.setSelectionAffine,
        // Keys
        "events.keyPress": client.setKeyPress,
        "events.keyRelease": client.setKeyRelease,
        // Settings
        "settings.changed": client.setSettingsChanged,
    };

    this.debug = false;
    if (global.hasOwnProperty("_3DCONNEXION_DEBUG")) {
        this.debug = global._3DCONNEXION_DEBUG;
    }

    // Workaround: removeEventListener requires the same instance as addEventListener.
    this.blur = this.blur.bind(this);
    this.focus = this.focus.bind(this);
};

// CommonJs
if (typeof module === "object") {
    module.exports = _3Dconnexion;
}

_3Dconnexion.prototype = {
    constructor: _3Dconnexion,
};

_3Dconnexion.nlOptions = {
    none: 0,
    rowMajorOrder: 2,
};

Object.freeze(_3Dconnexion.nlOptions);

//////////////////////////////////////////////////////////////////////////////////////////
// Connect to the 3dconnexion NL-Proxy
_3Dconnexion.prototype.connect = function () {
    "use strict";
    const self = this,
        client = this.client,
        xmlhttp = new XMLHttpRequest();
    let ret = 1,
        port,
        host,
        url;

    if (!client.onConnect) {
        throw new Error("onConnect handler required!");
    }

    //  The url for XHR
    url = "https://" + self.host + ":" + self.defport + "/3dconnexion/nlproxy";

    xmlhttp.onreadystatechange = function () {
        if (xmlhttp.readyState === 4 && xmlhttp.status === 200) {
            if (self.debug) {
                console.log(xmlhttp.responseText);
            }

            self.nlRpcUri = "wss://" + self.host + "/3dconnexion#";
            self.nlResourceUri = "wss://" + self.host + "/3dconnexion";

            try {
                port = JSON.parse(xmlhttp.responseText).port;
                host = "wss://" + self.host + ":" + port;

                if (self.debug) {
                    console.log("Connecting to " + host + " ...");
                }

                // if AUTOBAHN_DEBUG has been defined as true enable debug
                if (
                    global.hasOwnProperty("AUTOBAHN_DEBUG") &&
                    global.AUTOBAHN_DEBUG
                ) {
                    global.ab.debug(true, true, true);
                }
                // connect to the server
                global.ab.connect(
                    // websocket uri
                    host,

                    // session established onconnect handler
                    function (session) {
                        self.session = session;
                        self.connected = true;

                        if (self.debug) {
                            console.log("Connected!");
                        }

                        // establish the prefix for use in CURIEs in this session
                        session.prefix("3dx_rpc", self.nlRpcUri); // prefix for the rpcs
                        session.prefix("3dconnexion", self.nlResourceUri); // prefix for the resources
                        session.prefix("self", global.location.href); // prefix our own self

                        // call the onConnect callback
                        self.client.onConnect();
                    },

                    // session has gone onhangup handler
                    function (code, reason) {

                        if (code == window.ab.CONNECTION_UNREACHABLE_SCHEDULED_RECONNECT || code == window.ab.CONNECTION_LOST_SCHEDULED_RECONNECT) {
                            if (self.debug) {
                                console.log(reason);
                            }
                            return;
                        }

                        // the socket was closed (this could be an error or simply that there is no server)
                        self.connected = false;
                        self.session = null;

                        self.delete3dmouse();

                        // call the callback if one is supplied
                        if (self.client.onDisconnect !== undefined) {
                            self.client.onDisconnect(reason);
                        }

                        if (self.debug) {
                            console.log("Socket closed!", reason);
                        }
                    },

                    // The session options
                    {
                        maxRetries: 3,
                        retryDelay: 500,
                    }
                );
            } catch (err) {
                console.error(err);
            }
        }
    };

    xmlhttp.onerror = function () {
        if (self.debug) {
            console.log(
                "_3Dconnexion.connect: No response from local 3dmouse server " +
                url
            );
        }
    };

    xmlhttp.ontimeout = function () {
        if (self.debug) {
            console.log(
                "_3Dconnexion.connect: Timeout querying local 3dmouse server " +
                url
            );
        }
    };

    try {
        xmlhttp.open("GET", url, true);
        xmlhttp.setRequestHeader("Accept", "application/json; charset=utf-8");
        xmlhttp.msCaching = "disabled";
        xmlhttp.timeout = 0;

        xmlhttp.send();
    } catch (err) {
        ret = 0;
        console.error(err.toString());
    }

    return ret;
};

// Create the controller instance for this 3dview
_3Dconnexion.prototype.create3dmouse = function (view, appname, options) {
    "use strict";

    options =
        typeof options !== "undefined" ? options : _3Dconnexion.nlOptions.none;

    // appname is the name of the application for the smartui
    // global
    const self = this;
    self.viewport = view;

    if (self.debug) {
        console.log("create3dmouse() viewport.id is " + self.viewport.id);
    }

    // Set up the on focus and blur callbacks
    // We need to tell the library when we are in focus
    self.viewport.addEventListener("focus", self.focus);
    self.viewport.addEventListener("blur", self.blur);

    //////////////////////////////////////////////////////////////////////////////////////
    // create the connexion
    self.session
        .call("3dx_rpc:create", "3dconnexion:3dmouse", self.version)
        .then(
            function (result) {
                // save the 3dconnexion instance
                self.connexion = result.connexion;

                // tell the navlib about ourself
                const info = {
                    version: parseFloat(self.version),
                    name: appname,
                    rowMajorOrder:
                        (options & _3Dconnexion.nlOptions.rowMajorOrder) !== 0,
                };

                // create the 3dcontroller
                self.session
                    .call(
                        "3dx_rpc:create",
                        "3dconnexion:3dcontroller",
                        self.connexion,
                        info
                    )
                    .then(
                        function (result) {
                            // save the 3d controller instance
                            self._3dcontroller = result.instance;

                            self.session.subscribe(
                                "3dconnexion:3dcontroller/" +
                                self._3dcontroller,
                                self.onEvent.bind(self)
                            );
                            // if the global/canvas/container has focus tell the server about it
                            if (document.hasFocus()) {
                                try {
                                    // if nothing has focus, set the focus to the container
                                    if (
                                        document.activeElement ===
                                        document.body ||
                                        document.activeElement === null
                                    ) {
                                        self.viewport.focus();
                                        // the above may not have worked, for example in chrome so for now assume that the window has focus
                                        if (window === self.viewport) {
                                            // Another idea might be to simply call blur - then we know it hasn't got focus.
                                            self.focus();
                                        }
                                    } else if (typeof self.viewport.contains == "function" &&
                                        self.viewport.contains(
                                            document.activeElement
                                        )
                                    ) {
                                        // if the container or one of its children already has focus ...
                                        self.focus();
                                        if (self.debug) {
                                            console.log(
                                                "self.viewport has focus"
                                            );
                                        }
                                    }
                                    else if (window === self.viewport) {
                                        // Another idea might be to simply call blur - then we know it hasn't got focus.
                                        self.focus();
                                        if (self.debug) {
                                            console.log(
                                                "window has focus"
                                            );
                                        }
                                    }
                                } catch (err) {
                                    console.error(err);
                                }
                            }

                            if (self.client.on3dmouseCreated)
                                self.client.on3dmouseCreated();
                        },
                        function (error) {
                            console.log(
                                "3dx_rpc:create 3dconnexion:3dcontroller " +
                                error
                            );
                        }
                    );
            },
            function (error) {
                // handle the error
                console.log("3dx_rpc:create " + error);
            }
        );
};

////////////////////////////////////////////////////////////////////////////////////////////
// blur event handler
_3Dconnexion.prototype.blur = function () {
    "use strict";
    const self = this;
    if (self.debug) {
        console.log("blur on ");
    }
    if (self.session && self._3dcontroller) {
        self.update3dcontroller({ focus: false });
    }
};

////////////////////////////////////////////////////////////////////////////////////////////
// focus event handler
_3Dconnexion.prototype.focus = function () {
    "use strict";
    const self = this;
    if (self.debug) {
        console.log("focus on ");
    }
    if (self.session && self._3dcontroller) {
        self.update3dcontroller({ focus: true });
    }
};

////////////////////////////////////////////////////////////////////////////////////////////
//
_3Dconnexion.prototype.onKeyPress = function (key) {
    "use strict";
    const self = this;
    if (self.debug) {
        console.log("onKeyPress " + key);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////
//
_3Dconnexion.prototype.onKeyRelease = function (key) {
    "use strict";
    const self = this;
    if (self.debug) {
        console.log("onKeyRelease " + key);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////
//
_3Dconnexion.prototype.onMotion = function (motion) {
    "use strict";
    const self = this;

    if (motion === true) {
        if (self.client.onStartMotion !== undefined) {
            self.client.onStartMotion();
        }
    }
    else if (self.client.onStopMotion !== undefined) {
        self.client.onStopMotion();
    }
};

////////////////////////////////////////////////////////////////////////////////////////////
//
_3Dconnexion.prototype.onEvent = function (topicUri, event) {
    "use strict";
    const self = this;
    let result = null,
        fn = null,
        obj = null;

    if (self.debug) {
        console.log(topicUri);
        console.log(event);
    }
    // [ TYPE_ID_CALL , callID , procURI , ... ]
    if (event[0] === global.ab._MESSAGE_TYPEID_CALL) {
        // Read
        if (event[2] === "self:read") {
            fn = self.clientFnRead[event[4]];
            if (fn !== undefined) {
                result = fn.bind(self.client)();
            } else {
                obj = [
                    global.ab._MESSAGE_TYPEID_CALL_ERROR,
                    event[1],
                    event[2] + "#generic",
                    event[4] + " unknown property",
                ];
                self.session._send(obj);
                return;
            }
        } else if (event[2] === "self:update") {
            fn = self.fnUpdate[event[4]];
            if (fn !== undefined) {
                result = fn(event[5]);
            } else {
                fn = self.clientFnUpdate[event[4]];
                if (fn !== undefined) {
                    result = fn.bind(self.client)(event[5]);
                } else {
                    obj = [
                        global.ab._MESSAGE_TYPEID_CALL_ERROR,
                        event[1],
                        event[2] + "#generic",
                        event[4] + " unknown property",
                    ];
                    self.session._send(obj);
                    return;
                }
            }
        } else {
            // Unknown procedure
            obj = [
                global.ab._MESSAGE_TYPEID_CALL_ERROR,
                event[1],
                event[2] + "#generic",
                "unknown procedure",
            ];
            self.session._send(obj);
            return;
        }

        obj = [global.ab._MESSAGE_TYPEID_CALL_RESULT, event[1], result];
        self.session._send(obj);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////
_3Dconnexion.prototype.read3dcontroller = function (str, onRead) {
    "use strict";
    const self = this;
    if (self.debug) {
        console.log("read3dcontroller " + self._3dcontroller);
    }
    try {
        self.session
            .call(
                "3dx_rpc:read",
                "3dconnexion:3dcontroller/" + self._3dcontroller,
                str
            )
            .then(
                function (result) {
                    if (onRead) {
                        onRead(result);
                    }
                },
                function (error) {
                    console.log(
                        "3dx_rpc:read " +
                        "3dconnexion:3dcontroller/" +
                        self._3dcontroller +
                        " " +
                        str +
                        " " +
                        error
                    );
                }
            );
    } catch (err) {
        console.error(err);
    }
};

_3Dconnexion.prototype.update3dcontroller = function (value) {
    "use strict";
    const self = this;
    if (self.debug) {
        console.log("update3dmouse " + self._3dcontroller);
    }

    try {
        if (!self._3dcontroller)
            throw new Error("exception 3dx_rpc:update: 3dcontroller not initialized");

        self.session
            .call(
                "3dx_rpc:update",
                "3dconnexion:3dcontroller/" + self._3dcontroller,
                value
            )
            .then(
                function (result) {
                    // Do nothing
                    return;
                },
                function (error) {
                    console.log(
                        "3dx_rpc:update 3dconnexion:3dcontroller/" +
                        self._3dcontroller +
                        " " +
                        error
                    );
                }
            );
    } catch (err) {
        console.error(err);
    }
};

_3Dconnexion.prototype.delete3dmouse = function () {
    "use strict";
    const self = this,
        connexion = self.connexion;
    if (self.debug) {
        console.log("delete3dmouse ");
    }
    self.connexion = null;
    self._3dcontroller = null;

    if (self.viewport !== null) {
        // Remove the on focus and blur callbacks
        self.viewport.removeEventListener("focus", self.focus);
        self.viewport.removeEventListener("blur", self.blur);
    }

    if (self.session) {
        self.session
            .call("3dx_rpc:delete", "3dconnexion:3dmouse/" + connexion)
            .then(
                function (result) {
                    if (self.debug) {
                        console.log("deleted connexion " + connexion);
                    }
                    self.close();
                },
                function (error) {
                    console.log("3dx_rpc:delete " + connexion + " " + error);
                    self.close();
                }
            );
    }
};

_3Dconnexion.prototype.close = function () {
    "use strict";
    const self = this;
    self.connected = false;
    const session = self.session;

    if (session) {
        session.close();
    }
};

