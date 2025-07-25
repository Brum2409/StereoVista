#ifndef _VERSION_H_INCLUDED
#define _VERSION_H_INCLUDED
// version.h
/*
 * Copyright (c) 2010-2018 3Dconnexion. All rights reserved.
 *
 * This file and source code are an integral part of the "3Dconnexion
 * Software Developer Kit", including all accompanying documentation,
 * and is protected by intellectual property laws. All use of the
 * 3Dconnexion Software Developer Kit is subject to the License
 * Agreement found in the "LicenseAgreementSDK.txt" file.
 * All rights not expressly granted by 3Dconnexion are reserved.
 */
#include <3DxWareVersionNumbers.h>
///////////////////////////////////////////////////////////////////////////////
// History:
//
// $Id: version.h 20917 2024-05-07 06:57:41Z mbonk $
//
///////////////////////////////////////////////////////////////////////////////

// Current version
#define VERSION_MAJOR 3
#define VERSION_MINOR 1
#define VERSION_MICRO _3DXWINCORE_VERSION_BUILD_NUMBER

#define FILE_VERSION                                                           \
  VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, _3DXWINCORE_SVN_WCREV
#define PRODUCT_VERSION                                                        \
  VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, _3DXWINCORE_SVN_WCREV

// Defines used in the 3dxcontrol.rc file for version control
#define COMPANY_NAME_STR "3Dconnexion"
#define FILE_DESCRIPTION_STR "3Dconnexion Viewer Navlib Demo"
#define FILE_VERSION_STR                                                       \
  MACROSTRINGIZER(                                                             \
      VERSION_MAJOR.VERSION_MINOR.VERSION_MICRO._3DXWINCORE_SVN_WCREV)
#define INTERNAL_NAME_STR "Navlib Viewer"
#define LEGAL_COPYRIGHT_STR IDR_LEGALCOPYRIGHT_ENUS
#define LEGAL_TRADEMARKS_STR "All registered trademarks acknowledged."
#define ORIGINAL_FILENAME_STR "navlib_viewer.exe"
#define PRODUCT_NAME_STR "3Dconnexion Viewer Navlib Demo"
#define PRODUCT_VERSION_STR                                                    \
  MACROSTRINGIZER(                                                             \
      VERSION_MAJOR.VERSION_MINOR.VERSION_MICRO._3DXWINCORE_SVN_WCREV)

#endif //_VERSION_H_INCLUDED
