// -------------------------------------------------------------------------
//
// PRODUCT:            DMA Driver
// MODULE NAME:        PacketDMA.c
//
// MODULE DESCRIPTION:
//
// Contains the functions to support Packet Mode DMA
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
#include "PacketDMA.tmh"
#endif                          // TRACE_ENABLED

// Local functions

#ifdef ALLOC_PRAGMA
#endif                          /* ALLOC_PRAGMA */

// Local defines
EVT_WDF_PROGRAM_DMA PacketProgramS2CDmaCallback;
EVT_WDF_PROGRAM_DMA PacketProgramC2SDmaCallback;

BOOLEAN PacketProgramS2CDmaCallback(IN WDFDMATRANSACTION DmaTransaction, IN WDFDEVICE Device, IN WDFCONTEXT Context, IN WDF_DMA_DIRECTION Direction, IN PSCATTER_GATHER_LIST SgList);

BOOLEAN PacketProgramC2SDmaCallback(IN WDFDMATRANSACTION DmaTransaction, IN WDFDEVICE Device, IN WDFCONTEXT Context, IN WDF_DMA_DIRECTION Direction, IN PSCATTER_GATHER_LIST SgList);

/*
 * Calculate how much of this buffer that a HW descriptor can handle.
 */
static inline UINT64 PacketProgramDescFrag(ULONG SGLength)
{
    return (SGLength > PACKET_DESC_BYTE_COUNT_MASK) ? PACKET_DESC_BYTE_COUNT_MASK : (UINT64)SGLength;
}

/*
 * Count the number of descriptor fragments required for this S/G list.
 */
static UINT32 PacketProgramCountDescFragments(PSCATTER_GATHER_LIST SgList)
{
    ULONG SGLength;
    ULONG i;
    UINT32 SGFragments = 0;

    for (i=0; i<SgList->NumberOfElements; i++) {
        SGLength = SgList->Elements[i].Length;
        while (SGLength) {
            SGFragments++;
            SGLength -= (ULONG)PacketProgramDescFrag(SGLength);
        }
    }
    return SGFragments;
}


//--------------------------------------------------------
//  S2C Packet Mode routines
//--------------------------------------------------------

// FIFO Packet Mode functions

/*! PacketStartSend
 *
 * \brief -This routine setups the send request then calls
 *  WdfDmaTransactionExecute to start or queue the actual DMA request
 * \param Request - WDF I/O Request (PACKET_SEND_IOCTL)
 * \param DevExt - WDF Driver context
 * \param pSendPacket - Contents of the PACKET_SEND_IOCTL request
 * \return status
 */
NTSTATUS PacketStartSend(IN WDFREQUEST Request, IN PDEVICE_EXTENSION pDevExt, IN PPACKET_SEND_STRUCT pSendPacket)
{
        NTSTATUS status = STATUS_SUCCESS;
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
        WDFDMATRANSACTION DmaTransaction;
        PDMA_XFER pDmaXfer;
        WDF_OBJECT_ATTRIBUTES attributes;
        PREQUEST_CONTEXT reqContext = NULL;

        status = GetDMAEngineContext(pDevExt, pSendPacket->EngineNum, &pDmaExt);
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketSend DMA Engine number invalid 0x%x", status));
                return status;
        }

        reqContext = RequestContext(Request);
        if (reqContext == NULL) {
                return STATUS_ACCESS_VIOLATION;
        }

        if (reqContext->pMdl == NULL) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketSend MDL == NULL\n"));
                return STATUS_ACCESS_VIOLATION;
        }

        DMADriverLock(pDmaExt);

        if ((UINT64) reqContext->Length >= pSendPacket->Length) {
                // Create a DMA Transaction object just for this transfer
                WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DMA_XFER);
                status = WdfDmaTransactionCreate(pDmaExt->DmaEnabler, &attributes, &DmaTransaction);

                // if no errors kick off the DMA, first save a pointer to the request.
                if (NT_SUCCESS(status)) {
                        // Keep a pointer the Request and the accumulated byte count in the Transaction
                        pDmaXfer = DMAXferContext(DmaTransaction);
                        pDmaXfer->Request = Request;
                        pDmaXfer->bytesTransferred = 0;
                        pDmaXfer->CardAddress = pSendPacket->CardOffset;
                        pDmaXfer->UserControl = pSendPacket->UserControl;
                        pDmaXfer->Mode = 0;
                        pDmaXfer->PacketStatus = 0;
                        pDmaXfer->pMdl = reqContext->pMdl;

                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "    Calling WdfDmaTransactionInitialize, Length %d, MdlLength %d", (UINT32) pSendPacket->Length, reqContext->Length));

                        status = WdfDmaTransactionInitialize(DmaTransaction,
                                                             (PFN_WDF_PROGRAM_DMA) PacketProgramS2CDmaCallback, pDmaExt->DmaDirection, reqContext->pMdl, reqContext->pVA, (size_t) pSendPacket->Length);

                        if (NT_SUCCESS(status)) {
                                // Put the request on a WDF maintained Queue in case it gets canceled before we complete it
                                WdfSpinLockAcquire(pDmaExt->DmaSpinLock);
                                status = WdfRequestForwardToIoQueue(Request, pDmaExt->TransactionQueue);
                                WdfSpinLockRelease(pDmaExt->DmaSpinLock);
                                if (NT_SUCCESS(status)) {
                                        // start the DMA, via PacketProgramDmaCallback
                                        status = WdfDmaTransactionExecute(DmaTransaction, pDmaExt);
                                        if (!NT_SUCCESS(status)) {
                                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketStartSend failed 0x%x", status));
                                                // Make sure we get the Request off the queue.
                                                WdfSpinLockAcquire(pDmaExt->DmaSpinLock);
                                                FindRequestByRequest(pDmaExt, Request);
                                                WdfSpinLockRelease(pDmaExt->DmaSpinLock);
                                                FreeReqCtx(reqContext);
                                                WdfObjectDelete(DmaTransaction);
                                        }
                                } else {
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfRequestForwardToIoQueue failed 0x%x", status));
                                        FreeReqCtx(reqContext);
                                        WdfObjectDelete(DmaTransaction);
                                }
                        } else {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfDmaTransactionInitialize failed 0x%x", status));
                                FreeReqCtx(reqContext);
                                WdfObjectDelete(DmaTransaction);
                        }
                } else {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfDmaTransactionCreate failed 0x%x", status));
                        FreeReqCtx(reqContext);
                }
        } else {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, " Invalid length, Length %d, MdlLength %d", (UINT32) pSendPacket->Length, reqContext->Length));
                FreeReqCtx(reqContext);
                status = STATUS_INVALID_PARAMETER;
        }

        DMADriverUnlock(pDmaExt);

        return status;
}

// Addressable Packet Mode functions

/*! PacketStartWrite
 *
 * \brief This routine setups the send request then calls
 *     WdfDmaTransactionExecute to start or queue the actual DMA request
 * \param Request - WDF I/O Request (PACKET_SEND_IOCTL)
 * \param DevExt - WDF Driver context
 * \param pWritePacket - Contents of the PACKET_SEND_IOCTL request
 * \return NTSTATUS - STATUS_SUCCESS if it works, FALSE if error.
 */
NTSTATUS PacketStartWrite(IN WDFREQUEST Request, IN PDEVICE_EXTENSION pDevExt, IN PPACKET_WRITE_STRUCT pWritePacket)
{
        NTSTATUS status = STATUS_SUCCESS;
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
        WDFDMATRANSACTION DmaTransaction;
        PDMA_XFER pDmaXfer;
        WDF_OBJECT_ATTRIBUTES attributes;
        PREQUEST_CONTEXT reqContext = NULL;

        status = GetDMAEngineContext(pDevExt, pWritePacket->EngineNum, &pDmaExt);
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketWrite DMA Engine number invalid 0x%x", status));
                return status;
        }

        reqContext = RequestContext(Request);
        if (reqContext == NULL) {
                return STATUS_ACCESS_VIOLATION;
        }
        if (reqContext->pMdl == NULL) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketWrite MDL == NULL\n"));
                return STATUS_ACCESS_VIOLATION;
        }

        DMADriverLock(pDmaExt);

        if ((UINT64) reqContext->Length >= pWritePacket->Length) {
                // Create a DMA Transaction object just for this transfer
                WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DMA_XFER);
                status = WdfDmaTransactionCreate(pDmaExt->DmaEnabler, &attributes, &DmaTransaction);

                // if no errors kick off the DMA, first save a pointer to the request.
                if (NT_SUCCESS(status)) {
                        // Keep a pointer the Request and the accumulated byte count in the Transaction
                        pDmaXfer = DMAXferContext(DmaTransaction);
                        pDmaXfer->Request = Request;
                        pDmaXfer->bytesTransferred = 0;
                        pDmaXfer->CardAddress = pWritePacket->CardOffset;
                        pDmaXfer->UserControl = pWritePacket->UserControl;
                        pDmaXfer->Mode = pWritePacket->ModeFlags;
                        pDmaXfer->PacketStatus = 0;
                        pDmaXfer->pMdl = reqContext->pMdl;

                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "    Calling WdfDmaTransactionInitialize, Length %d, MdlLength %d", (UINT32) pWritePacket->Length, reqContext->Length));

                        status = WdfDmaTransactionInitialize(DmaTransaction,
                                                             (PFN_WDF_PROGRAM_DMA) PacketProgramS2CDmaCallback,
                                                             pDmaExt->DmaDirection, reqContext->pMdl, reqContext->pVA, (size_t) pWritePacket->Length);

                        if (NT_SUCCESS(status)) {
                                // Put the request on a WDF maintained Queue in case it gets canceled before we complete it
                                WdfSpinLockAcquire(pDmaExt->DmaSpinLock);
                                status = WdfRequestForwardToIoQueue(Request, pDmaExt->TransactionQueue);
                                WdfSpinLockRelease(pDmaExt->DmaSpinLock);
                                if (NT_SUCCESS(status)) {
                                        // start the DMA, via PacketProgramDmaCallback
                                        status = WdfDmaTransactionExecute(DmaTransaction, pDmaExt);
                                        if (!NT_SUCCESS(status)) {
                                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfDmaTransactionExecute failed 0x%x", status));
                                                // Make sure we get the Request off the queue.
                                                WdfSpinLockAcquire(pDmaExt->DmaSpinLock);
                                                FindRequestByRequest(pDmaExt, Request);
                                                WdfSpinLockRelease(pDmaExt->DmaSpinLock);
                                                FreeReqCtx(reqContext);
                                                WdfObjectDelete(DmaTransaction);
                                        }
                                } else {
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfRequestForwardToIoQueue failed 0x%x", status));
                                        FreeReqCtx(reqContext);
                                        WdfObjectDelete(DmaTransaction);
                                }
                        } else {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfDmaTransactionInitialize failed 0x%x", status));
                                FreeReqCtx(reqContext);
                                WdfObjectDelete(DmaTransaction);
                        }
                } else {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfDmaTransactionCreate failed 0x%x", status));
                        FreeReqCtx(reqContext);
                }
        } else {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, " Invalid length, Length %d, MdlLength %d", (UINT32) pWritePacket->Length, reqContext->Length));
                FreeReqCtx(reqContext);
                status = STATUS_INVALID_PARAMETER;
        }

        DMADriverUnlock(pDmaExt);

        return status;
}

