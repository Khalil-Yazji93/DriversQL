// -------------------------------------------------------------------------
// 
// PRODUCT:            DMA Driver
// MODULE NAME:        User Interrupt Request Timer
// 
// MODULE DESCRIPTION: 
// 
// Contains the the routines to run the User Interrupt request timer.
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

#include "precomp.h"

// Prototypes
VOID DMADriverUsrIntReqTimerCall(IN WDFTIMER Timer);
VOID UserInterruptDpc(IN PRKDPC Dpc, PVOID context, PVOID SystemArgument1, PVOID SystemArgument2);

#if TRACE_ENABLED
#include "UserIntReqTimer.tmh"
#endif                          // TRACE_ENABLED

/*! DMADriverUsrIntReqTimerInit
 *
 * \brief Initialize the UsrIntReq timer at driver load.  
 *  Will not start until 'DMADriverUsrIntReqTimerStart' is called.
 * \param pDevExt
 * \return Status
 */
NTSTATUS DMADriverUsrIntReqTimerInit(IN PDEVICE_EXTENSION pDevExt)
{
    KeInitializeDpc(&pDevExt->UsrIntReqDpc, UserInterruptDpc, pDevExt);
    KeInitializeTimer(&pDevExt->UsrIntReqTimer);
    return STATUS_SUCCESS;
}

VOID UserInterruptDpc(IN PRKDPC Dpc, PVOID context, PVOID SystemArgument1, PVOID SystemArgument2)
{
    PDEVICE_EXTENSION pDevExt = context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    UserIrqComplete(pDevExt, STATUS_IO_TIMEOUT);
}

/*! DMADriverUsrIntReqTimerStart
 *
 * \brief Start the UsrIntReq timer for duration <dwTimeoutMilliSec>.
 * \param pDevExt
 * \param dwTimeoutMilliSec
 * \return None
 */
VOID DMADriverUsrIntReqTimerStart(IN PDEVICE_EXTENSION pDevExt, UINT32 dwTimeoutMilliSec)
{
    LARGE_INTEGER DueTime;

    DueTime.QuadPart = dwTimeoutMilliSec;
    DueTime.QuadPart *= 10000; /* 100 nsec intervals */
    DueTime.QuadPart *= (-1); /* relative timeout */

    KeSetTimer(&pDevExt->UsrIntReqTimer, DueTime, &pDevExt->UsrIntReqDpc);
}

/*! DMADriverUsrIntReqTimerStop
 *
 * \brief Stop the UsrIntReq timer.
 * \param pDevExt
 * \return None
 */
VOID DMADriverUsrIntReqTimerStop(IN PDEVICE_EXTENSION pDevExt, BOOLEAN Wait)
{
    UNREFERENCED_PARAMETER(Wait);
    KeCancelTimer(&pDevExt->UsrIntReqTimer);
}

/*! DMADriverUsrIntReqTimerDelete
 *
 * \brief Remove the UsrIntReq timer.
 *  Used with removing/uninstall/disable the driver.
 * \param pDevExt
 * \return None
 */
VOID DMADriverUsrIntReqTimerDelete(IN PDEVICE_EXTENSION pDevExt)
{
    DMADriverUsrIntReqTimerStop(pDevExt, FALSE);
}

