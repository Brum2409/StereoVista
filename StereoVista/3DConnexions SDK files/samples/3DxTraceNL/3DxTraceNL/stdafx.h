#ifndef STDAFX_H_INCLUDED_
#define STDAFX_H_INCLUDED_

// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

// reference additional headers your program requires here
#define _CRT_SECURE_NO_WARNINGS 1

// #define WIN32_LEAN_AND_MEAN to exclude APIs such as Cryptography, DDE, RPC, Shell, and 
// Windows Sockets. The define is required to fix "fatal error C1189: #error:  WinSock.h 
// has already been included" when using asio.
#define WIN32_LEAN_AND_MEAN

// get rid of those awful min max macros.
#define NOMINMAX 1
#include <Windows.h>
#endif // STDAFX_H_INCLUDED_
