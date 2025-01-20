// -------------------------------------------------------------------------
//
// PRODUCT:            DMA Driver
// MODULE NAME:        IrqHandling.c
//
// MODULE DESCRIPTION:
//
// Contains the code for Interrupt Handling for the PCI Express DMA Driver.
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
#include "IrqHandling.tmh"
#endif                          // TRACE_ENABLED

/*! DMADriverInterruptCreate
 *
 * \brief Create and Initialize the Interrupt object
 *  This is called (indirectly) from the DMADriverEvtDeviceAdd.
 * \param pDevExt
 * \return status
 */
NTSTATUS DMADriverInterruptCreate(IN PDEVICE_EXTENSION pDevExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        WDF_INTERRUPT_CONFIG interruptConfig;
        UINT32 i;

       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL --> WdfInterruptCreate"));
        pDevExt->NumIRQVectors = 0;

        if (((pDevExt->MSIXSupported && (pDevExt->MSIXOverride == TRUE)) || ((pDevExt->MSISupported && (pDevExt->MSIOverride == TRUE)))) && (pDevExt->MSINumberVectors > 1)) {
			KdPrintEx((1, DPFLTR_ERROR_LEVEL,"USL Setting up %u MSI vectors", pDevExt->MSINumberVectors));
			if (pDevExt->MSINumberVectors > MAX_NUM_DMA_ENGINES) {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Too many MSI vectors %u, max allowed is %u", pDevExt->MSINumberVectors, MAX_NUM_DMA_ENGINES));
                        return STATUS_INVALID_PARAMETER;
                }
                for (i = 0; i < pDevExt->MSINumberVectors; i++) {
                        // setup interrupt config structure with ISR and DPC pointers
                        WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, DMADriverInterruptIsr, NULL);

                        // Setup Interrupt enable/disable routine callbacks
                        interruptConfig.EvtInterruptEnable = DMADriverInterruptEnable;
                        interruptConfig.EvtInterruptDisable = DMADriverInterruptDisable;

                        // Create interrupt object
                        status = WdfInterruptCreate(pDevExt->Device, &interruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &pDevExt->Interrupt[i]);
                        if (!NT_SUCCESS(status)) {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- WdfInterruptCreate failed  0x%x", status));
                                break;
                        }

                        pDevExt->NumIRQVectors++;
                }

                // User Interrupt
                WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, DMADriverInterruptUserIRQMSIIsr, NULL);

                // Create interrupt object
                status = WdfInterruptCreate(pDevExt->Device, &interruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &pDevExt->Interrupt[pDevExt->NumIRQVectors]);
                if (!NT_SUCCESS(status)) {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- WdfInterruptCreate failed  0x%x\n", status));
                }
                pDevExt->NumIRQVectors++;
                // End User Interrupt
        } else {
			KdPrintEx((1, DPFLTR_ERROR_LEVEL,"USL Setting up single interrupt handler\n"));
			// setup interrupt config structure with ISR and DPC pointers
                WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, DMADriverInterruptIsr, NULL);

                // Setup Interrupt enable/disable routine callbacks
                interruptConfig.EvtInterruptEnable = DMADriverInterruptEnable;
                interruptConfig.EvtInterruptDisable = DMADriverInterruptDisable;

                // Create interrupt object
                status = WdfInterruptCreate(pDevExt->Device, &interruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &pDevExt->Interrupt[0]);
                if (!NT_SUCCESS(status)) {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- WdfInterruptCreate failed  0x%x", status));
                        return status;
                }
                pDevExt->NumIRQVectors++;
        }

        // Make sure we allocated at least one Interrupt vector
        if (pDevExt->NumIRQVectors) {
                status = STATUS_SUCCESS;
        }
		KdPrintEx((1, DPFLTR_ERROR_LEVEL,"USL Number of interrupt vectors  %d \n", pDevExt->NumIRQVectors));
		return status;
}

static BOOLEAN CheckForUserInterrupt(PDEVICE_EXTENSION pDevExt, BOOLEAN IsOurInterrupt)
{

    if ((pDevExt->pDmaRegisters->commonControl.ControlStatus & CARD_USER_INTERRUPT_MASK) == CARD_USER_INTERRUPT_MASK) {

       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL user interrupt request %p", pDevExt->UsrIntRequest));

        // Let the DPC complete the operation.
        WdfDpcEnqueue(pDevExt->UsrIrqDpc);

        IsOurInterrupt = TRUE;
    }

    return(IsOurInterrupt);
}

