// -------------------------------------------------------------------------
// 
// PRODUCT:            DMA Driver
// MODULE NAME:        PacketIOCtl.c
// 
// MODULE DESCRIPTION: 
// 
// Contains the IOCtl function handlers for Packet Mode IOCtls
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

#if TRACE_ENABLED
#include "PacketIOCtl.tmh"
#endif                          // TRACE_ENABLED

// Local functions

#ifdef ALLOC_PRAGMA
#endif                          /* ALLOC_PRAGMA */

/*! PacketBufferAllocate    
 *
 *  \brief This routine is called when a
 *     PACKET_RX_BUF_ALLOCATE_IOCTL is sent from the application
 *   This routine is called at IRQL < DISPATCH_LEVEL.
 *     \param pDevExt - Pointer to this drivers context (data store)
 *    \param Request - Pointer to the IOCtl request
 *  \return NTSTATUS
 */
NTSTATUS PacketBufferAllocate(PDEVICE_EXTENSION pDevExt, IN WDFREQUEST Request)
{
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt = NULL;
        PREQUEST_CONTEXT reqContext = NULL;
        PMDL Mdl;
        PVOID RetVirtAddress;
        NTSTATUS status = STATUS_SUCCESS;

        reqContext = RequestContext(Request);
        if (reqContext != NULL) {
                status = GetDMAEngineContext(pDevExt, reqContext->DMAEngine, &pDmaExt);
                if (!NT_SUCCESS(status)) {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Packet Buffer Allocate DMA Engine number invalid. Status: 0x%x.\n", status));
                        return status;
                }
                // See if the Application did the allocate
                if ((pDmaExt->PacketMode == PACKET_MODE_FIFO) || (pDmaExt->PacketMode == PACKET_MODE_STREAMING)) {

                        if (pDmaExt->PacketMode == PACKET_MODE_STREAMING) {
                                pDmaExt->bFreeRun = TRUE;
                        }

                        if (reqContext->Length > MIN_BUFFER_POOL_SIZE) {
                                pDmaExt->PMdl = reqContext->pMdl;
                                pDmaExt->UserVa = reqContext->pVA;

                                status = WdfRequestRetrieveOutputWdmMdl(Request, &Mdl);
                                if (NT_SUCCESS(status)) {
                                        RetVirtAddress = MmGetMdlVirtualAddress(Mdl);
                                        status = InitializeRxDescriptors(pDevExt, pDmaExt, RetVirtAddress, reqContext->Length);
                                }
                        } else {
                               KdPrintEx((1, DPFLTR_WARNING_LEVEL, "Buffer Pool size less than %d not supported.\n", MIN_BUFFER_POOL_SIZE));
                                status = STATUS_INVALID_PARAMETER;
                        }
                }
        } else {
                PBUF_ALLOC_STRUCT pBufAlloc;
                status = WdfRequestRetrieveInputBuffer(Request, sizeof(PBUF_ALLOC_STRUCT), &pBufAlloc, NULL);

                if (NT_SUCCESS(status)) {
                        // Validate EngineNum and make sure it has been properly initialized
                        if (pBufAlloc != NULL) {
                                if ((pBufAlloc->EngineNum < MAX_NUM_DMA_ENGINES) && (pDevExt->pDmaEngineDevExt[pBufAlloc->EngineNum] != NULL)) {
                                        status = GetDMAEngineContext(pDevExt, pBufAlloc->EngineNum, &pDmaExt);
                                        if (NT_SUCCESS(status)) {
                                                if (pDmaExt->PacketMode == PACKET_MODE_ADDRESSABLE) {
                                                        status = InitializeAddressablePacketDescriptors(pDevExt, pDmaExt);
                                                } else {
                                                       KdPrintEx((1, DPFLTR_WARNING_LEVEL, "Packet / allocate mode is not supported."));
                                                        status = STATUS_INVALID_PARAMETER;
                                                }
                                        }
                                }
                        }
                }
        }
        if (status != 0) {
                if (reqContext != NULL) {
                        if (reqContext->pMdl != NULL) {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Freeing Packet Buffer MDL.\n"));
                                FreeReqCtx(reqContext);
                                if (pDmaExt != NULL) {
                                        pDmaExt->PMdl = NULL;
                                        pDmaExt->UserVa = NULL;
                                }
                        }
                }
        }
        return status;
}

/*! PacketBufferRelease 
 *
 * \brief This routine is called when a
 *  PACKET_RX_BUF_RELEASE_IOCTL is sent from the application
 *  This routine is called at IRQL < DISPATCH_LEVEL.
 * \param pDevExt - Pointer to this drivers context (data store)
 * \param Request - Pointer to the IOCtl request
 * \return NTSTATUS
 */