/*! PacketProgramS2CDmaCallback
 *
 * \brief This routine performs the actual programming of the Packet DMA engine and descriptors
 * \param DmaTransaction - WDF DMA Transaction handle
 * \param Device - WDF DMA Device handle
 * \param Context - WDF Context pointer (our DMA Engine pointer)
 * \param Direction - WDF Direction of DAM Transfer
 * \param SgList - Pointer to the WDF Scatter/Gather list
 * \return TRUE if it works, FALSE if error.
 * \note This function is called by WdfDmaTransactionExecute
 * \note This function is called in the transfer sequence.
 *  It should be optimized to be as fast as possible.
 */
BOOLEAN PacketProgramS2CDmaCallback(IN WDFDMATRANSACTION DmaTransaction, IN WDFDEVICE Device, IN WDFCONTEXT Context, IN WDF_DMA_DIRECTION Direction, IN PSCATTER_GATHER_LIST SgList)
{
        NTSTATUS status = STATUS_SUCCESSFUL;
        UINT32 SGFragments;
        UINT32 SGIndex;
        ULONG SGLength;
        UINT64 SGAddr;
        PDEVICE_EXTENSION pDevExt;
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
        PDRIVER_DESC_STRUCT pDrvDesc;
        PDMA_DESCRIPTOR_STRUCT pHWDesc;
        PDMA_DESCRIPTOR_STRUCT pLastHWDesc = NULL;
        PDMA_XFER pDmaXfer;
        UINT64 CardAddress;
        UINT32 numAvailDescriptors;
        UINT32 Control;
        UINT32 descNum;

        UNREFERENCED_PARAMETER(Direction);

       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "--> PacketProgramDmaCallback, IRQL=%d", KeGetCurrentIrql()));

        // Get Device Extensions
        pDevExt = DMADriverGetDeviceContext(Device);
        pDmaExt = (PDMA_ENGINE_DEVICE_EXTENSION) Context;
        pDmaXfer = DMAXferContext(DmaTransaction);
        CardAddress = pDmaXfer->CardAddress;

        /*
         * One thread at a time.
         */
        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);

        // Determine number of available descriptors
        numAvailDescriptors = pDmaExt->NumberOfDescriptors - pDmaExt->NumberOfUsedDescriptors;

        SGFragments = PacketProgramCountDescFragments(SgList);

        if (numAvailDescriptors >= SGFragments) {
                // Lock the access to the head pointer, get the next pointer, calc the new head, store it back and release the lock
                pDrvDesc = pDmaExt->pNextDesc;
                pHWDesc = pDrvDesc->pHWDesc;

                // Setup descriptor control, Interrupt when the DMA is stopped short
                Control = PACKET_DESC_S2C_CTRL_START_OF_PACKET;

                // Get the first fragment address and length
                SGIndex = 0;
                SGLength = SgList->Elements[SGIndex].Length;
                SGAddr = SgList->Elements[SGIndex].Address.QuadPart;
                SGIndex++;

                // setup each of the descriptors
                for (descNum = 0; descNum < SGFragments; descNum++) {
                        if (descNum == (SGFragments - 1)) {
                                /*
                                   End the processing here only interrupt on completion of the
                                   last DMA descriptor and when the DMA is stopped short.
                                 */
                                Control |= PACKET_DESC_S2C_CTRL_END_OF_PACKET | PACKET_DESC_S2C_CTRL_IRQ_ON_COMPLETE | PACKET_DESC_S2C_CTRL_IRQ_ON_ERROR;
                        }
                        // Setup the descriptor
                        pHWDesc->S2C.StatusFlags_BytesCompleted = (UINT32)PacketProgramDescFrag(SGLength);
                        // Set the User Control field in the first packet only
                        pHWDesc->S2C.UserControl = pDmaXfer->UserControl;
                        pDmaXfer->UserControl = 0;

                        pHWDesc->S2C.CardAddress = (UINT32) (CardAddress & 0xFFFFFFFF);
                        pHWDesc->S2C.ControlFlags_ByteCount = (UINT32) ((CardAddress & 0xF00000000) >> 12);
                        pHWDesc->S2C.ControlFlags_ByteCount |= ((UINT32)PacketProgramDescFrag(SGLength)) | Control;
                        pHWDesc->S2C.SystemAddressPhys = SGAddr;
                        pDrvDesc->DmaTransaction = DmaTransaction;

                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Descriptor #%d, Length=%d, Control=0x%x, SA=0x%x, ND=0x%p", descNum, SGLength, Control, (UINT32) SGAddr, pDrvDesc->pNextDesc));

                        // Remove the start of packet bit for next descriptor and zero the CardAddress.
                        Control &= ~PACKET_DESC_S2C_CTRL_START_OF_PACKET;

                        // Update the card offset in the next descriptor.
                        CardAddress += PacketProgramDescFrag(SGLength);

                        // Update pointers
                        pLastHWDesc = pHWDesc;
                        pDrvDesc = pDrvDesc->pNextDesc;
                        pHWDesc = pDrvDesc->pHWDesc;
                        _InterlockedIncrement(&pDmaExt->NumberOfUsedDescriptors);

                        // See if we have exhausted this fragment
                        SGAddr += PacketProgramDescFrag(SGLength);
                        SGLength -= (ULONG)PacketProgramDescFrag(SGLength);
                        if (SGLength == 0) {
                                SGLength = SgList->Elements[SGIndex].Length;
                                SGAddr = SgList->Elements[SGIndex].Address.QuadPart;
                                SGIndex++;
                        }
                }

                pDmaExt->TimeoutCount = CARD_WATCHDOG_INTERVAL;

                if (pLastHWDesc != NULL) {
                        // setup the descriptor pointer
                        pDmaExt->pDmaEng->SoftwareDescriptorPtr = pLastHWDesc->S2C.NextDescriptorPhys;
                        pDmaExt->pNextDesc = pDrvDesc;
                }
        } else {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Too many desc, Available = %d, Required = %d", numAvailDescriptors, SGFragments));
                status = STATUS_INSUFFICIENT_RESOURCES;
        }

        WdfSpinLockRelease(pDmaExt->DmaSpinLock);

        if (!NT_SUCCESS(status)) {
                NTSTATUS FinalStatus = status;
                // an error has occurred, reset this transaction
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "DMAD PacketProgramDmaCallback failed status 0x%x", status));
                WdfDmaTransactionDmaCompletedFinal(DmaTransaction, 0, &FinalStatus);
                // complete the transaction
                WdfRequestCompleteWithInformation(pDmaXfer->Request, status, 0);
                WdfObjectDelete(DmaTransaction);
                return FALSE;
        }
        return TRUE;
}

/*! PacketS2CDpc
 *
 *  \brief This routine processes completed
 *    DMA Packet descriptors, dequeues requests and completes them
 *     \param Dpc - Pointer to the driver context for this Dpc
 *  \return none
 */
