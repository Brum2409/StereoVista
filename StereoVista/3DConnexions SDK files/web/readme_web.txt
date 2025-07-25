
3DconnexionJS
3Dconnexion JavaScript Framework

NOTICE:
All materials in this package are copyright 3Dconnexion unless stated otherwise.


1. REQUIREMENTS
---------------
The 3DconnexionJS supporting software (Navigation Library and Navigation Proxy executables) require an installation of 3DxWare 10.
3DxWare 10 for macOS  : version 10.6.1 or newer.
3DxWare 10 for Windows: version 10.8.12 or newer.


2. SOFTWARE INSTALLATION
------------------------
The "Navigation Library" is included in 3DxWare 10 and no additional steps are necessary.


3. USING THE SAMPLES
--------------------
Five samples are included is this package: "web_threejs.html", "web_threejs_require.html", "ortho_webglapp.html", "canvas2d-panzoomrotate.html" and "MultiCanvas.html".

The samples "web_threejs.html" and "web_threejs_require.html" are functionally similar with "web_threejs_require.html" demonstrating the use of Require JS (https://requirejs.org/) to handle dependencies. Comparing the "main.js" and the "web_threejs.html" files shows the demos are the same program, just with loading of dependencies structured differently.

The "MultiCanvas.html" sample demonstrates the use of 3DconnexionJS with multiple canvases.

The "ortho_webglapp.html" sample demonstrates the use of 3DconnexionJS in glMatrix-based program (http://glmatrix.net/).

The "canvas2d-panzoomrotate.html" sample is a demonstration of a conceptual "2D" viewer where only pan and zoom are implemented as is the case of "drawing" or "drafting" environment.

The samples included in this package require being loaded from a HTTP server. Loading them straight from the file system will cause issues with the application command export feature.

Nginx (http://nginx.org) is one web server program that can be used to serve the samples over the loopback adaptor.

The default configuration of nginx (file nginx.conf) can be modified as follows:
...
    server {
...
        location /samples {
            root   <path to the parent of the "samples" directory>;
            index  web_threejs.html;
        }
...

Copyright (c) 3Dconnexion. All rights reserved.