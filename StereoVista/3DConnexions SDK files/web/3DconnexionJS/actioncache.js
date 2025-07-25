/**
*
* Copyright (c) 2016 3Dconnexion. All rights reserved.
* License:
*   This file is licensed under the terms of the "3Dconnexion
*   Software Development Kit" license agreement:
*   http://www.3dconnexion.com/service/software-developer/licence-agreement.html
*   All rights not expressly granted by 3Dconnexion are reserved.
*
* $Revision: 21321 $
*/
/**
* Support for commands
*
* @author 3dconnexion / http://3dconnexion.com/
* @author msb
*
* $Date: 2025-01-09 14:35:56 +0100 (Thu, 09 Jan 2025) $
*
* Dependencies:
*   imageitem.js
*
*/
///////////////////////////////////////////////////////////////////////////
// History
//
// 10/27/16 MSB Initial
//
/*jshint unused: vars*/
/*jslint browser: true, devel: true, indent: 2, nomen: true, unparam: true*/

// File: actioncache.js
'use strict';

_3Dconnexion.SiActionNodeType_t = {
    SI_ACTIONSET_NODE: 0
    , SI_CATEGORY_NODE: 1
    , SI_ACTION_NODE: 2
};

Object.freeze(_3Dconnexion.SiActionNodeType_t);

_3Dconnexion.ActionNode = function (id, label, type) {
    this.id = id;
    this.label = label || id;
    this.type = type;
    return this;
};

_3Dconnexion.ActionNode.prototype = {
    constructor: _3Dconnexion.ActionNode
    , id: { value: null, enumerable: true }
    , label: { value: null, writable: true, enumerable: true }
    , type: { value: null, enumerable: true }
};

// _3Dconnexion.Action constructor
// inherits from _3Dconnexion.ActionNode
// adds a description field
_3Dconnexion.Action = function (id, label, description) {
    _3Dconnexion.ActionNode.call(this, id, label, _3Dconnexion.SiActionNodeType_t.SI_ACTION_NODE);
    this.description = description || '';
    return this;
};

_3Dconnexion.Action.prototype = Object.create(_3Dconnexion.ActionNode.prototype, {
    constructor: { value: _3Dconnexion.Action }

    , description: { value: '', writable: true, enumerable: true }
});

// _3Dconnexion.ActionTreeNode constructor
// the store for the child nodes
_3Dconnexion.ActionTreeNode = function () {
    // the child nodes
    this.nodes = [];
    return this;
};

_3Dconnexion.ActionTreeNode.prototype = Object.create(_3Dconnexion.ActionNode.prototype, {
    constructor: { value: _3Dconnexion.ActionTreeNode },

    nodes: { value: null, writable: true, enumerable: true },

    push: {
        value: function (node) {
            this.nodes.push(node);
            return node;
        }
    }
});

// _3Dconnexion.ActionSet constructor
// inherits from _3Dconnexion.ActionNode and _3Dconnexion.ActionTreeNode
_3Dconnexion.ActionSet = function (id, label) {
    _3Dconnexion.ActionNode.call(this, id, label, _3Dconnexion.SiActionNodeType_t.SI_ACTIONSET_NODE);
    _3Dconnexion.ActionTreeNode.call(this);

    return this;
};

_3Dconnexion.ActionSet.prototype = Object.create(_3Dconnexion.ActionTreeNode.prototype, {
    constructor: { value: _3Dconnexion.ActionSet }
});

// _3Dconnexion.Category constructor
// inherits from _3Dconnexion.ActionNode and _3Dconnexion.ActionTreeNode
_3Dconnexion.Category = function (id, label) {
    _3Dconnexion.ActionNode.call(this, id, label, _3Dconnexion.SiActionNodeType_t.SI_CATEGORY_NODE);
    _3Dconnexion.ActionTreeNode.call(this);

    return this;
};

_3Dconnexion.Category.prototype = Object.create(_3Dconnexion.ActionTreeNode.prototype, {
    constructor: { value: _3Dconnexion.Category }
});

// sugar
_3Dconnexion.ActionTree = function () {
    this.nodes = [];
    return this;
};

_3Dconnexion.ActionTree.prototype = Object.create(null, {
    constructor: { value: _3Dconnexion.ActionTree },

    nodes: { value: null, writable: true, enumerable: true },

    push: {
        value: function (node) {
            this.nodes.push(node);
            return node;
        }
    }
});