VOID PacketS2CDpc(IN WDFDPC Dpc)
{
        PDPC_CTX pDpcCtx;
        PDEVICE_EXTENSION pDevExt;
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
        PDRIVER_DESC_STRUCT pDrvDesc;
        PDMA_DESCRIPTOR_STRUCT pHWDesc;
        PDMA_XFER pDmaXfer = NULL;
        WDFREQUEST Request;
        BOOLEAN transactionComplete;
        NTSTATUS status = STATUS_SUCCESS;

        pDevExt = DMADriverGetDeviceContext(WdfDpcGetParentObject(Dpc));
        pDpcCtx = DPCContext(Dpc);
        pDmaExt = pDpcCtx->pDmaExt;

        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);

        // Inc the DPC Count
        pDmaExt->DPCsInLastSecond++;

        // Make sure we have completed descriptor(s)
        pDrvDesc = pDmaExt->pTailDesc;
        pHWDesc = pDrvDesc->pHWDesc;

       KdPrintEx((1, DPFLTR_INFO_LEVEL, "DMA Engine %u status 0x%08x", pDmaExt->DmaEngine, pDmaExt->pDmaEng->ControlStatus));

        while (pHWDesc->S2C.StatusFlags_BytesCompleted & (PACKET_DESC_S2C_STAT_COMPLETE|PACKET_DESC_S2C_STAT_ERROR)) {
                // At this point we know we have a completed Send DMA Descriptor
                // Update the contexts links to the next descriptor
                pDmaExt->pTailDesc = pDrvDesc->pNextDesc;
                _InterlockedDecrement(&pDmaExt->NumberOfUsedDescriptors);

                // The Transaction data pointer is in every descriptor for a given Request
                pDmaXfer = DMAXferContext(pDrvDesc->DmaTransaction);

                if (pHWDesc->S2C.ControlFlags_ByteCount & PACKET_DESC_S2C_CTRL_START_OF_PACKET) {
                        pDmaXfer->bytesTransferred = 0;
                }
                pDmaXfer->bytesTransferred += (pHWDesc->S2C.StatusFlags_BytesCompleted & PACKET_DESC_COMPLETE_BYTE_COUNT_MASK);
                pDmaXfer->PacketStatus |= (pHWDesc->S2C.StatusFlags_BytesCompleted & PACKET_DESC_S2C_STAT_ERROR);

                if (pHWDesc->S2C.ControlFlags_ByteCount & PACKET_DESC_S2C_CTRL_END_OF_PACKET) {
                        if (pDmaXfer->PacketStatus) {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "DMADriver Error: Control/Status returned error"));
                                // Stop the current DMA transfer, tell driver to not continue
                                transactionComplete = WdfDmaTransactionDmaCompletedFinal(pDrvDesc->DmaTransaction, pDmaXfer->bytesTransferred, &status);
                                status = STATUS_ADAPTER_HARDWARE_ERROR;
                        } else {
                                status = STATUS_SUCCESS;
                                // complete the transaction,  transaction completed successfully
                                // tell the framework that this DMA set is complete
                                transactionComplete = WdfDmaTransactionDmaCompletedWithLength(pDrvDesc->DmaTransaction, pDmaXfer->bytesTransferred, &status);
                        }

                        // Is the full transaction complete?
                        if (transactionComplete) {
                           KdPrintEx((1, DPFLTR_INFO_LEVEL, "      Transaction Complete, size=%lu", (UINT32) pDmaXfer->bytesTransferred));
                                if (pDmaXfer->pMdl != NULL) {
                                        // Unlock the pages locked by MmProbeAndLockPages
                                        MmUnlockPages(pDmaXfer->pMdl);
                                        IoFreeMdl(pDmaXfer->pMdl);
                                        pDmaXfer->pMdl = NULL;
                                } else {
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketSend/Write MDL == NULL\n"));
                                }

                                // Retrieve the originating request from the Transaction data extension
                                Request = FindRequestByRequest(pDmaExt, pDmaXfer->Request);
                                if (Request != NULL) {
                                        // complete the transaction
                                        WdfRequestCompleteWithInformation(Request, status, pDmaXfer->bytesTransferred);
                                }
                                // Release the Transaction record
                                WdfDmaTransactionRelease(pDrvDesc->DmaTransaction);
                                pDmaXfer->Request = NULL;
                                WdfObjectDelete(pDrvDesc->DmaTransaction);
                                pDmaXfer = NULL;
                        }
                }
                // Indicate we processed this descriptor by clearing Complete and Error flags
                pHWDesc->S2C.StatusFlags_BytesCompleted &= ~(PACKET_DESC_S2C_STAT_COMPLETE | PACKET_DESC_S2C_STAT_ERROR);

                // Link to the next packets
                pDrvDesc = pDrvDesc->pNextDesc; // Link to the next descriptor in the chain
                pHWDesc = pDrvDesc->pHWDesc;
        }

        DMADriverAckDmaInterrupt(pDmaExt);

        WdfSpinLockRelease(pDmaExt->DmaSpinLock);
}

//-----------------------------------------------------------
//C2S Packet Mode routines
//-----------------------------------------------------------

//-----------------------------------------------------------
// Addressable Packet Mode functions
//-----------------------------------------------------------

/*! PacketStartRead
 *  \brief This routine setups the Read request then calls
 *     WdfDmaTransactionExecute to start or queue the actual DMA request
 *
 *     \param Request - WDF I/O Request (PACKET_SEND_IOCTL)
 *     \param DevExt - WDF Driver context
 *     \param pReadPacket - Contents of the PACKET_READ_IOCTL request
 *
 *     \return NTSTATUS - STATUS_SUCCESS if it works, FALSE if error.
 */
NTSTATUS PacketStartRead(IN WDFREQUEST Request, IN PDEVICE_EXTENSION pDevExt, IN PPACKET_READ_STRUCT pReadPacket)
{
        NTSTATUS status = STATUS_SUCCESS;
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
        WDFDMATRANSACTION DmaTransaction;
        PDMA_XFER pDmaXfer;
        WDF_OBJECT_ATTRIBUTES attributes;
        PREQUEST_CONTEXT reqContext = NULL;

        status = GetDMAEngineContext(pDevExt, pReadPacket->EngineNum, &pDmaExt);
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketRead DMA Engine number invalid 0x%x", status));
                return status;
        }

        if (pReadPacket->Length == 0) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketRead transfer length = 0"));
                return STATUS_SUCCESSFUL;
        }

        reqContext = RequestContext(Request);
        if (reqContext == NULL) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketRead ReqContext = NULL"));
                return STATUS_ACCESS_VIOLATION;
        }
        if (reqContext->pMdl == NULL) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketSend MDL == NULL\n"));
                return STATUS_ACCESS_VIOLATION;
        }

        DMADriverLock(pDmaExt);

        // Create a DMA Transaction object just for this transfer
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DMA_XFER);
        status = WdfDmaTransactionCreate(pDmaExt->DmaEnabler, &attributes, &DmaTransaction);

        // if no errors kick off the DMA, first save a pointer to the request.
        if (NT_SUCCESS(status)) {
                // Keep a pointer the Request and the accumulated byte count in the Transaction
                pDmaXfer = DMAXferContext(DmaTransaction);
                pDmaXfer->Request = Request;
                pDmaXfer->bytesTransferred = 0;
                pDmaXfer->CardAddress = pReadPacket->CardOffset;
                pDmaXfer->pMdl = reqContext->pMdl;
                pDmaXfer->Mode = pReadPacket->ModeFlags;
                pDmaXfer->PacketStatus = 0;

               KdPrintEx((1, DPFLTR_INFO_LEVEL, " Calling WdfDmaTransactionInitialize, Length %d", (UINT32) pReadPacket->Length));
                //#pragma warning(suppress: 28160)
                if ((DmaTransaction == NULL) || (reqContext->pMdl == NULL) || (reqContext->pVA == NULL)) {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketStartRead Null pointer found"));
                        DbgBreakPoint();
                        WdfObjectDelete(DmaTransaction);
                        status = STATUS_INVALID_PARAMETER;
                        goto PacketStartReadExit;
                }
                status = WdfDmaTransactionInitialize(DmaTransaction,
                                                     (PFN_WDF_PROGRAM_DMA) PacketProgramC2SDmaCallback, pDmaExt->DmaDirection, reqContext->pMdl, reqContext->pVA, (size_t) pReadPacket->Length);
                if (NT_SUCCESS(status)) {
                        // Put the request on a WDF maintained Queue in case it gets canceled before we complete it
                        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);
                        status = WdfRequestForwardToIoQueue(Request, pDmaExt->TransactionQueue);
                        WdfSpinLockRelease(pDmaExt->DmaSpinLock);

                        if (NT_SUCCESS(status)) {
                                // start the DMA, via PacketProgramDmaCallback
                                status = WdfDmaTransactionExecute(DmaTransaction, pDmaExt);
                                if (!NT_SUCCESS(status)) {
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfDmaTransactionExecute failed 0x%x", status));
                                        // Make sure we get the Request off the queue.
                                        FreeReqCtx(reqContext);
                                        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);
                                        FindRequestByRequest(pDmaExt, Request);
                                        WdfSpinLockRelease(pDmaExt->DmaSpinLock);
                                        WdfObjectDelete(DmaTransaction);
                                }
                        } else {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfRequestForwardToIoQueue failed 0x%x", status));
                                FreeReqCtx(reqContext);
                                WdfObjectDelete(DmaTransaction);
                        }
                } else {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfDmaTransactionInitialize failed 0x%x", status));
                        FreeReqCtx(reqContext);
                        WdfObjectDelete(DmaTransaction);
                }
        } else {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfDmaTransactionCreate failed 0x%x", status));
                FreeReqCtx(reqContext);
        }

