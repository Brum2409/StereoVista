#ifndef Stdafx_H_INCLUDED_
#define Stdafx_H_INCLUDED_
// stdafx.h : include file for standard system include files
/*
 * Copyright (c) 2010-2020 3Dconnexion. All rights reserved.
 *
 * This file and source code are an integral part of the "3Dconnexion
 * Software Developer Kit", including all accompanying documentation,
 * and is protected by intellectual property laws. All use of the
 * 3Dconnexion Software Developer Kit is subject to the License
 * Agreement found in the "LicenseAgreementSDK.txt" file.
 * All rights not expressly granted by 3Dconnexion are reserved.
 */

///////////////////////////////////////////////////////////////////////////////////
// History
//
// $Id: stdafx.h 18024 2021-01-18 08:44:12Z mbonk $
//

// #define WIN32_LEAN_AND_MEAN to exclude APIs such as Cryptography, DDE, RPC, Shell, and
// Windows Sockets. The define is required to fix "fatal error C1189: #error:  WinSock.h
// has already been included" when using asio.
#define WIN32_LEAN_AND_MEAN

// get rid of those awful min max macros.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
using std::max;
using std::min;

#define VC_EXTRALEAN
#define WINVER 0x501

/// <summary>
/// The application has an animation loop implemented which is used to draw the scene when the scene
///  or objects are moved.
/// </summary>
/// <remarks>
/// When set to 1 the application is responsible for supplying the frame timing to the navlib. When
/// set to 0, the navlib will try to use the monitor refresh rate as the frame timing source.
/// </remarks>
#define APPLICATION_HAS_ANIMATION_LOOP 1

/// <summary>
/// Controls whether the application renders the scene in an external thread or in the GUI thread.
/// </summary>
#define APPLICATION_HAS_EXTRA_RENDER_THREAD 1


#include <afxext.h> // MFC extensions
#include <afxwin.h> // MFC core and standard components

#pragma warning(                                                               \
    disable : 4503) // decorated name length exceeded, name was truncated
#pragma warning(                                                               \
    disable : 4996) // Function call with parameters that may be unsafe

#ifdef UNICODE
#define CT2U8(x) (ATL::CW2A(x, 65001))
#define CU8T(x) (ATL::CA2W(x, 65001))
#else
#define CT2U8(x) x
#define CU8T(x) x
#endif

#endif // Stdafx_H_INCLUDED_