VOID DMADriverAckDmaInterrupt(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
    // Acknowledge interrupts for this DMA engine
    pDmaExt->pDmaEng->ControlStatus |= (COMMON_DMA_CTRL_IRQ_ACTIVE | COMMON_DMA_CTRL_IRQ_ENABLE | PACKET_DMA_CTRL_DMA_ENABLE);
}

static BOOLEAN DMADriverHandleInterrupt(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
    UINT32 Status;

    Status = pDmaExt->pDmaEng->ControlStatus & (COMMON_DMA_CTRL_IRQ_ACTIVE | COMMON_DMA_CTRL_IRQ_ENABLE);

	if (Status == (COMMON_DMA_CTRL_IRQ_ACTIVE | COMMON_DMA_CTRL_IRQ_ENABLE)) {
 //      KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Interrupt for DMA Channel %u, status %08x", pDmaExt->DmaEngine, pDmaExt->pDmaEng->Controlstatus));

        // Increment the Interrupt Count
        pDmaExt->IntsInLastSecond++;

        WdfDpcEnqueue(pDmaExt->CompletionDpc);
        return TRUE;
    }

    return FALSE;
}

static BOOLEAN DMADriverHandleMsiInterrupt(PDEVICE_EXTENSION pDevExt, unsigned long MessageID)
{
    PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;

    if (MessageID >= MAX_NUM_DMA_ENGINES) {
       KdPrintEx((1, DPFLTR_ERROR_LEVEL,"USL MessageID %u >= %u", MessageID, MAX_NUM_DMA_ENGINES));
        return FALSE;
    }

    pDmaExt = pDevExt->pDmaExtMSIVector[MessageID];

    if (pDmaExt != NULL) {
        if (pDmaExt->DMAEngineMSIVector != MessageID) {
           KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Bogus MSI vector (%u), expected %u", MessageID, pDmaExt->DMAEngineMSIVector));
            return FALSE;
        }
		return DMADriverHandleInterrupt(pDmaExt);
    }
    else {
		return CheckForUserInterrupt(pDevExt, FALSE);
    }
}

/*! DMADriverInterruptIsr
 *
 * \brief Interrupt Handler.
 *  Determine if this is our interrupt.  If so, save the status and schedule a DPC.
 * \param Interrupt
 * \param MessageID
 * \return IsOurInterrupt
 */
BOOLEAN DMADriverInterruptIsr(IN WDFINTERRUPT Interrupt, IN unsigned long MessageID)
{
    PDEVICE_EXTENSION pDevExt;
    UINT32 dmaEngine;
    BOOLEAN IsOurInterrupt = FALSE;

    // Get Device Extensions
    pDevExt = DMADriverGetDeviceContext(WdfInterruptGetDevice(Interrupt));

    if (pDevExt->NumIRQVectors > 1) {
//		return DMADriverHandleMsiInterrupt(pDevExt, MessageID);  //????****
		DMADriverHandleMsiInterrupt(pDevExt, MessageID);
	}
    
    // search for active dmaEngine
    for (dmaEngine = 0; dmaEngine < MAX_NUM_DMA_ENGINES; dmaEngine++) {
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;

        pDmaExt = pDevExt->pDmaEngineDevExt[dmaEngine];
        if (pDmaExt != NULL) {
            IsOurInterrupt = DMADriverHandleInterrupt(pDmaExt);
            if (IsOurInterrupt == TRUE) {
                break; 
            }
        }
    }

    return CheckForUserInterrupt(pDevExt, IsOurInterrupt);
}

BOOLEAN DMADriverInterruptUserIRQMSIIsr(IN WDFINTERRUPT Interrupt, IN unsigned long MessageID)
{
        PDEVICE_EXTENSION pDevExt;
        UNREFERENCED_PARAMETER(MessageID);

        // Get Device Extensions
        pDevExt = DMADriverGetDeviceContext(WdfInterruptGetDevice(Interrupt));

       KdPrintEx((1, DPFLTR_ERROR_LEVEL, ""));

        return CheckForUserInterrupt(pDevExt, FALSE);
}