PacketStartReadExit:

        DMADriverUnlock(pDmaExt);

        return status;
}

/*! PacketProgramC2SDmaCallback
 *
 *     \brief This routine performs the actual programming of the
 *   Packet DMA engine and descriptors
 *    \param DmaTransaction - WDF DMA Transaction handle
 *     \param Device - WDF DMA Device handle
 *     \param Context - WDF Context pointer (our DMA Engine pointer)
 *     \param Direction - WDF Direction of DAM Transfer
 *  \param SgList - Pointer to the WDF Scatter/Gather list
 *  \return TRUE if it works, FALSE if error.
 *
 *  \note This function is called by WdfDmaTransactionExecute
 *  \note This function is called in the transfer sequence.
 *     It should be optimized to be as fast as possible.
 *
 */
BOOLEAN PacketProgramC2SDmaCallback(IN WDFDMATRANSACTION DmaTransaction, IN WDFDEVICE Device, IN WDFCONTEXT Context, IN WDF_DMA_DIRECTION Direction, IN PSCATTER_GATHER_LIST SgList)
{
        NTSTATUS status = STATUS_SUCCESSFUL;
        UINT32 SGFragments;
        UINT32 SGIndex;
        ULONG SGLength;
        UINT64 SGAddr;
        PDEVICE_EXTENSION pDevExt;
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
        PDRIVER_DESC_STRUCT pDrvDesc;
        PDMA_DESCRIPTOR_STRUCT pHWDesc;
        PDMA_DESCRIPTOR_STRUCT pLastHWDesc = NULL;
        PDMA_XFER pDmaXfer;
        UINT64 CardAddress;
        UINT32 Control;
        UINT32 numAvailDescriptors;
        UINT32 descNum;

        UNREFERENCED_PARAMETER(Direction);

       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "--> PacketProgramDmaCallback, IRQL=%d", KeGetCurrentIrql()));

        // Get Device Extensions
        pDevExt = DMADriverGetDeviceContext(Device);
        pDmaExt = (PDMA_ENGINE_DEVICE_EXTENSION) Context;
        pDmaXfer = DMAXferContext(DmaTransaction);
        CardAddress = pDmaXfer->CardAddress;

        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);

        // Determine number of available descriptors
        numAvailDescriptors = pDmaExt->NumberOfDescriptors - pDmaExt->NumberOfUsedDescriptors;

        // Setup descriptor control, Set for the first descriptor
        Control = PACKET_DESC_C2S_CTRL_START_OF_PACKET;

        // Count the fragments, including the fragments bigger than one DMA Descriptor size
        SGFragments = PacketProgramCountDescFragments(SgList);

        if (numAvailDescriptors >= SGFragments) {
                // Lock the access to the head pointer, get the next pointer, calc the new head, store it back and release the lock
                pDrvDesc = pDmaExt->pNextDesc;
                pHWDesc = pDrvDesc->pHWDesc;

                // Get the first fragment address and length
                SGIndex = 0;
                SGLength = SgList->Elements[SGIndex].Length;
                SGAddr = SgList->Elements[SGIndex].Address.QuadPart;
                SGIndex++;

                // setup each of the descriptors
                for (descNum = 0; descNum < SGFragments; descNum++) {
                        // setup the descriptor
                        pHWDesc->C2S.StatusFlags_BytesCompleted = (UINT32)PacketProgramDescFrag(SGLength);
                        pHWDesc->C2S.UserStatus = 0;
                        pHWDesc->C2S.CardAddress = (UINT32) (CardAddress & 0xFFFFFFFF);
                        if (descNum == (SGFragments - 1)) {
                                // End the processing here, Only interrupt on completion of the last DMA descriptor and
                                // when the DMA is stopped short.
                                Control |= PACKET_DESC_C2S_CTRL_END_OF_PACKET | PACKET_DESC_C2S_CTRL_IRQ_ON_COMPLETE | PACKET_DESC_C2S_CTRL_IRQ_ON_ERROR;
                        }
                        pHWDesc->C2S.ControlFlags_ByteCount = ((UINT32) ((CardAddress & 0xF00000000) >> 12)) | ((UINT32)PacketProgramDescFrag(SGLength)) | Control;
                        pHWDesc->C2S.SystemAddressPhys = SGAddr;
                        pDrvDesc->DmaTransaction = DmaTransaction;

                       KdPrintEx((1, DPFLTR_INFO_LEVEL, "Descriptor #%d, Length=%d, SA=0x%x, ND=0x%p", descNum, SGLength, (UINT32) SGAddr, pDrvDesc->pNextDesc));

                        // Remove the start of packet bit for next descriptor and zero out CardAddress
                        Control &= ~PACKET_DESC_S2C_CTRL_START_OF_PACKET;

                        // Card Address must be contiguous between Descriptors in the same packet. Use of DescCardAddress is optional. If unused, tie to 0.
                        CardAddress += PacketProgramDescFrag(SGLength);

                        // update pointers
                        pLastHWDesc = pDrvDesc->pHWDesc;
                        pDrvDesc = pDrvDesc->pNextDesc;
                        pHWDesc = pDrvDesc->pHWDesc;
                        _InterlockedIncrement(&pDmaExt->NumberOfUsedDescriptors);

                        // See if we have exhausted this fragment
                        SGAddr += PacketProgramDescFrag(SGLength);
                        SGLength -= (ULONG)PacketProgramDescFrag(SGLength);
                        if (SGLength == 0) {
                                SGLength = SgList->Elements[SGIndex].Length;
                                SGAddr = SgList->Elements[SGIndex].Address.QuadPart;
                                SGIndex++;
                        }
                }
                pDmaExt->TimeoutCount = CARD_WATCHDOG_INTERVAL;

                if (pLastHWDesc != NULL) {
                        // setup the descriptor pointer
                        pDmaExt->pDmaEng->SoftwareDescriptorPtr = pLastHWDesc->C2S.NextDescriptorPhys;
                        pDmaExt->pNextDesc = pDrvDesc;
                }
        } else {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Too many desc, Available = %d, Required = %d", numAvailDescriptors, SGFragments));
                status = STATUS_INSUFFICIENT_RESOURCES;
        }
        WdfSpinLockRelease(pDmaExt->DmaSpinLock);

        if (!NT_SUCCESS(status)) {
                NTSTATUS FinalStatus = status;

                // an error has occurred, reset this transaction
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "DMAD PacketProgramC2SDmaCallback failed status 0x%x", status));
                if (pDmaXfer->pMdl != NULL) {
                        MmUnlockPages(pDmaXfer->pMdl);
                        IoFreeMdl(pDmaXfer->pMdl);
                        pDmaXfer->pMdl = NULL;
                }
                WdfDmaTransactionDmaCompletedFinal(DmaTransaction, 0, &FinalStatus);
                // complete the transaction
                WdfRequestCompleteWithInformation(pDmaXfer->Request, status, 0);
                WdfObjectDelete(DmaTransaction);
                return FALSE;
        }
        return TRUE;
}

/*! PacketC2SDpc
 *
 *    \brief - This routine processes completed
 *      DMA Packet descriptors, dequeues requests and completes them
 *     \param Dpc - Pointer to the driver context for this Dpc
 *  \return nothing
 */
VOID PacketC2SDpc(IN WDFDPC Dpc)
{
        PDPC_CTX pDpcCtx;
        PDEVICE_EXTENSION pDevExt;
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;

        pDevExt = DMADriverGetDeviceContext(WdfDpcGetParentObject(Dpc));
        pDpcCtx = DPCContext(Dpc);
        pDmaExt = pDpcCtx->pDmaExt;

        // Inc the DPC Count
        pDmaExt->DPCsInLastSecond++;

        if (pDmaExt->PacketMode == PACKET_MODE_FIFO) {
                // We only want one thread processing recieves at a time.
                PacketProcessCompletedReceives(pDevExt, pDmaExt);
        } else if (pDmaExt->PacketMode == PACKET_MODE_ADDRESSABLE) {
                PacketReadComplete(pDevExt, pDmaExt);
        } else if (pDmaExt->PacketMode == PACKET_MODE_STREAMING) {
                pDmaExt->DMAEngineStatus |= DMA_OVERRUN_ERROR;
        }

        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);
        DMADriverAckDmaInterrupt(pDmaExt);
        WdfSpinLockRelease(pDmaExt->DmaSpinLock);
}

// FIFO Packet Mode functions