NTSTATUS PacketBufferRelease(IN PDEVICE_EXTENSION pDevExt, IN WDFREQUEST Request)
{
        PBUF_DEALLOC_STRUCT pBufDeAlloc;
        NTSTATUS status = STATUS_SUCCESS;

        // Get the input buffer, where we find the deallocate parameters
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(BUF_DEALLOC_STRUCT),     /* Min size */
                                               (PVOID *) & pBufDeAlloc, /* buffer    */
                                               NULL);
        if (status == STATUS_SUCCESS) {
                status = STATUS_INVALID_DEVICE_REQUEST;
                // Validate EngineNum
                if ((pBufDeAlloc != NULL) && (pBufDeAlloc->EngineNum < MAX_NUM_DMA_ENGINES) && (pDevExt->pDmaEngineDevExt[pBufDeAlloc->EngineNum] != NULL)) {
                        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
                        status = GetDMAEngineContext(pDevExt, pBufDeAlloc->EngineNum, &pDmaExt);
                        if (!NT_SUCCESS(status)) {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Packet Buffer DeAllocate DMA Engine number is invalid. Status: 0x%x\n", status));
                                return status;
                        }
                        if (pDmaExt->DmaType == DMA_TYPE_PACKET_RECV) {
                                if ((pDmaExt->PacketMode == PACKET_MODE_FIFO) || (pDmaExt->PacketMode == PACKET_MODE_STREAMING)) {
                                        pDmaExt->bFreeRun = FALSE;
                                        FreeRxDescriptors(pDevExt, pDmaExt);
                                        if (pDmaExt->PMdl != NULL) {
//                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketBufferRelease: MDL = 0x%p\n", pDmaExt->PMdl);
                                                MmUnlockPages(pDmaExt->PMdl);
                                                IoFreeMdl(pDmaExt->PMdl);
                                                pDmaExt->PMdl = NULL;
                                        } else {
                                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketBufferRelease: pMDL = NULL"));
                                        }
                                        pDmaExt->UserVa = NULL;
                                        status = STATUS_SUCCESS;
                                } else if (pDmaExt->PacketMode == PACKET_MODE_ADDRESSABLE) {
                                        FreeRxDescriptors(pDevExt, pDmaExt);
                                        pDmaExt->PMdl = NULL;
                                        pDmaExt->UserVa = NULL;
                                        status = STATUS_SUCCESS;
                                } else {
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Packet / Deallocate mode is not supported."));
                                        status = STATUS_INVALID_PARAMETER;
                                }
                                pDmaExt->PacketMode = DMA_MODE_NOT_SET;
                        } else {
                                // Must be an S2C DMA Engine.
                        }
                }
        } else {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "<-- WdfRequestRetrieveInputBuffer Failed. Status: 0x%x\n", status));
        }

        return status;
}

/*! ResetDMAEngine 
 *
 * \brief This routine is called when a
 *    RESET_DMA_ENGINE_IOCTL is sent from the application
 *     This routine is called at IRQL < DISPATCH_LEVEL.
 * \param pDevExt - Pointer to this drivers context (data store)
 * \param Request - Pointer to the IOCtl request
 * \return NTSTATUS
 */
NTSTATUS ResetDMAEngineIoctlHandler(IN PDEVICE_EXTENSION pDevExt, IN WDFREQUEST Request)
{
        PRESET_DMA_STRUCT pResetDMA;
        NTSTATUS status = STATUS_SUCCESS;

       KdPrintEx((1, DPFLTR_ERROR_LEVEL, ""));

        // Get the input buffer, where we find the deallocate parameters
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(RESET_DMA_STRUCT),       /* Min size */
                                               (PVOID *) & pResetDMA,   /* buffer */
                                               NULL);
        if (status == STATUS_SUCCESS) {
                status = STATUS_INVALID_DEVICE_REQUEST;
                // Validate EngineNum
                if ((pResetDMA != NULL) && (pResetDMA->EngineNum < MAX_NUM_DMA_ENGINES) && (pDevExt->pDmaEngineDevExt[pResetDMA->EngineNum] != NULL)) {
                        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
                        status = GetDMAEngineContext(pDevExt, pResetDMA->EngineNum, &pDmaExt);
                        if (!NT_SUCCESS(status)) {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Reset DMA Engine number invalid 0x%x\n", status));
                                return status;
                        }
                        if (pDmaExt->DmaType == DMA_TYPE_PACKET_RECV) {
                                if ((pDmaExt->PacketMode == PACKET_MODE_FIFO) || (pDmaExt->PacketMode == PACKET_MODE_STREAMING)) {
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Resetting C2S FIFO Mode DMA Engine %d.\n", pResetDMA->EngineNum));
                                        FreeRxDescriptors(pDevExt, pDmaExt);
                                } else if (pDmaExt->PacketMode == PACKET_MODE_ADDRESSABLE) {
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Resetting C2S AddrMode DMA Engine %d.\n", pResetDMA->EngineNum));
                                        FreeRxDescriptors(pDevExt, pDmaExt);
                                }
                                else if (pDmaExt->PacketMode == DMA_MODE_NOT_SET) {
                                    ShutdownDMAEngine(pDevExt, pDmaExt);
                                }
                                else {
                                   KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Unknown packetmode %08x\n", pDmaExt->PacketMode));
                                    status = STATUS_DRIVER_INTERNAL_ERROR;
                                }
                        } else {
                                // Must be an S2C DMA Engine.
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Resetting S2C DMA Engine %d.\n", pResetDMA->EngineNum));
                                ShutdownDMAEngine(pDevExt, pDmaExt);
                        }
                }
                else {
                    if (pResetDMA) {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Reset failed on EngineNum %u\n", pResetDMA->EngineNum)); 
                    }
                    else
                    {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL,"pResetDMA == NULL")); 
                    }
                }
        } else {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfRequestRetrieveInputBuffer failed 0x%x\n", status));
        }
        return status;
}