/*! DMADriverInterruptEnable
 *
 * \brief Called at DIRQL after EvtDeviceD0Entry returns or
 *  when WdfInterruptEnable is called.
 *  The interrupt spinlock is being held during this call.
 * \note This enables global device interrupts.
 *  DMA Engine interrupts are not enabled at this point.
 * \param Interrupt
 * \param Device
 * \return status
 */
NTSTATUS DMADriverInterruptEnable(IN WDFINTERRUPT Interrupt, IN WDFDEVICE Device)
{
        PDEVICE_EXTENSION pDevExt = DMADriverGetDeviceContext(Device);
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
        UINT32 i;

        /*
         * The user interrupt does not have an associated DMA engine.
        */
        for (i=0; i<MAX_NUM_DMA_ENGINES; i++) {
            pDmaExt = pDevExt->pDmaEngineDevExt[i];
            if (pDmaExt && pDmaExt->Interrupt == Interrupt) {
                pDmaExt->pDmaEng->ControlStatus |= COMMON_DMA_CTRL_IRQ_ENABLE;
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Enable interrupt %u \n", i));
                break;
            }
        }

        // Enable interrupts
        pDevExt->pDmaRegisters->commonControl.ControlStatus = CARD_IRQ_ENABLE;
        return STATUS_SUCCESS;
}

/*! DMADriverInterruptDisable
 *
 * \brief Called at DIRQL before EvtDeviceD0Exit is called or
 *  when WdfInterruptDisable is called.
 *  The interrupt spinlock is being held during this call.
 * \note This disables global device interrupts.
 *  DMA Engine interrupts are not disabled at this point.
 * \param Interrupt
 * \param Device
 * \return status
 */
NTSTATUS DMADriverInterruptDisable(IN WDFINTERRUPT Interrupt, IN WDFDEVICE Device)
{
        PDEVICE_EXTENSION pDevExt = DMADriverGetDeviceContext(Device);
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
        UINT32 i;

        /*
         * The user interrupt does not have an associated DMA engine.
        */
        for (i=0; i<MAX_NUM_DMA_ENGINES; i++) {
            pDmaExt = pDevExt->pDmaEngineDevExt[i];
            if (pDmaExt && pDmaExt->Interrupt == Interrupt) {
                pDmaExt->pDmaEng->ControlStatus &= ~COMMON_DMA_CTRL_IRQ_ENABLE;
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Disable interrupt %u  \n", i));
                break;
            }
        }

        // Disable interrupts
        pDevExt->pDmaRegisters->commonControl.ControlStatus = 0;

        return STATUS_SUCCESS;
}

VOID UserIrqComplete(PDEVICE_EXTENSION pDevExt, NTSTATUS Status)
{
    WdfSpinLockAcquire(pDevExt->UsrIntSpinLock);

    DMADriverUsrIntReqTimerStop(pDevExt, FALSE);

//   KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL UsrIntRequest %p UsrIntCount %llu status %d", pDevExt->UsrIntRequest, pDevExt->UsrIntCount, status));

    if (pDevExt->UsrIntRequest) {
        if (pDevExt->UsrIntCount && Status == STATUS_SUCCESS)
        {
            pDevExt->UsrIntCount--;
        }
        WdfRequestComplete(pDevExt->UsrIntRequest, Status);
        pDevExt->UsrIntRequest = NULL;
    }
    WdfSpinLockRelease(pDevExt->UsrIntSpinLock); 
}

// User Interrupt Support
/*! UserIRQDpc
 *
 * \brief This routine processes User Interrupts
 * \param pDpcCtx - Pointer to the driver context for this Dpc
 * \returns none
 */
VOID UserIRQDpc(IN WDFDPC Dpc)
{
        PDEVICE_EXTENSION pDevExt;

       KdPrintEx((1, DPFLTR_ERROR_LEVEL, ""));

        pDevExt = DMADriverGetDeviceContext(WdfDpcGetParentObject(Dpc));

        WdfSpinLockAcquire(pDevExt->UsrIntSpinLock);

        // Acknowledge the User Interrupt in the Common Control by writing the INTERRUPT_ACTIVE flag.
        pDevExt->pDmaRegisters->commonControl.ControlStatus |= CARD_USER_INTERRUPT_ACTIVE;

        pDevExt->UsrIntCount++;
        WdfSpinLockRelease(pDevExt->UsrIntSpinLock);

        UserIrqComplete(pDevExt, STATUS_SUCCESS);
}

// End User Interrupt