static NTSTATUS CheckForCompletedPacket(IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, BOOLEAN *Completed)
{
    PDRIVER_DESC_STRUCT pDrvDesc;
    PDMA_DESCRIPTOR_STRUCT pHWDesc;
    UINT32 CachedDescStatus;
    LONG NumberOfCheckedDescriptors=0;

    *Completed = FALSE;

    pDrvDesc = pDmaExt->pNextDesc;
    pHWDesc = pDrvDesc->pHWDesc;

    if (pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_C2S_STAT_COMPLETE) {

        if (!(pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_C2S_STAT_START_OF_PACKET)) {
           KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Missing SOP at Head descriptor 0x%p (0x%x)\n", pHWDesc, pHWDesc->C2S.StatusFlags_BytesCompleted));
            return STATUS_UNSUCCESSFUL;
        }

        // Walk the descriptors looking for the EOP descriptor. It could be this descriptor
        do {
            // Save a copy of the status flags for later loop while test
            CachedDescStatus = pHWDesc->C2S.StatusFlags_BytesCompleted;
            if (CachedDescStatus & (PACKET_DESC_C2S_STAT_COMPLETE | PACKET_DESC_C2S_STAT_ERROR)) {
                if (pDrvDesc->DescFlags == DESC_FLAGS_HW_OWNED) {
                    // Link to the next descriptor
                    pDrvDesc = pDrvDesc->pNextDesc;
                    pHWDesc = pDrvDesc->pHWDesc;
                } else {  // This is an ERROR! It means we overran the queue
                   KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Descriptor is NOT owned by hardware"));
                    return STATUS_DRIVER_INTERNAL_ERROR;
                }
            } else {  // This Packet is not complete, exit out.
               KdPrintEx((1, DPFLTR_INFO_LEVEL, "Packet is not complete, exiting"));
                return STATUS_SUCCESS;
            }
        } while ((!(CachedDescStatus & PACKET_DESC_C2S_STAT_END_OF_PACKET)) && ((++NumberOfCheckedDescriptors) < pDmaExt->NumberOfUsedDescriptors));

        if (NumberOfCheckedDescriptors >= pDmaExt->NumberOfUsedDescriptors) {
           KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Overran the ring"));
            return STATUS_UNSUCCESSFUL;
        }

        // If we exit the while loop then we have a completed packet
        *Completed = TRUE;
        return STATUS_SUCCESS;

    } else {
        // This is not necessarily an error. We could be looping past a completed packet to the next
        // DMA Descriptor that has not started to be DMA'd yet.
        return STATUS_SUCCESS;
    }
}

static NTSTATUS CompleteReceivedPacket(IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, IN PPACKET_RET_RECEIVE_STRUCT pRecvPacketRet)
{
    PDRIVER_DESC_STRUCT pDrvDesc;
    PDMA_DESCRIPTOR_STRUCT pHWDesc;
    UINT32 CachedDescStatus;
    NTSTATUS status = STATUS_DRIVER_INTERNAL_ERROR;
    LONG NumberOfCheckedDescriptors=0;

    pDrvDesc = pDmaExt->pNextDesc;
    pHWDesc = pDrvDesc->pHWDesc;

    // Walk the descriptors again looking for the EOP descriptor. It could be this descriptor
    do {
        // Save a copy of the status flags for later loop while test
        CachedDescStatus = pHWDesc->C2S.StatusFlags_BytesCompleted;

        // Get the bytes transfered before we clear this field below
        pRecvPacketRet->Length += (CachedDescStatus & PACKET_DESC_COMPLETE_BYTE_COUNT_MASK);

        // The descriptor is now "owned" by software and will not get it back until
        // the application does another PACKET_RECEIVE_IOCTL with this "token"
        // Indicate we processed this descriptor by clearing all but the SOP and EOP flags
        pHWDesc->C2S.StatusFlags_BytesCompleted &= (PACKET_DESC_C2S_STAT_START_OF_PACKET | PACKET_DESC_C2S_STAT_END_OF_PACKET);
        pDrvDesc->DescFlags = DESC_FLAGS_SW_OWNED;

        if (CachedDescStatus & PACKET_DESC_C2S_STAT_START_OF_PACKET) {
            pRecvPacketRet->RxToken = pDrvDesc->DescriptorNumber;
            pRecvPacketRet->Address = (UINT64)pDrvDesc->SystemAddressVirt;

            // Make sure we flush the processor(s) caches for this memory.
            // It should not be cached so this should take almost zero time.
            // This is strickly a precaution.
            KeFlushIoBuffers(pDmaExt->PMdl, TRUE, TRUE);
        }
        if (CachedDescStatus & PACKET_DESC_C2S_STAT_ERROR) {
            // Indicate a bad packet by zeroing out the address and Length fields
            pRecvPacketRet->Address = 0;
            pRecvPacketRet->Length = 0;
            pDrvDesc->DescFlags = DESC_FLAGS_SW_FREED;
           KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Received a bad packet"));
        }

        if (CachedDescStatus & PACKET_DESC_C2S_STAT_END_OF_PACKET) {

            // Return the EOP UserStatus to the application
            pRecvPacketRet->UserStatus = pHWDesc->C2S.UserStatus;

            // Make sure the return token is valid
            if (pRecvPacketRet->RxToken == INVALID_RELEASE_TOKEN) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Bad Token Return in Receive Process"));
            } else if (pRecvPacketRet->Address) {
                status = STATUS_SUCCESS;
            } else {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Found an error during the End of packet. The address is zero."));
            }
        }

        // Link to the next descriptor in the chain
        pDrvDesc = pDrvDesc->pNextDesc;
        pHWDesc = pDrvDesc->pHWDesc;

    } while ((!(CachedDescStatus & PACKET_DESC_C2S_STAT_END_OF_PACKET)) && ((++NumberOfCheckedDescriptors) < pDmaExt->NumberOfUsedDescriptors));

    if (NumberOfCheckedDescriptors >= pDmaExt->NumberOfUsedDescriptors) {
       KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Overran the ring"));
        status = STATUS_DRIVER_INTERNAL_ERROR;
    }

    // We consider this descriptor processed at this point, link to the next descriptor in the chain
    pDmaExt->pNextDesc = pDrvDesc;

    return status;
}

static void InitRecvPacket(PPACKET_RET_RECEIVE_STRUCT pRecvPacketRet)
{
    // Zero out the return length, address and set Token to -1
    pRecvPacketRet->Length = 0;
    pRecvPacketRet->Address = 0;
    pRecvPacketRet->RxToken = INVALID_RELEASE_TOKEN;
    pRecvPacketRet->UserStatus = 0;
}

/*! PacketProcessCompletedReceives
 *
 *  \brief This routine processes completed DMA Packet descriptors,
 *   dequeues requests and completes them
 *     \param pDevExt - Pointer to the driver context for this adapter
 *  \param pDmaExt - Pointer to the DMA Engine Context
 *     \return STATUS_SUCCESS if it works, an error if it fails.
 *     \note This routine must be called while protected by a spinlock
 */
NTSTATUS PacketProcessCompletedReceives(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
        PPACKET_RET_RECEIVE_STRUCT pRecvPacketRet;
        WDFREQUEST Request;
        size_t bufferSize;
        NTSTATUS status = STATUS_SUCCESS;

        UNREFERENCED_PARAMETER(pDevExt);

        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);

        //  Loop forever until we run out of PACKET_RECV_IOCTL requests or Completed DMA Descriptors
        // Make sure there is a Request waiting, if not just exit out.
        while (!WDF_IO_QUEUE_IDLE(WdfIoQueueGetState(pDmaExt->TransactionQueue, NULL, NULL))) {
                BOOLEAN Completed;

                // Stage 1: Make sure we have a completed DMA PAcket, i.e. both SOP and EOP completed descriptor(s)
                status = CheckForCompletedPacket(pDmaExt, &Completed);
                if (!NT_SUCCESS(status) || (Completed == FALSE)) {
                    goto PacketProcessCompletedReceivesExit;
                }

                // Stage 2: We have a completed packet. Now get the Request in the queue.

                // At this point we know we have an outstanding RECV request and a completed Packet
                status = WdfIoQueueRetrieveNextRequest(pDmaExt->TransactionQueue, &Request);
                if (NT_SUCCESS(status)) {
                        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(PACKET_RET_RECEIVE_STRUCT), &pRecvPacketRet, &bufferSize);
                        if (NT_SUCCESS(status)) {
                                InitRecvPacket(pRecvPacketRet);
                                status = CompleteReceivedPacket(pDmaExt, pRecvPacketRet);
                                WdfRequestCompleteWithInformation(Request, status, sizeof(PACKET_RET_RECEIVE_STRUCT));
                                if (NT_SUCCESS(status)) {
                                    pDmaExt->TimeoutCount = CARD_WATCHDOG_INTERVAL; 
                                }
                        } else  // WdfRequestRetrieveOutputBuffer(Request... failed
                        {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfRequestRetrieveOutputBuffer failed, status:0x%x", status));
                                WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, sizeof(PACKET_RET_RECEIVE_STRUCT));
                                // Status already set to Not SUCCESS, go around again to see if there is
                                // another Request pending.
                        }
                } else          // WdfIoQueueRetrieveNextRequest(pDmaExt->RxPendingQueue...  failed
                {
                        // Status already set to Not SUCCESS, exit the while loop
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Queue failed, Pending Queue empty!"));
                        break;
                }
        }

PacketProcessCompletedReceivesExit:

        WdfSpinLockRelease(pDmaExt->DmaSpinLock);
        return status;
}

