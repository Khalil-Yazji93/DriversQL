#ifndef __PLX_IOCTL_H
#define __PLX_IOCTL_H

/*******************************************************************************
 * Copyright (c) PLX Technology, Inc.
 *
 * PLX Technology Inc. licenses this source file under the GNU Lesser General Public
 * License (LGPL) version 2.  This source file may be modified or redistributed
 * under the terms of the LGPL and without express permission from PLX Technology.
 *
 * PLX Technology, Inc. provides this software AS IS, WITHOUT ANY WARRANTY,
 * EXPRESS OR IMPLIED, INCLUDING, WITHOUT LIMITATION, ANY WARRANTY OF
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  PLX makes no guarantee
 * or representations regarding the use of, or the results of the use of,
 * the software and documentation in terms of correctness, accuracy,
 * reliability, currentness, or otherwise; and you rely on the software,
 * documentation and results solely at your own risk.
 *
 * IN NO EVENT SHALL PLX BE LIABLE FOR ANY LOSS OF USE, LOSS OF BUSINESS,
 * LOSS OF PROFITS, INDIRECT, INCIDENTAL, SPECIAL OR CONSEQUENTIAL DAMAGES
 * OF ANY KIND.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * File Name:
 *
 *      PlxIoctl.h
 *
 * Description:
 *
 *      This file contains the common I/O Control messages shared between
 *      the driver and the PCI API.
 *
 * Revision History:
 *
 *      04-01-08 : PLX SDK v6.00
 *
 ******************************************************************************/


#include "PlxTypes.h"
/*
#if defined(PLX_MSWINDOWS) && !defined(PLX_DRIVER)
    #include <winioctl.h>
#elif defined(PLX_LINUX)
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
#endif
*/

#ifdef __cplusplus
extern "C" {
#endif




// Used to pass IOCTL arguments down to the driver
typedef struct _PLX_PARAMS
{
    PLX_STATUS     ReturnCode;      // API status code
    PLX_DEVICE_KEY Key;             // Device key information
    U64            value[3];        // Generic storage for parameters
    union
    {
        PLX_INTERRUPT    PlxIntr;
        PLX_PHYSICAL_MEM PciMemory;
        PLX_PORT_PROP    PortProp;
        PLX_PCI_BAR_PROP BarProp;
        PLX_DMA_PROP     DmaProp;
        PLX_DMA_PARAMS   TxParams;
        PLX_DRIVER_PROP  DriverProp;
    } u;
} PLX_PARAMS, *PPLX_PARAMS; 


 

#ifdef __cplusplus
}
#endif

#endif


