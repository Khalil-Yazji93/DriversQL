#pragma once
// -------------------------------------------------------------------------
// 
// PRODUCT:            DMA Driver
// MODULE NAME:        Public.h
// 
// MODULE DESCRIPTION: 
// 
// Contains the public defines for the project.
// 
// $Revision:  $
//
// ------------------------- CONFIDENTIAL ----------------------------------
// 
//              Copyright (c) 2017 by Northwest Logic, Inc.   
//                       All rights reserved. 
// 
// Trade Secret of Northwest Logic, Inc.  Do not disclose. 
// 
// Use of this source code in any form or means is permitted only 
// with a valid, written license agreement with Northwest Logic, Inc. 
// 
// Licensee shall keep all information contained herein confidential  
// and shall protect same in whole or in part from disclosure and  
// dissemination to all third parties. 
// 
// 
//                        Northwest Logic, Inc. 
//                  1100 NW Compton Drive, Suite 100 
//                      Beaverton, OR 97006, USA 
//   
//                        Ph:  +1 503 533 5800 
//                        Fax: +1 503 533 5900 
//                      E-Mail: info@nwlogic.com 
//                           www.nwlogic.com 
// 
// -------------------------------------------------------------------------

#ifndef PUBLIC_H_
#define PUBLIC_H_
#include "Include/DMADriverIoctl.h"

DEFINE_GUID(GUID_ULTRASONICS_DRIVER_INTERFACE,
	0x2fd18809, 0x8bb3, 0x452a, 0x97, 0x16, 0xe9, 0x55, 0x36, 0xe8, 0x52, 0x6e);

//DEFINE_GUID(GUID_ULTRASONICS_DRIVER_INTERFACE,
//	0x78a1c341, 0x4539, 0x11d3, 0xb8, 0x8d, 0x00, 0xc0, 0x4f, 0xad, 0x51, 0x71);


#endif                          /* PUBLIC_H_ */