/*! PacketProcessCompletedReceiveNB
*
*  \brief This routine processes completed DMA Packet descriptors
*   and completes them
*  \param pDmaExt - Pointer to the DMA Engine Context
*     \return STATUS_SUCCESS if it works, an error if it fails.
*     \note This routine must be called while protected by a spinlock
*/
NTSTATUS PacketProcessCompletedReceiveNB(IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, WDFREQUEST Request)
{
        PPACKET_RET_RECEIVE_STRUCT pRecvPacketRet;
        size_t bufferSize;
        NTSTATUS status;

        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);

        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(PACKET_RET_RECEIVE_STRUCT), &pRecvPacketRet, &bufferSize);
        if (NT_SUCCESS(status)) {
                BOOLEAN Completed;

                InitRecvPacket(pRecvPacketRet);

                // Stage 1: Make sure we have a completed DMA PAcket, i.e. both SOP and EOP completed descriptor(s)
                status = CheckForCompletedPacket(pDmaExt, &Completed);
                if (!NT_SUCCESS(status) || (Completed == FALSE)) {
                    goto PacketProcessCompletedReceiveNBExit;
                }

                // Stage 2: We have a completed packet.
                status = CompleteReceivedPacket(pDmaExt, pRecvPacketRet);
                if (NT_SUCCESS(status)) {
                    pDmaExt->TimeoutCount = CARD_WATCHDOG_INTERVAL; 
                }
        } else                  // WdfRequestRetrieveOutputBuffer(Request... failed
        {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "WdfRequestRetrieveOutputBuffer failed, status:0x%x", status));
                status = STATUS_INVALID_PARAMETER;
        }

PacketProcessCompletedReceiveNBExit:

        WdfRequestCompleteWithInformation(Request, status, sizeof(PACKET_RET_RECEIVE_STRUCT));
        WdfSpinLockRelease(pDmaExt->DmaSpinLock);
        return STATUS_SUCCESS;
}

/*! PacketProcessReturnedDescriptors
 *
 *    \brief This routine processes the returned
 *      Packet descriptors and updates the pointer if appropriate
 *     \param pDevExt - Pointer to the driver context for this adapter
 *  \param pDmaExt - Pointer to the DMA Engine Context
 *  \param ReturnToken - Token (Index) for the starting DMA Descriptor to be
 *     recycled back to the DMA Engine to be re-used
 *  \return STATUS_SUCCESS if it works, an error if it fails.
 */
NTSTATUS PacketProcessReturnedDescriptors(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, IN UINT32 ReturnToken)
{
        PDRIVER_DESC_STRUCT pDrvDesc;
        PDRIVER_DESC_STRUCT pPrevDrvDesc;
        PDMA_DESCRIPTOR_STRUCT pHWDesc;
        UINT32 DescriptorState = DESC_FLAGS_HW_OWNED;
        UINT32 CachedDescStatus = 0;
        NTSTATUS status = STATUS_SUCCESS;
        LONG NumberOfCheckedDescriptors=0;

        UNREFERENCED_PARAMETER(pDevExt);

        // We only want one thread processing descriptors at a time.
        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);

        // Stage 1: Determine if this token is at the tail or will create a 'hole' in the list
        // Get the descriptor that is at the tail of the queue
        pDrvDesc = pDmaExt->pTailDesc->pNextDesc;
        pHWDesc = pDrvDesc->pHWDesc;
        if (pDrvDesc->DescFlags != DESC_FLAGS_SW_OWNED) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Returned Token %d is NOT owned by Software (Flags:0x%x)", ReturnToken, pDrvDesc->DescFlags));
                status = STATUS_INVALID_PARAMETER;
                goto PacketProcessReturnedDescriptorsExit;
        }
        if ((pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_C2S_STAT_START_OF_PACKET) != PACKET_DESC_C2S_STAT_START_OF_PACKET) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Descriptor at Token %d is not SOP", ReturnToken));
                status = STATUS_INVALID_PARAMETER;
                goto PacketProcessReturnedDescriptorsExit;
        }

        pPrevDrvDesc = pDrvDesc;

        // See if the Returned decscriptor (token) is next in line.
        if (pDrvDesc->DescriptorNumber != ReturnToken) {
                // In this case we are not at the tail, hence we just "free" the descriptor
                // Get the Descriptor at the Token Index into the descriptor array
                pDrvDesc = &pDmaExt->pDrvDescBase[ReturnToken];
                pHWDesc = pDrvDesc->pHWDesc;
                // Make sure the Token matches the Desciptor
                if (pDrvDesc->DescriptorNumber == ReturnToken) {
                        if ((pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_C2S_STAT_START_OF_PACKET) != PACKET_DESC_C2S_STAT_START_OF_PACKET) {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Descriptor at Return Token %d is not SOP", ReturnToken));
                                status = STATUS_INVALID_PARAMETER;
                                goto PacketProcessReturnedDescriptorsExit;
                        }
                        DescriptorState = DESC_FLAGS_SW_FREED;
                } else {
                        // Log error and exit
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "DMA Engine %d, Descriptor %d number does not match ReturnToken %d", pDmaExt->DmaEngine, pDrvDesc->DescriptorNumber, ReturnToken));
                        status = STATUS_INVALID_PARAMETER;
                        goto PacketProcessReturnedDescriptorsExit;
                }
        }
        // Stage 2: Walk the list starting at the token and either Free or mark for HW the descriptor(s)
        // Make sure the token is the start of the packet. If not it is either an app error or something worse
        if (pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_C2S_STAT_START_OF_PACKET) {
                CachedDescStatus = pHWDesc->C2S.StatusFlags_BytesCompleted;
                // Walk the descriptors looking for the EOP descriptor. It could be this descriptor
                do {
                        if (pDrvDesc->DescFlags != DESC_FLAGS_HW_OWNED) {
                                // We can change ownership back to hardware since we sill be setting the SwDescPtr
                                pDrvDesc->DescFlags = DescriptorState;
                                // Save a copy of the status flags for later loop while test
                                CachedDescStatus = pHWDesc->C2S.StatusFlags_BytesCompleted;
                                // Clear the status just in case
                                pHWDesc->C2S.StatusFlags_BytesCompleted = 0;
                                // Cache the current Descriptor
                                pPrevDrvDesc = pDrvDesc;
                                // Link to the next descriptor
                                pDrvDesc = pDrvDesc->pNextDesc;
                                pHWDesc = pDrvDesc->pHWDesc;
                        } else {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Returned Token %d descriptor is owned by hardware (Flags:0x%x)", ReturnToken, pDrvDesc->DescFlags));
                                status = STATUS_INVALID_PARAMETER;
                                goto PacketProcessReturnedDescriptorsExit;
                        }
                } while ((!(CachedDescStatus & PACKET_DESC_C2S_STAT_END_OF_PACKET)) && ((++NumberOfCheckedDescriptors) < pDmaExt->NumberOfUsedDescriptors));

                if (NumberOfCheckedDescriptors >= pDmaExt->NumberOfUsedDescriptors) {
                   KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Overran the receive ring"));
                    status = STATUS_DRIVER_INTERNAL_ERROR;
                    goto PacketProcessReturnedDescriptorsExit;
                }

        } else {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Returned Token %d Descriptor missing SOP", ReturnToken));
                status = STATUS_DRIVER_INTERNAL_ERROR;
                goto PacketProcessReturnedDescriptorsExit;
        }

        // Stage 3: If not a 'freed hole' then advance the list, look for freed descriptors ahead first
        if (DescriptorState == DESC_FLAGS_HW_OWNED) {
                // pDesc is pointing to the descriptor following the EOP
                // Now see if there are any previously 'freed' descriptors ahead of us.
                NumberOfCheckedDescriptors = 0;
                while ((pDrvDesc->DescFlags == DESC_FLAGS_SW_FREED) && ((++NumberOfCheckedDescriptors) < pDmaExt->NumberOfUsedDescriptors)) {
                        // And mark it as HW Owned
                        pDrvDesc->DescFlags = DESC_FLAGS_HW_OWNED;
                        // Cache the current Descriptor
                        pPrevDrvDesc = pDrvDesc;
                        // Link to the next descriptor
                        pDrvDesc = pDrvDesc->pNextDesc;
                        pHWDesc = pDrvDesc->pHWDesc;
                }

                if (NumberOfCheckedDescriptors >= pDmaExt->NumberOfUsedDescriptors) {
                   KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Overran the receive ring"));
                    status = STATUS_DRIVER_INTERNAL_ERROR;
                    goto PacketProcessReturnedDescriptorsExit;
                }

                // Set the last descriptor as the new end and update the Tail pointer
                pDmaExt->pDmaEng->SoftwareDescriptorPtr = pPrevDrvDesc->pHWDescPhys.LowPart;
                pDmaExt->pTailDesc = pPrevDrvDesc;
        }

PacketProcessReturnedDescriptorsExit:
        WdfSpinLockRelease(pDmaExt->DmaSpinLock);
        return status;
}

// Addressable Packet Mode functions
/*! PacketReadComplete
 *
 * \brief This routine processes completed DMA Packet descriptors, dequeues
 *  requests and completes them
 * \param pDpcCtx - Pointer to the driver context for this Dpc
 * \return nothing
 */
