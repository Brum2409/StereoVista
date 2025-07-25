#ifndef version_H_INCLUDED_
#define version_H_INCLUDED_
// <copyright file="version.h" company="3Dconnexion">
// ------------------------------------------------------------------------------------------------
// Copyright (c) 2013-2021 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer Kit",
// including all accompanying documentation, and is protected by intellectual property laws. All
// use of the 3Dconnexion Software Developer Kit is subject to the License Agreement found in the
// "LicenseAgreementSDK.txt" file. All rights not expressly granted by 3Dconnexion are reserved.
// ------------------------------------------------------------------------------------------------
// </copyright>
// <history>
// ************************************************************************************************
// File History
//
// $Id: version.h 17426 2020-05-07 06:56:31Z mbonk $
// </history>


// Current version
#define VERSION_MAJOR 0
#define VERSION_MINOR 6
#define VERSION_MICRO 0

#define STRINGIZER(arg) #arg
#define MACROSTRINGIZER(arg) STRINGIZER(arg)

#define FILE_VERSION VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO

#define COMPANY_NAME_STR "3Dconnexion"
#define FILE_DESCRIPTION_STR "3DxNlClient"
#define FILE_VERSION_STR MACROSTRINGIZER(VERSION_MAJOR.VERSION_MINOR.VERSION_MICRO)
#define INTERNAL_NAME_STR "3DxNlClient"
#define LEGAL_COPYRIGHT_STR "Copyright 2013-2021 3Dconnexion. All rights reserved."
#define LEGAL_TRADEMARKS_STR "All registered trademarks acknowledged."
#define ORIGINAL_FILENAME_STR "3DxNlClient"
#define PRODUCT_NAME_STR "3Dconnexion Navigation Client"
#define PRODUCT_VERSION_STR MACROSTRINGIZER(VERSION_MAJOR.VERSION_MINOR.VERSION_MICRO)

#endif // version_H_INCLUDED_
