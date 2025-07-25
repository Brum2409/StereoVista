/**
*
* Copyright (c) 2017 3Dconnexion. All rights reserved.
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
* Dependencies:
*
*/
///////////////////////////////////////////////////////////////////////////
// History
//
// 02/08/17 MSB Fix: ImageItem.fromURL: Handling of item.status incorrect
//              Fix: ImageItem.fromURL: If XMLHttpRequest does not return
//              status===200 do NOT set the data to the response.
//              Fix: ImageCache: Remove the onload() function of the item in
//              the onload() function to prevent it being invoked again
//              Fix: ImageCache: Remove items with status != 200
// 10/27/16 MSB Initial
//
/*jshint unused: vars*/
/*jslint browser: true, devel: true, indent: 2, nomen: true, unparam: true*/
'use strict';

// File: imageitem.js
_3Dconnexion.ImageItem = function () {
};

_3Dconnexion.SiImageType_t = {
    e_none: 0
    , e_image_file: 1
    , e_resource_file: 2
    , e_image: 3
};

Object.freeze(_3Dconnexion.SiImageType_t);

_3Dconnexion.ImageItem.prototype = {
    constructor: _3Dconnexion.ImageItem,

    id: { value: '', writable: true },
    type: { value: _3Dconnexion.SiImageType_t.e_none || '', enumerable: true }
};

_3Dconnexion.ImageItem.fromImage = function (arrayBuffer, id) {
    const item = new _3Dconnexion.ImageItem();
    return Object.defineProperties(item, {
        'id': { value: id || '', writable: true, enumerable: true },
        'type': { value: _3Dconnexion.SiImageType_t.e_image, enumerable: true },
        'index': { value: 0, writable: true, enumerable: true },
        'data': { value: _3Dconnexion.ImageItem.base64FromArrayBuffer(arrayBuffer), enumerable: true },
        'status': { value: 200 }
    });
};

_3Dconnexion.ImageItem.fromURL = function (URL, id) {
    const item = new _3Dconnexion.ImageItem();
    Object.defineProperties(item, {
        'id': { value: id || '', writable: true, enumerable: true },
        'type': { value: _3Dconnexion.SiImageType_t.e_image, enumerable: true },
        'index': { value: 0, writable: true, enumerable: true },
        'buffer': { value: null, writable: true },
        'data': {
            get: function () {
                return this.buffer;
            },
            set: function (data) {
                item.buffer = data;
                item.onload();
            },
            enumerable: true
        },
        'status': { value: 100, writable: true },
        'onload': { value: function () { }, writable: true }
    });

    const req = new XMLHttpRequest();
    req.overrideMimeType('text/plain; charset=x-user-defined');
    req.open('GET', URL, true);
    req.responseType = 'arraybuffer';
    req.onload = function () {
        item.status = req.status;
        if (req.status === 200 && req.response !== null) {
            item.data = _3Dconnexion.ImageItem.base64FromArrayBuffer(req.response);
            return;
        }
        else
            item.status = 404;
        item.data = null;
    };
    req.onerror = function () {
        item.status = req.status;
        item.data = null;
    };
    req.send(null);
    return item;
};

_3Dconnexion.ImageItem.base64FromArrayBuffer = function (arraybuffer) {
    const codeChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/="
        , bytes = new Uint8Array(arraybuffer), rest = bytes.length % 3, count = bytes.length - rest;
    let i, base64 = "";

    for (i = 0; i < count; i += 3) {
        base64 += codeChars[bytes[i] >> 2];
        base64 += codeChars[(bytes[i] & 0x03) << 4 | bytes[i + 1] >> 4];
        base64 += codeChars[(bytes[i + 1] & 0x0f) << 2 | bytes[i + 2] >> 6];
        base64 += codeChars[bytes[i + 2] & 0x3f];
    }

    if (rest !== 0) {
        base64 += codeChars[bytes[count] >> 2];
        if (rest === 2) {
            base64 += codeChars[(bytes[count] & 0x03) << 4 | bytes[count + 1] >> 4];
            base64 += codeChars[(bytes[count + 1] & 0x0f) << 2];
            base64 += "=";
        }
        else {
            base64 += codeChars[(bytes[count] & 0x03) << 4];
            base64 += "==";
        }
    }

    return base64;
};

_3Dconnexion.ImageCache = function () {
    this.images = [];
    this.outstanding_requests = 0;
};

_3Dconnexion.ImageCache.prototype = {
    constructor: _3Dconnexion.ImageCache

    , images: { value: null, writable: true, enumerable: true }

    , outstanding_requests: { value: 0, writable: true }

    , push: function (item) {
        const self = this;
        if (item.status === 100) {
            ++self.outstanding_requests;

            item.onload = function () {
                const item = this;
                item.onload = function () { };
                if (item.status !== 200) {
                    const index = self.images.indexOf(item);
                    if (index > -1) {
                        self.images.splice(index, 1);
                    }
                }
                --self.outstanding_requests;
                if (self.outstanding_requests == 0)
                    self.onload();
            }.bind(item);
        }
        else if (item.data === null)
            return;

        self.images.push(item);
    }

    , onload: function () {
    }
};