NTSTATUS PacketReadComplete(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
        PDRIVER_DESC_STRUCT pDrvDesc;
        PDMA_DESCRIPTOR_STRUCT pHWDesc;
        PDMA_XFER pDmaXfer = NULL;
        WDFREQUEST Request;
        PPACKET_RET_READ_STRUCT pReadRetPacket;
        size_t ReadRetPacketSize = 0;
        BOOLEAN transactionComplete;
        NTSTATUS status = STATUS_SUCCESS;

        UNREFERENCED_PARAMETER(pDevExt);

        // We only want one thread processing recieves at a time.
        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);

        // Make sure we have completed descriptor(s)
        pDrvDesc = pDmaExt->pTailDesc;
        pHWDesc = pDrvDesc->pHWDesc;
       KdPrintEx((1, DPFLTR_INFO_LEVEL, "PacketReadComplete Desc [%p] status 0x%x", pDrvDesc, pHWDesc->C2S.StatusFlags_BytesCompleted));
        while ((pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_C2S_STAT_COMPLETE) || (pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_C2S_STAT_ERROR)) {
                // At this point we know we have a completed Send DMA Descriptor
                // Update the contexts links to the next descriptor
                pDmaExt->pTailDesc = pDrvDesc->pNextDesc;

                if (pDrvDesc->DmaTransaction != NULL) {
                        _InterlockedDecrement(&pDmaExt->NumberOfUsedDescriptors);

                        // The Transaction data pointer is in every decriptor for a given Request
                        pDmaXfer = DMAXferContext(pDrvDesc->DmaTransaction);
                        if (pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_C2S_STAT_START_OF_PACKET) {
                                pDmaXfer->bytesTransferred = 0;
                        }
                        pDmaXfer->bytesTransferred += (pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_COMPLETE_BYTE_COUNT_MASK);
                        pDmaXfer->PacketStatus |= (pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_S2C_STAT_ERROR);

                        if (pHWDesc->C2S.StatusFlags_BytesCompleted & PACKET_DESC_C2S_STAT_END_OF_PACKET) {
                                pDmaXfer->UserControl = pHWDesc->C2S.UserStatus;
                                if (pDmaXfer->PacketStatus) {
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "DMADriver Error: Control/Status returned error"));
                                        // Stop the current DMA transfer, tell driver to not continue
                                        transactionComplete = WdfDmaTransactionDmaCompletedFinal(pDrvDesc->DmaTransaction, pDmaXfer->bytesTransferred, &status);
                                        status = STATUS_ADAPTER_HARDWARE_ERROR;
                                } else {
                                        status = STATUS_SUCCESS;
                                        // complete the transaction,  transaction completed successfully
                                        // tell the framework that this DMA set is complete
                                        transactionComplete = WdfDmaTransactionDmaCompletedWithLength(pDrvDesc->DmaTransaction, pDmaXfer->bytesTransferred, &status);
                                }

                                // Is the full transaction complete?
                                if (transactionComplete) {
                                   KdPrintEx((1, DPFLTR_INFO_LEVEL, "Transaction Complete, size=%lu", (UINT32) pDmaXfer->bytesTransferred));

                                        if (pDmaXfer->pMdl != NULL) {
                                                MmUnlockPages(pDmaXfer->pMdl);
                                                IoFreeMdl(pDmaXfer->pMdl);
                                                pDmaXfer->pMdl = NULL;
                                        } else {
                                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketReadComplete: pMDL = NULL"));
                                        }

                                        // Retrieve the originating request from the Transaction data extension
                                        Request = FindRequestByRequest(pDmaExt, pDmaXfer->Request);
                                        if (Request != NULL) {
                                                // get the output buffer pointer
                                                status = WdfRequestRetrieveOutputBuffer(Request, (size_t) sizeof(PPACKET_RET_READ_STRUCT),      // Min size
                                                                                        (PVOID *) & pReadRetPacket, &ReadRetPacketSize);
                                                if (NT_SUCCESS(status)) {
                                                        if (pReadRetPacket != NULL) {
                                                                if (ReadRetPacketSize >= sizeof(PPACKET_RET_READ_STRUCT)) {
                                                                        pReadRetPacket->Length = (UINT32) pDmaXfer->bytesTransferred;
                                                                        pReadRetPacket->UserStatus = pDmaXfer->UserControl;
                                                                }
                                                        }
                                                        // complete the transaction
                                                        WdfRequestCompleteWithInformation(Request, status, ReadRetPacketSize);
                                                }
                                        }
                                        // Release the Transaction record
                                        WdfDmaTransactionRelease(pDrvDesc->DmaTransaction);
                                        pDmaXfer->Request = NULL;
                                        WdfObjectDelete(pDrvDesc->DmaTransaction);
                                        pDmaXfer = NULL;
                                }
                        }
                }
                // Indicate we processed this descriptor by clearing Complete and Error flags
                pDrvDesc->DmaTransaction = NULL;
                pHWDesc->C2S.StatusFlags_BytesCompleted = 0;

                // Link to the next packets
                pDrvDesc = pDrvDesc->pNextDesc; // Link to the next descriptor in the chain
                pHWDesc = pDrvDesc->pHWDesc;
        }
        WdfSpinLockRelease(pDmaExt->DmaSpinLock);

        return STATUS_SUCCESS;
}

/*! PacketReadRequestCancel
 *
 *     \brief - Cancels a waiting Packet Read request.
 *  \param Request - WDF Request
 *  \return nothing
 */
VOID PacketReadRequestCancel(IN WDFQUEUE Queue, IN WDFREQUEST Request)
{
        PQUEUE_CTX pQueueCtx = QueueContext(Queue);
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt = pQueueCtx->pDmaExt;
        WDF_REQUEST_PARAMETERS Params;
        PDRIVER_DESC_STRUCT pDrvDesc;
        PDMA_DESCRIPTOR_STRUCT pHWDesc;
        PDMA_XFER pDmaXfer;
        WDFREQUEST CancelRequest;
        NTSTATUS status;

       KdPrintEx((1, DPFLTR_WARNING_LEVEL, "In function PacketReadRequestCancel"));

        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);

        // Shut down the DMA Engine.
        pDmaExt->pDmaEng->ControlStatus = 0;

        WDF_REQUEST_PARAMETERS_INIT(&Params);
        WdfRequestGetParameters(Request, &Params);

        if (Params.Parameters.DeviceIoControl.IoControlCode == PACKET_READ_IOCTL) {
                pDrvDesc = pDmaExt->pTailDesc;
                pHWDesc = pDrvDesc->pHWDesc;
                while (pDrvDesc != pDmaExt->pNextDesc) {
                        // Update the contexts links to the next descriptor
                        pDmaExt->pTailDesc = pDrvDesc->pNextDesc;
                        if (pDrvDesc->DmaTransaction != NULL) {
                                // The Transaction data pointer is in every decriptor for a given Request
                                pDmaXfer = DMAXferContext(pDrvDesc->DmaTransaction);
                                if (pDmaXfer != NULL) {
                                        if (pHWDesc->C2S.ControlFlags_ByteCount & PACKET_DESC_C2S_CTRL_END_OF_PACKET) {
                                                status = STATUS_CANCELLED;
                                                WdfDmaTransactionDmaCompletedFinal(pDrvDesc->DmaTransaction, 0, &status);
                                                if (pDmaXfer->pMdl != NULL) {
                                                        MmUnlockPages(pDmaXfer->pMdl);
                                                        IoFreeMdl(pDmaXfer->pMdl);
                                                        pDmaXfer->pMdl = NULL;
                                                } else {
                                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "PacketReadRequestCancel: pMDL = NULL"));
                                                }
                                                if (Request != pDmaXfer->Request) {
                                                        CancelRequest = FindRequestByRequest(pDmaExt, pDmaXfer->Request);
                                                        if (CancelRequest != NULL) {
                                                                // complete the transaction
                                                                WdfRequestCompleteWithInformation(CancelRequest, STATUS_CANCELLED, 0);
                                                        }
                                                }
                                                // Release the Transaction record
                                                pDmaXfer->Request = NULL;
                                                WdfDmaTransactionRelease(pDrvDesc->DmaTransaction);
                                                WdfObjectDelete(pDrvDesc->DmaTransaction);
                                                pDmaXfer = NULL;
                                        }
                                }
                        }       // if (pDesc->Packet.C2S.DmaTransaction...

                        _InterlockedDecrement(&pDmaExt->NumberOfUsedDescriptors);
                        // Clear the byte count and the SOP and EOP
                        pHWDesc->C2S.ControlFlags_ByteCount = 0;
                        pHWDesc->C2S.StatusFlags_BytesCompleted = PACKET_DESC_C2S_STAT_ERROR;
                        pDrvDesc->DmaTransaction = NULL;
                        pDrvDesc->pScatterGatherList = 0;

                        pDrvDesc = pDrvDesc->pNextDesc; // Link to the next descriptor in the chain
                        pHWDesc = pDrvDesc->pHWDesc;
                }               // while (pDesc != pDmaExt->pNextDesc)
        }
        FindRequestByRequest(pDmaExt, Request);
        // complete the transaction
        WdfRequestCompleteWithInformation(Request, STATUS_CANCELLED, 0);

        WdfSpinLockRelease(pDmaExt->DmaSpinLock);
        return;
}

static BOOLEAN PacketProcessCompletedFreeRunDescriptorsFindEOP(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
    PDRIVER_DESC_STRUCT pDrvDesc;
    PDMA_DESCRIPTOR_STRUCT pHWDesc;
    UINT32 StatusFlags_BytesCompleted;

    pDrvDesc = pDmaExt->pNextDesc;
    pHWDesc = pDrvDesc->pHWDesc;

    /*
     * Don't go around the descriptor ring more then once.
     */
    do
    {
        StatusFlags_BytesCompleted = pHWDesc->C2S.StatusFlags_BytesCompleted;
        if (!(StatusFlags_BytesCompleted & (PACKET_DESC_C2S_STAT_COMPLETE | PACKET_DESC_C2S_STAT_ERROR))) {
            return FALSE;
        }
        if (StatusFlags_BytesCompleted & PACKET_DESC_C2S_STAT_END_OF_PACKET)
        {
            return TRUE;
        }

        pDrvDesc = pDrvDesc->pNextDesc; 
        pHWDesc = pDrvDesc->pHWDesc;

    } while (pDrvDesc != pDmaExt->pNextDesc);

    return FALSE;
}

/*
 * Setting the IRQ bits in a HW descriptor will cause an interrupt if the
 * DMA engine processes that descriptor. When this happens you know there has been a
 * DMA overrun. The detection algorithm is to set the IRQ bits in the first descriptor
 * of a group of packets, then clear them the next time PacketRecvsIOCtlHandler() is
 * called to process a packet. Wash rinse repeat...
*/
#define PACKET_RECVS_IRQ_BITS (PACKET_DESC_S2C_CTRL_IRQ_ON_COMPLETE | PACKET_DESC_S2C_CTRL_IRQ_ON_ERROR)

static void PacketProcessCompletedFreeRunDescriptorsClearIRQ(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
    if (pDmaExt->pIrqDesc)
    {
        pDmaExt->pIrqDesc->pHWDesc->C2S.ControlFlags_ByteCount &= (~PACKET_RECVS_IRQ_BITS);
        pDmaExt->pIrqDesc = NULL;
    }
}

static void PacketProcessCompletedFreeRunDescriptorsSetIRQ(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, PDRIVER_DESC_STRUCT pDrvDesc)
{
    PacketProcessCompletedFreeRunDescriptorsClearIRQ(pDmaExt);

    pDrvDesc->pHWDesc->C2S.ControlFlags_ByteCount |= PACKET_RECVS_IRQ_BITS;
    pDmaExt->pIrqDesc = pDrvDesc;
}

static void PacketProcessCompletedFreeRunDescriptorsResetLastIRQ(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
    if (pDmaExt->pIrqDesc && pDmaExt->pIrqDesc->pNextDesc != pDmaExt->pNextDesc)
    {
        PDRIVER_DESC_STRUCT pDrvDesc = pDmaExt->pIrqDesc;
        while (pDrvDesc->pNextDesc != pDmaExt->pNextDesc)
        {
            pDrvDesc = pDrvDesc->pNextDesc;
        }
        PacketProcessCompletedFreeRunDescriptorsSetIRQ(pDmaExt, pDrvDesc);
    }
}

// Free Run FIFO Packet Mode functions
/*! PacketProcessCompletedFreeRunDescriptors
 *
 * \brief This routine processes completed DMA Packet descriptors, dequeues
 *  requests and completes them
 *
 *     \param pDevExt - Pointer to the driver context for this adapter
 *  \param pDmaExt - Pointer to the DMA Engine Context
 *  \return STATUS_SUCCESS if it works, an error if it fails.
 *
 *  \note This routine must be called while protected by a spinlock
 */
NTSTATUS PacketProcessCompletedFreeRunDescriptors(IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, PPACKET_RECVS_STRUCT pPacketRecvs)
{
        PDRIVER_DESC_STRUCT pDrvDesc;
        PDRIVER_DESC_STRUCT pPrevDrvDesc = NULL;
        PDMA_DESCRIPTOR_STRUCT pHWDesc;
        UINT32 CachedDescStatus = 0;
        NTSTATUS status = STATUS_SUCCESS;
        LONG NumberOfCheckedDescriptors=0;

        WdfSpinLockAcquire(pDmaExt->DmaSpinLock);

        pPacketRecvs->RetNumEntries = 0;
        pPacketRecvs->EngineStatus = pDmaExt->DMAEngineStatus;
        pDmaExt->DMAEngineStatus = 0;
        // Loop to file out the packet receives.
        while (pPacketRecvs->RetNumEntries < pPacketRecvs->AvailNumEntries) {
                // Zero out the return length, address, etc
                pPacketRecvs->Packets[pPacketRecvs->RetNumEntries].Length = 0;
                pPacketRecvs->Packets[pPacketRecvs->RetNumEntries].Address = 0;
                pPacketRecvs->Packets[pPacketRecvs->RetNumEntries].Status = PACKET_ERROR_MALFORMED;
                pPacketRecvs->Packets[pPacketRecvs->RetNumEntries].UserStatus = 0;

                if (PacketProcessCompletedFreeRunDescriptorsFindEOP(pDmaExt) != TRUE)
                {
                    /*
                     * We can usually assume the previous packets have been processed by the application
                     * and can now be released from DMA overrun detection. Note that this may not be an accurate 
                     * assumption if there are multiple threads calling this IOCTL handler.
                     */
                    if (pPacketRecvs->RetNumEntries == 0)
                    {
                        PacketProcessCompletedFreeRunDescriptorsResetLastIRQ(pDmaExt);
                    }
                    goto PacketProcessCompletedFreeRunDescriptorsExit;
                }

                /*
                 * If this is the first loop, and there is an EOP, then set the IRQ bits to detect
                 * DMA overrun.
                 */
                if (pPacketRecvs->RetNumEntries == 0)
                {
                    PacketProcessCompletedFreeRunDescriptorsSetIRQ(pDmaExt, pDmaExt->pNextDesc);
                }

                pDrvDesc = pDmaExt->pNextDesc;;
                pHWDesc = pDrvDesc->pHWDesc;
                // Walk the descriptors again retrieving length, addressm etc. and looking for
                //   the EOP descriptor, it could be this one.
                do {
                        CachedDescStatus = pHWDesc->C2S.StatusFlags_BytesCompleted;
                        // Make sure we have a completed DMA Descriptor.
                        if (CachedDescStatus & (PACKET_DESC_C2S_STAT_COMPLETE | PACKET_DESC_C2S_STAT_ERROR)) {
                                // Get the bytes transfered before we clear this field below
                                pPacketRecvs->Packets[pPacketRecvs->RetNumEntries].Length += (CachedDescStatus & PACKET_DESC_COMPLETE_BYTE_COUNT_MASK);
                                pHWDesc->C2S.StatusFlags_BytesCompleted = 0;
                                if (CachedDescStatus & PACKET_DESC_C2S_STAT_START_OF_PACKET) {
                                        pPacketRecvs->Packets[pPacketRecvs->RetNumEntries].Address = (UINT64) pDrvDesc->SystemAddressVirt;
                                        if (pDmaExt->PMdl != NULL) {
                                                // Make sure we flush the processor(s) caches for this memory.
                                                // Should not be necessary, it is just a precaution.
                                                KeFlushIoBuffers(pDmaExt->PMdl, TRUE, TRUE);
                                        }
                                }
                                if (CachedDescStatus & PACKET_DESC_C2S_STAT_ERROR) {
                                        pPacketRecvs->Packets[pPacketRecvs->RetNumEntries].Status = CachedDescStatus & (PACKET_DESC_C2S_STAT_ERROR | PACKET_DESC_C2S_STAT_SHORT);
                                }
                                if (CachedDescStatus & PACKET_DESC_C2S_STAT_END_OF_PACKET) {
                                        // Return the EOP UserStatus to the application
                                        pPacketRecvs->Packets[pPacketRecvs->RetNumEntries].UserStatus = pHWDesc->C2S.UserStatus;
                                        // Since we found the EOP remove the Malformed packet indicator.
                                        pPacketRecvs->Packets[pPacketRecvs->RetNumEntries].Status &= ~PACKET_ERROR_MALFORMED;
                                        pPacketRecvs->RetNumEntries++;
                                }
                                // Mark the descriptor as HW Owned
                                pDrvDesc->DescFlags = DESC_FLAGS_HW_OWNED;
                                // Cache the current Descriptor
                                pPrevDrvDesc = pDrvDesc;
                                // Link to the next descriptor
                                pDrvDesc = pDrvDesc->pNextDesc;
                                pHWDesc = pDrvDesc->pHWDesc;
                        } else  // This descriptor is not complete and it should be, exit out.
                        {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "Packet is not complete, exiting"));
                                status = STATUS_DRIVER_INTERNAL_ERROR;
                                goto PacketProcessCompletedFreeRunDescriptorsExit;
                        }
                } while ((!(CachedDescStatus & PACKET_DESC_C2S_STAT_END_OF_PACKET)) && ((++NumberOfCheckedDescriptors) < pDmaExt->NumberOfUsedDescriptors));

                if (NumberOfCheckedDescriptors >= pDmaExt->NumberOfUsedDescriptors) {
                   KdPrintEx((1, DPFLTR_ERROR_LEVEL,"Overran the ring"));
                    status = STATUS_DRIVER_INTERNAL_ERROR;
                    goto PacketProcessCompletedFreeRunDescriptorsExit;
                }

                // We consider this Packet processed at this point, link to the next descriptor in the chain
                pDmaExt->pNextDesc = pDrvDesc;

                if (pDmaExt->PacketMode == PACKET_MODE_FIFO) {
                        if (pPrevDrvDesc != NULL) {
                                // Set the last descriptor as the new end and update the Tail pointer
                                pDmaExt->pDmaEng->SoftwareDescriptorPtr = pPrevDrvDesc->pHWDescPhys.LowPart;
                                pDmaExt->pTailDesc = pPrevDrvDesc;
                        }
                }
        }                       // while more packets available to return in the struct...

PacketProcessCompletedFreeRunDescriptorsExit:
        WdfSpinLockRelease(pDmaExt->DmaSpinLock);
        return status;
}
