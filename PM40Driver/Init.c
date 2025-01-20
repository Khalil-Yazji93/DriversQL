// -------------------------------------------------------------------------
//
// PRODUCT:            DMA Driver
// MODULE NAME:        Init.c
//
// MODULE DESCRIPTION:
//
// Contains the initialization functions for the PCI Express DMA Driver.
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

// Windows 10 SDK build warnings in Win7 and Win8.
// See line 1086 for more information.
#pragma warning(disable : 30029)
#pragma prefast(disable : 30030)

#if TRACE_ENABLED
#include "Init.tmh"
#endif                          // TRACE_ENABLED

// Local defines
#define PCI_STATUS_CAPABILITIES_LIST_PRESENT    (0x0010)
#define PCI_EXPROM_ADDRESS_OFFSET                11
#pragma warning( disable : 4996)


//This bit shouldnt be here its a copy and paste from a windows header that was not #defined
/*
#define DECLARE_CONST_UNICODE_STRING(_variablename, _string)      \
const WCHAR _variablename ## _buffer[] = _string;                 \
__pragma(warning(suppress:4204)) __pragma(warning(suppress:4221)) \
const UNICODE_STRING _variablename = { sizeof(_string) - sizeof(WCHAR), sizeof(_string), (PWSTR) _variablename ## _buffer }
*/
DECLARE_CONST_UNICODE_STRING(InterruptManagementName, L"Interrupt Management");
DECLARE_CONST_UNICODE_STRING(MSIPropertiesName, L"MessageSignaledInterruptProperties");
DECLARE_CONST_UNICODE_STRING(MSISupportedName, L"MSISupported");
DECLARE_CONST_UNICODE_STRING(MSIXSupportedName, L"MSIXSupported");
DECLARE_CONST_UNICODE_STRING(MSILimitName, L"MessageNumberLimit");
DECLARE_CONST_UNICODE_STRING(InterruptModeName, L"InterruptMode");
DECLARE_CONST_UNICODE_STRING(NumberDMADescName, L"NumberDMADescriptors");

// Local Prototypes
NTSTATUS DmaDriverSetupQueues(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

NTSTATUS DmaDriverCreateDMAObjects(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

NTSTATUS DmaDriverCreateDMADescBuffers(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

NTSTATUS DmaDriverSetupDPC(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

NTSTATUS DMADriverScanPciResources(IN PDEVICE_EXTENSION pDevExt, IN WDFCMRESLIST ResourcesTranslated);

NTSTATUS DMADriverBoardConfigInit(PDEVICE_EXTENSION pDevExt);

NTSTATUS DMADriverGetRegHardwareInfo(PDEVICE_EXTENSION pDevExt);

NTSTATUS DMADriverGetRegistryInfo(PDEVICE_EXTENSION pDevExt);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, DMADriverInitializeDeviceExtension)
#pragma alloc_text (PAGE, DMADriverPrepareHardware)
#pragma alloc_text (PAGE, DMADriverScanPciResources)
#endif                          /* ALLOC_PRAGMA */

/*! DMADriverBoardDmaInit
 *
 * \brief Get the DMA Configuration from the card and initialize the DMA routines.
 *  This may get called twice.  If we have already setup the DMA engine once before,
 * do not recreate the DMA Engine Dev Ext, Queue or Transaction structures.
 * \param pDevExt
 * \return status
 */
NTSTATUS DMADriverBoardDmaInit(PDEVICE_EXTENSION pDevExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        UINT8 dmaNum;
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
        WDF_DMA_DIRECTION dmaDirection;

       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL --> DMADriverBoardDmaInit\n"));

        ASSERT(sizeof(DMA_DESCRIPTOR_STRUCT) == 32);

        // Setup the DMA Engines -
        // By default, BAR 0 is used for the DMA registers.  Validate it as a Memory Bar.
        if ((pDevExt->NumberOfBARS > DMA_REG_BAR) && ((pDevExt->BarType[DMA_REG_BAR] == CmResourceTypeMemory) || (pDevExt->BarType[DMA_REG_BAR] == CmResourceTypeMemoryLarge))) {
                pDevExt->pDmaRegisters = (PBAR0_REGISTER_MAP_STRUCT) pDevExt->BarVirtualAddress[DMA_REG_BAR];

                if (pDevExt->pDmaRegisters != NULL) {
                        // Scan each possible DMA register locations for existing DMA Engine controller
                        for (dmaNum = 0; dmaNum < MAX_NUM_DMA_ENGINES; dmaNum++) {
                                if ((pDevExt->pDmaRegisters->dmaEngine[dmaNum].Capabilities & DMA_CAP_ENGINE_PRESENT) == DMA_CAP_ENGINE_PRESENT) {
                                        // See if this is a Block Mode DMA Engine
                                        if ((pDevExt->pDmaRegisters->dmaEngine[dmaNum].Capabilities & DMA_CAP_ENGINE_TYPE_MASK) == DMA_CAP_BLOCK_DMA) {
                                                // If so, bypass it since we do not support it.
                                                continue;
                                        }
                                        // Found one! See if it is a System To Card
                                        if (DMA_CAP_ENGINE_DIRECTION(pDevExt->pDmaRegisters->dmaEngine[dmaNum].Capabilities) == DMA_CAP_SYSTEM_TO_CARD) {
                                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL DMA Engine found, #%d, S2C, Capabilities=0x%08x\n", dmaNum, pDevExt->pDmaRegisters->dmaEngine[dmaNum].Capabilities));
                                                // S2C DMA engine
                                                dmaDirection = WdfDmaDirectionWriteToDevice;
                                                if (++pDevExt->BoardConfig.NumDmaWriteEngines == 1) {
                                                        pDevExt->BoardConfig.FirstDmaWriteEngine = dmaNum;
                                                }
                                        } else  // If not assume it is a Card To System DMA Engine
                                        {
                                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL DMA Engine found, #%d, C2S, Capabilities=0x%08x\n", dmaNum, pDevExt->pDmaRegisters->dmaEngine[dmaNum].Capabilities));
                                                // C2S DMA engine
                                                dmaDirection = WdfDmaDirectionReadFromDevice;
                                                if (++pDevExt->BoardConfig.NumDmaReadEngines == 1) {
                                                        pDevExt->BoardConfig.FirstDmaReadEngine = dmaNum;
                                                }
                                        }

                                        // If the DMA Engine Device extension has not been created
                                        if (pDevExt->pDmaEngineDevExt[dmaNum] == NULL) {
                                                // create it
                                                pDevExt->pDmaEngineDevExt[dmaNum] = (PDMA_ENGINE_DEVICE_EXTENSION) ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(DMA_ENGINE_DEVICE_EXTENSION), 'amDP');
                                                if (pDevExt->pDmaEngineDevExt[dmaNum] != NULL) {
                                                        pDmaExt = pDevExt->pDmaEngineDevExt[dmaNum];
                                                        pDevExt->pDmaExtMSIVector[pDevExt->NumberDMAEngines] = pDmaExt;

                                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL pDmaExtMSIVector %u gets DMA Engine %u\n", pDevExt->NumberDMAEngines, dmaNum));

                                                        pDmaExt->Interrupt = pDevExt->Interrupt[pDevExt->NumberDMAEngines];

                                                        // initialize the structure
                                                        pDmaExt->TransactionQueue = NULL;
                                                        pDmaExt->DmaEnabler = NULL;
                                                        pDmaExt->DmaEnabler32BitOnly = NULL;
                                                        pDmaExt->DmaRequest = NULL;
                                                        pDmaExt->DmaTransaction = NULL;
                                                        pDmaExt->DescCommonBuffer = NULL;
                                                        pDmaExt->pHWDescriptorBasePhysical.QuadPart = 0;
                                                        pDmaExt->pHWDescriptorBase = NULL;
                                                        // Default to a single vector
                                                        pDmaExt->DMAEngineMSIVector = 0;

                                                        // Packet Mode Specific variables
                                                        pDmaExt->PMdl = NULL;
                                                        pDmaExt->UserVa = NULL;
                                                        pDmaExt->DmaSpinLock = 0;
                                                        pDmaExt->PacketMode = DMA_MODE_NOT_SET;
                                                        pDmaExt->DMAEngineStatus = 0;
                                                        pDmaExt->bFreeRun = FALSE;

                                                        pDmaExt->pDrvDescBase = NULL;
                                                        pDmaExt->pNextDesc = NULL;
                                                        pDmaExt->pTailDesc = NULL;
                                                        pDmaExt->pIrqDesc = NULL;

                                                        // initialize performance counters
                                                        pDmaExt->BytesInLastSecond = 0;
                                                        pDmaExt->BytesInCurrentSecond = 0;
                                                        pDmaExt->HardwareTimeInLastSecond = 0;
                                                        pDmaExt->HardwareTimeInCurrentSecond = 0;
                                                        pDmaExt->DMAInactiveTime = 0;
                                                        pDmaExt->IntsInLastSecond = 0;
                                                        pDmaExt->DPCsInLastSecond = 0;

                                                        // save the device direction
                                                        pDmaExt->DmaEngine = dmaNum;
                                                        pDmaExt->DmaDirection = dmaDirection;
                                                        // Setup the pointer to this DMA Engines registers
                                                        pDmaExt->pDmaEng = &pDevExt->pDmaRegisters->dmaEngine[dmaNum];

                                                        HardResetDMAEngine(pDmaExt);

                                                        // determine the maximum transfer size
                                                        pDmaExt->MaximumTransferLength = (size_t) DMA_CAP_CARD_ADDR_SIZE(pDmaExt->pDmaEng->Capabilities);
                                                        if (pDmaExt->MaximumTransferLength == 1) {
                                                                // this is a fifo, use system max transfer length
                                                                pDmaExt->MaximumTransferLength = pDevExt->MaximumDmaTransferLength;
                                                        }
                                                        // Go setup the driver IOCTL and internal queues
                                                        status = DmaDriverSetupQueues(pDevExt, pDmaExt);
                                                        if (NT_SUCCESS(status)) {
                                                                // Go create the spnlocks and DMA Enabler objects
                                                                status = DmaDriverCreateDMAObjects(pDevExt, pDmaExt);
                                                                if (NT_SUCCESS(status)) {
                                                                        // Go create the Common PCI DMA Descriptor buffers
                                                                        status = DmaDriverCreateDMADescBuffers(pDevExt, pDmaExt);
                                                                        if (NT_SUCCESS(status)) {
                                                                                // Go setup the DPC for this DMA Engine
                                                                                status = DmaDriverSetupDPC(pDevExt, pDmaExt);
                                                                                if (NT_SUCCESS(status)) {
                                                                                        pDevExt->NumberDMAEngines++;
                                                                                } else {
                                                                                        // DmaDriverSetupDPC Failed
                                                                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DmaDriverSetupDPC failed for DmaEngine[%d] 0x%x\n", dmaNum, status));
                                                                                        DmaDriverBoardDmaRelease(pDevExt, dmaNum);
                                                                                }
                                                                        } else {
                                                                                // DmaDriverCreateDMADescBuffers Failed
                                                                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DmaDriverCreateDMADescBuffers failed for DmaEngine[%d] 0x%x\n", dmaNum, status));
                                                                                DmaDriverBoardDmaRelease(pDevExt, dmaNum);
                                                                        }
                                                                } else {
                                                                        // DmaDriverCreateDMAObjects Failed
                                                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DmaDriverCreateDMAObjects failed for DmaEngine[%d] 0x%x\n", dmaNum, status));
                                                                        DmaDriverBoardDmaRelease(pDevExt, dmaNum);
                                                                }
                                                        } else  // DmaDriverSetupQueues Failed!
                                                        {
                                                                // DmaDriverSetupQueues Failed
                                                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DmaDriverSetupQueues failed for DmaEngine[%d] 0x%x\n", dmaNum, status));
                                                                DmaDriverBoardDmaRelease(pDevExt, dmaNum);
                                                        }
                                                } else  // ExAllocatePoolWithTag Failed for Dma Engine Context
                                                {
                                                        // ExAllocatePoolWithTag Failed
                                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- ExAllocatePoolWithTag failed for DmaEngine[%d] 0x%x\n", dmaNum, status));
                                                }
                                        }       //    if (pDevExt->pDmaEngineDevExt[dmaNum] == NULL)
                                }       // if ((pDevExt->pDmaRegisters->dmaEngine[dmaNum].Capabilities  Found a valid DMA Engine
                        }       // for (dmaNum = 0; dmaNum < MAX_NUM_DMA_ENGINES; dmaNum++)
                } else          // Error-  pDevExt->pDmaRegister is NULL!
                {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL pDevExt->pDmaRegisters == NULL"));
                        status = STATUS_DEVICE_CONFIGURATION_ERROR;
                }
        }                       // if ((pDevExt->NumberOfBARS > DMA_BAR) && ((pDevExt->BarType[DMA_BAR] == CmResourceTypeMemory)

        // See if we setup multiple Interrupt vectors
        if (pDevExt->NumIRQVectors > 1) {
                UINT32 MSIVector = 0;

               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL NumIRQVectors %u NumberDMAEngines %u\n", pDevExt->NumIRQVectors, pDevExt->NumberDMAEngines));

                // Now make sure we allocated enough MSI vectors
                if (pDevExt->NumIRQVectors >= pDevExt->NumberDMAEngines) {
                        for (dmaNum = 0; dmaNum < MAX_NUM_DMA_ENGINES; dmaNum++) {
                                pDmaExt = pDevExt->pDmaEngineDevExt[dmaNum];
                                // Make sure this is a valid DMA Engine
                                if (pDmaExt != NULL) {
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL DMA Engine %u (index %u) gets MSI vector %u\n", pDmaExt->DmaEngine, dmaNum, MSIVector));

                                        // If so, record the vector in each DMA Extension.
                                        pDmaExt->DMAEngineMSIVector = MSIVector;
                                        MSIVector++;
                                }
                        }
                }
        }
        // Retireve the Card status and the FPGA Version information from the Card.
        if (status == STATUS_SUCCESS) {
                pDevExt->BoardConfig.DMARegistersBAR = DMA_REG_BAR;
                pDevExt->BoardConfig.CardStatusInfo = pDevExt->pDmaRegisters->commonControl.ControlStatus;
                pDevExt->BoardConfig.DMABackEndCoreVersion = pDevExt->pDmaRegisters->commonControl.DMABackEndCoreVersion;
                pDevExt->BoardConfig.PCIExpressCoreVersion = pDevExt->pDmaRegisters->commonControl.PCIExpressCoreVersion;
                pDevExt->BoardConfig.UserVersion = pDevExt->pDmaRegisters->commonControl.UserVersion;
        }

       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DMADriverBoardDmaInit, 0x%x\n", status));
        return status;
}

/*! DMADriverSetupQueue
 *
 * \brief Sets up the WDF Driver IOCTL Queues depending on the type of DMA Engine
 * \param pDevExt
 * \param pDmaExt
 * \return status
 */
NTSTATUS DmaDriverSetupQueues(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        WDF_IO_QUEUE_CONFIG ioQueueConfig;
        WDF_OBJECT_ATTRIBUTES attributes;
        PQUEUE_CTX pQueueCtx;

        // Check for Packet Mode Send
        if (((pDmaExt->pDmaEng->Capabilities & DMA_CAP_ENGINE_TYPE_MASK) & DMA_CAP_PACKET_DMA) && (pDmaExt->DmaDirection == WdfDmaDirectionWriteToDevice)) {
                // Processing callbacks - We do our own queue management
                WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig, WdfIoQueueDispatchManual);

                pDmaExt->DmaType = DMA_TYPE_PACKET_SEND;
        }
        // If not check for Packet Mode Receive
        else if (((pDmaExt->pDmaEng->Capabilities & DMA_CAP_ENGINE_TYPE_MASK) & DMA_CAP_PACKET_DMA) && (pDmaExt->DmaDirection == WdfDmaDirectionReadFromDevice)) {
                // Processing callbacks - We do our own queue management
                WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig, WdfIoQueueDispatchManual);

                pDmaExt->DmaType = DMA_TYPE_PACKET_RECV;
        }
        pDmaExt->bAddressablePacketMode = FALSE;
        if ((pDmaExt->pDmaEng->Capabilities & DMA_CAP_ENGINE_TYPE_MASK) & DMA_CAP_ADDRESSABLE_PACKET_DMA) {
                pDmaExt->bAddressablePacketMode = TRUE;
                if (pDmaExt->DmaType == DMA_TYPE_PACKET_RECV) {
                        /* Setup the Cancel Request Handler */
                        ioQueueConfig.EvtIoCanceledOnQueue = PacketReadRequestCancel;
                }
        }
        // Create Queue
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, QUEUE_CTX);

        // DMA Transaction queue - We do our own queue management
        WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig, WdfIoQueueDispatchManual);
        status = WdfIoQueueCreate(pDevExt->Device, &ioQueueConfig, &attributes, &pDmaExt->TransactionQueue);
        if (NT_SUCCESS(status)) {
                // Keep a pointer the Dma Device Extension
                pQueueCtx = QueueContext(pDmaExt->TransactionQueue);
                pQueueCtx->pDmaExt = pDmaExt;
        } else {
                // Create Queue Failed
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- WdfIoQueueCreate failed for DmaEngine[%d] 0x%x\n", pDmaExt->DmaEngine, status));
        }
        return status;
}

/*! DMADriverCreateDMAObjects
 *
 * \brief Sets up the WDF Driver Spinlocks and EnablerCreate objects
 * \param pDevExt
 * \param pDmaExt
 * \return status
 */
NTSTATUS DmaDriverCreateDMAObjects(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        WDF_DMA_ENABLER_CONFIG dmaConfig;
        UINT32 mapRegistersAllocated;
        UINT32 maxFragmentLengthSupported;

		DMADriverLockInit(pDmaExt);

        status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &pDmaExt->DmaSpinLock);
        if (NT_SUCCESS(status)) {
                pDmaExt->MaximumTransferLength = pDevExt->NumberOfDescriptors * PAGE_SIZE;

               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL DMA Max Transfer size is %Id, Max Equate %d\n", pDmaExt->MaximumTransferLength, DMA_MAX_TRANSFER_LENGTH));
                // Initialize DMA Enabler Config, 64 bit version capable for Scatter/Gathers
                WDF_DMA_ENABLER_CONFIG_INIT(&dmaConfig, WdfDmaProfileScatterGather64, pDmaExt->MaximumTransferLength);
                // create enabler
                status = WdfDmaEnablerCreate(pDevExt->Device, &dmaConfig, WDF_NO_OBJECT_ATTRIBUTES, &pDmaExt->DmaEnabler);
                if (NT_SUCCESS(status)) {
                        // check to see how many DMA map registers are allocated
                        maxFragmentLengthSupported = (UINT32) WdfDmaEnablerGetFragmentLength(pDmaExt->DmaEnabler, pDmaExt->DmaDirection);
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL DMA Max Transfer size is %Id, Max Equate %d, wdf Max Length %u\n",
                               pDmaExt->MaximumTransferLength, DMA_MAX_TRANSFER_LENGTH, maxFragmentLengthSupported));
                        // calculate the number of map Registers
                        mapRegistersAllocated = BYTES_TO_PAGES(maxFragmentLengthSupported) + 1;

                        // Set to our maximum default number of descriptors
                        if (mapRegistersAllocated > pDevExt->NumberOfDescriptors) {
                                mapRegistersAllocated = pDevExt->NumberOfDescriptors;
                        }
                        // set the number of DMA elements to be this count
                        WdfDmaEnablerSetMaximumScatterGatherElements(pDmaExt->DmaEnabler, mapRegistersAllocated);

                        // Setup descriptor information used to be DMA_NUM_DESCR or the registry override
                        pDmaExt->NumberOfDescriptors = mapRegistersAllocated;
                        pDmaExt->NumberOfUsedDescriptors = 0;

                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL DMA Enabler Created, %d, MaxXferSize %d, mapRegistersAllocated = 0x%x\n", pDmaExt->DmaEngine, maxFragmentLengthSupported, mapRegistersAllocated));

                        // Initialize DMA Enabler Config
                        WDF_DMA_ENABLER_CONFIG_INIT(&dmaConfig, WdfDmaProfileScatterGather, pDmaExt->MaximumTransferLength);

                        // create enabler
                        status = WdfDmaEnablerCreate(pDevExt->Device, &dmaConfig, WDF_NO_OBJECT_ATTRIBUTES, &pDmaExt->DmaEnabler32BitOnly);
                } else  // WdfDmaEnablerCreate Failed!
                {
                        // Create Enabler Failed
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- WdfDmaEnablerCreate failed for DmaEngine[%d] 0x%x\n", pDmaExt->DmaEngine, status));
                }
        } else          // WdfSpinLockCreate Failed!
        {
                // Create SpinLock Failed
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- WdfSpinLockCreate failed for DmaEngine[%d] 0x%x\n", pDmaExt->DmaEngine, status));
        }
        return status;
}

/*! DMADriverCreateDMADescBuffers
 *
 * \brief Creates the Common PCI Addressable buffer for use as the DMA Descriptors
 * \param pDevExt
 * \param pDmaExt
 * \return status
 */
NTSTATUS DmaDriverCreateDMADescBuffers(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        WDF_COMMON_BUFFER_CONFIG commonBufferConfig;

        UNREFERENCED_PARAMETER(pDevExt);

        // define the descriptor space
        WDF_COMMON_BUFFER_CONFIG_INIT(&commonBufferConfig, DMA_DESCR_ALIGN_REQUIREMENT);

        // create it
        status = WdfCommonBufferCreateWithConfig(pDmaExt->DmaEnabler32BitOnly,
                                                 (size_t) (sizeof(DMA_DESCRIPTOR_STRUCT) * pDmaExt->NumberOfDescriptors), &commonBufferConfig, WDF_NO_OBJECT_ATTRIBUTES, &pDmaExt->DescCommonBuffer);
        if (NT_SUCCESS(status)) {
                // initialize the structure
                pDmaExt->pHWDescriptorBase = WdfCommonBufferGetAlignedVirtualAddress(pDmaExt->DescCommonBuffer);
                pDmaExt->pHWDescriptorBasePhysical = WdfCommonBufferGetAlignedLogicalAddress(pDmaExt->DescCommonBuffer);
#if defined(_AMD64_)
                // This is a work-around for an error where the descriptor memory is allocated in
                // above 4GB. The HW design requires the descriptors live in the first 4GB.
                // If we set ScatterGatherDuplex then Windows restricts the number of Map registers
                // to 256. We will take our chances with the allocate.
                if (pDmaExt->pHWDescriptorBasePhysical.HighPart & 0xffffffff) {
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL ERROR: Descriptor Buffer Created in memory above 4GB (address:0x%p)\n", pDmaExt->pHWDescriptorBasePhysical));
                        DmaDriverBoardDmaRelease(pDevExt, pDmaExt->DmaEngine);
                        return STATUS_NO_MEMORY;
                }
#endif                          // defined(_AMD64_)||defined(_IA64_)
                pDmaExt->pDrvDescBase = (PDRIVER_DESC_STRUCT) ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(DRIVER_DESC_STRUCT) * pDmaExt->NumberOfDescriptors, 'pxDD');
                if (pDmaExt->pDrvDescBase == NULL) {
                        return STATUS_INSUFFICIENT_RESOURCES;
                }

                status = DMADriverIntiializeDMADescriptors(pDmaExt, pDmaExt->NumberOfDescriptors, 0);

               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Descr Buffer Created, DmaEngine:%d, with %d Descriptor starting at address:0x%p\n", pDmaExt->DmaEngine, pDmaExt->NumberOfDescriptors, pDmaExt->pHWDescriptorBase));

                if (pDmaExt->DmaType == DMA_TYPE_PACKET_RECV) {
                        // For the Packet Recieve get the DMA Adapter handle
                        pDmaExt->pReadDmaAdapter = WdfDmaEnablerWdmGetDmaAdapter(pDmaExt->DmaEnabler, WdfDmaDirectionReadFromDevice);
                }
        }
        return status;
}

/*! DMADriverSetupDPC
 *
 * \brief Sets up the WDF Driver DPC routine for this DMA Engine
 * \param pDevExt
 * \param pDmaExt
 * \return status
 */
NTSTATUS DmaDriverSetupDPC(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        WDF_DPC_CONFIG dpcConfig;
        PDPC_CTX pDpcCtx;
        WDF_OBJECT_ATTRIBUTES dpcAttributes;

       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Engine %u DPC %s\n", pDmaExt->DmaEngine,
            pDmaExt->DmaType == DMA_TYPE_PACKET_SEND ? "PacketS2CDpc" : "PacketC2SDpc"));

        // Is this a Packet Send DMA Engine?
        if (pDmaExt->DmaType == DMA_TYPE_PACKET_SEND) {
                WDF_DPC_CONFIG_INIT(&dpcConfig, PacketS2CDpc);
        }
        // Is this a Packet Receive DMA Engine?
        else if (pDmaExt->DmaType == DMA_TYPE_PACKET_RECV) {
                WDF_DPC_CONFIG_INIT(&dpcConfig, PacketC2SDpc);
        } else {
                // DMA Type not found
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DmaDriverSetupDPC failed, could not determine DMA Engine type for DmaEngine[%d] 0x%x\n", pDmaExt->DmaEngine, status));
        }

        dpcConfig.AutomaticSerialization = FALSE;

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&dpcAttributes, DPC_CTX);
        dpcAttributes.ParentObject = pDevExt->Device;

        status = WdfDpcCreate(&dpcConfig, &dpcAttributes, &pDmaExt->CompletionDpc);

        if (NT_SUCCESS(status)) {
                // Keep a pointer to the Dma Device Extension
                pDpcCtx = DPCContext(pDmaExt->CompletionDpc);
                pDpcCtx->pDmaExt = pDmaExt;
        } else {
                // Create Queue Failed
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- WdfDpcCreate failed for DmaEngine[%d] 0x%x\n", pDmaExt->DmaEngine, status));
        }
        return status;
}

// User Interrupts
/*! DmaDriverSetupUserIRQ
*
* \brief
* \param pDevExt
* return status
*/
NTSTATUS DmaDriverSetupUserIRQ(IN PDEVICE_EXTENSION pDevExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        WDF_DPC_CONFIG dpcConfig;
        WDF_OBJECT_ATTRIBUTES dpcAttributes;


        pDevExt->UsrIntRequest = NULL;

        // Create SpinLock Object during Init.
        status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &pDevExt->UsrIntSpinLock);
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Failed to Create User Interrupt Spin Lock 0x%x\n", status));
                return status;
        }
       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Spin Lock %d\n", pDevExt->UsrIntSpinLock));

        WDF_DPC_CONFIG_INIT(&dpcConfig, UserIRQDpc);
        dpcConfig.AutomaticSerialization = TRUE;

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&dpcAttributes, DPC_UICTX);
        dpcAttributes.ParentObject = pDevExt->Device;

        // Create the DPC object with the pointer to the UsrIrqDpc
        status = WdfDpcCreate(&dpcConfig, &dpcAttributes, &pDevExt->UsrIrqDpc);
       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL USL WdfDpcCreate 0x%x\n", status));	//USL

        // If DPC created successfully,
        if (!NT_SUCCESS(status)) {
                // Create Queue Failed
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- WdfDpcCreate failed for User Interrupts 0x%x\n", status));
                return status;
        }
        // Initialize the User Interrupt Request Timeout Timer
        status = DMADriverUsrIntReqTimerInit(pDevExt);
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Failed to Create User Interrupt Timer 0x%x\n", status));
                return status;
        }
        return status;
}

// End User Interrupt

/*! DMADriverBoardDmaRelease
 *
 * \brief Releases the DMA Engine resources.
 * \param pDevExt
 * \param DmaEngNum
 * \return status
 */
NTSTATUS DmaDriverBoardDmaRelease(IN PDEVICE_EXTENSION pDevExt, IN UINT8 DmaEngNum)
{
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
        NTSTATUS status = STATUS_SUCCESS;

        if (pDevExt->pDmaEngineDevExt[DmaEngNum] != NULL) {
                pDmaExt = pDevExt->pDmaEngineDevExt[DmaEngNum];

                if (pDmaExt->DescCommonBuffer != NULL) {
                        // Free the Common Buffer
                        //
                        WdfObjectDelete(pDmaExt->DescCommonBuffer);
                        pDmaExt->DescCommonBuffer = NULL;
                }
                if (pDmaExt->pDrvDescBase != NULL) {
                        // Free the DMA Descriptor Software structure
                        ExFreePoolWithTag(pDmaExt->pDrvDescBase, 'pxDD');
                }
                if (pDmaExt->DmaTransaction != NULL) {
                        // Free the Dma Transaction
                        WdfObjectDelete(pDmaExt->DmaTransaction);
                        pDmaExt->DmaTransaction = NULL;
                }
                if (pDmaExt->DmaEnabler32BitOnly != NULL) {
                        // Free the Dma Enabler
                        WdfObjectDelete(pDmaExt->DmaEnabler32BitOnly);
                        pDmaExt->DmaEnabler32BitOnly = NULL;
                }
                if (pDmaExt->DmaEnabler != NULL) {
                        // Free the Dma Enabler
                        WdfObjectDelete(pDmaExt->DmaEnabler);
                        pDmaExt->DmaEnabler = NULL;
                }
                if (pDmaExt->DmaSpinLock != 0) {
                        // Free the SpinLock
                        WdfObjectDelete(pDmaExt->DmaSpinLock);
                        pDmaExt->DmaSpinLock = 0;
                }
                // Adjust the number of dma engine(s)
                if (pDmaExt->DmaDirection == WdfDmaDirectionWriteToDevice) {
                        // S2C DMA engine
                        if (--pDevExt->BoardConfig.NumDmaWriteEngines == 0) {
                                pDevExt->BoardConfig.FirstDmaWriteEngine = 0;
                        }
                } else {
                        // C2S DMA engine
                        if (--pDevExt->BoardConfig.NumDmaReadEngines == 0) {
                                pDevExt->BoardConfig.FirstDmaReadEngine = 0;
                        }
                }
                // The last thing we do is free the DMA Engine Context
                ExFreePoolWithTag(pDevExt->pDmaEngineDevExt[DmaEngNum], 'amDP');
                pDevExt->pDmaEngineDevExt[DmaEngNum] = NULL;
        }
        return status;
}

/*! DMADriverInitializeDeviceExtension
 *
 * \brief This routine initializes all the data structures required by this device.
 *  This routine also creates the read, write and IOCtl queues
 * \param pDevExt
 * \return status
 */
NTSTATUS DMADriverInitializeDeviceExtension(PDEVICE_EXTENSION pDevExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        WDF_IO_QUEUE_CONFIG ioQueueConfig;
        INT32 i;

        PAGED_CODE();
        pDevExt->BoardConfig.DriverVersionMajor = VER_PCI_MAJOR_NUM;
        pDevExt->BoardConfig.DriverVersionMinor = VER_PCI_MINOR_NUM;
        pDevExt->BoardConfig.DriverVersionSubMinor = VER_PCI_SUBMINOR_NUM;
        pDevExt->BoardConfig.DriverVersionBuildNumber = VER_PCI_BUILD_NUM;

        // Setup DMA structures
        pDevExt->BoardConfig.NumDmaWriteEngines = 0;
        pDevExt->BoardConfig.FirstDmaWriteEngine = 0;
        pDevExt->BoardConfig.NumDmaReadEngines = 0;
        pDevExt->BoardConfig.FirstDmaReadEngine = 0;
        pDevExt->BoardConfig.DMARegistersBAR = 0;
        pDevExt->BoardConfig.reserved1 = 0;
        pDevExt->pDmaRegisters = NULL;
        for (i = 0; i < MAX_NUM_DMA_ENGINES; i++) {
                pDevExt->pDmaEngineDevExt[i] = NULL;
                pDevExt->pDmaExtMSIVector[i] = NULL;
                pDevExt->Interrupt[i] = NULL;
        }

        // Default to not supporting MSI/MSI-X Intterupt.
        pDevExt->MSISupported = FALSE;
        pDevExt->MSIXSupported = FALSE;
        pDevExt->NumIRQVectors = 0;
        pDevExt->MSINumberVectors = MAX_NUM_DMA_ENGINES;

        // Count how many DMA Engines this firmware supports
        pDevExt->NumberDMAEngines = 0;

        pDevExt->WatchdogTimer = NULL;

        // for now, default this
        pDevExt->MaximumDmaTransferLength = DMA_MAX_TRANSFER_LENGTH;
        pDevExt->InterruptMode = PACKET_DMA_INT_CTRL_INT_EOP;

        pDevExt->NumberOfDescriptors = DMA_NUM_DESCR;

        // Check the registry for any initialization overrides
        DMADriverGetRegistryInfo(pDevExt);

        // initialize IOCTL Queue
        // - Default Queue
        // - Parallel Processing
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchParallel);

        // Processing callbacks
        ioQueueConfig.EvtIoRead = DMADriverEvtIoRead;
        ioQueueConfig.EvtIoWrite = DMADriverEvtIoWrite;
        ioQueueConfig.EvtIoDeviceControl = DMADriverEvtIoDeviceControl;

        // Create Queue
        status = WdfIoQueueCreate(pDevExt->Device, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &pDevExt->IoctlQueue);
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- WdfIoQueueCreate failed for IoctlQueue 0x%x\n", status));
                return status;
        }

        DMADriverGetRegHardwareInfo(pDevExt);

        // Setup the PCI Bus interface
        status = WdfFdoQueryForInterface(pDevExt->Device, &GUID_BUS_INTERFACE_STANDARD, (PINTERFACE) & pDevExt->BusInterface, sizeof(BUS_INTERFACE_STANDARD), 1,        // version
                                         NULL   // info
            );
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DMADriverInitializeDeviceExtension call WdfFdoQueryForInterface failed  0x%x\n", status));
                return status;
        }
        // Setup the board Config Structure
        status = DMADriverBoardConfigInit(pDevExt);
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DMADriverInitializeDeviceExtension call DMADriverBoardConfigInit failed  0x%x\n", status));
                return status;
        }
       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL USL int interrupts\n"));
        // Setup Interrupt Object(s)
        status = DMADriverInterruptCreate(pDevExt);
        if (!NT_SUCCESS(status)) {
                return status;
        }
       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL USL DmaDriverSetupUserIRQ\n"));
		status = DmaDriverSetupUserIRQ(pDevExt);
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DMADriverPrepareHardware call DMADriverSetupUserIRQDpc failed  0x%x\n", status));
        }
        return status;
}

/*! DMADriverPrepareHardware
 *
 * \brief This routine scans the device for resources and maps the memory areas.
 * \param pDevExt
 * \param ResourcesTranslated
 * \return status
 */
NTSTATUS DMADriverPrepareHardware(IN PDEVICE_EXTENSION pDevExt, IN WDFCMRESLIST ResourcesTranslated)
{
        NTSTATUS status = STATUS_SUCCESS;

        PAGED_CODE();

        status = DMADriverScanPciResources(pDevExt, ResourcesTranslated);
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DMADriverPrepareHardware call DMADriverScanPciResources failed  0x%x\n", status));
                return status;
        }
        // Setup the board DMA Resources
        status = DMADriverBoardDmaInit(pDevExt);
        if (!NT_SUCCESS(status)) {
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL <-- DMADriverPrepareHardware call DMADriverBoardDmaInit failed  0x%x\n", status));
                return status;
        }

        return status;
}

/*! DMADriverScanPciResources
 *
 * \brief This routine scans the device for resources and maps the memory areas.
 * \param pDevExt
 * \param ResourcesTranslated
 * \return status
 */
NTSTATUS DMADriverScanPciResources(IN PDEVICE_EXTENSION pDevExt, IN WDFCMRESLIST ResourcesTranslated)
{
        NTSTATUS status = STATUS_SUCCESS;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
        UINT8 bar;
        UINT32 i;
        UINT8 barSize;
        UINT64 barLength;
        BOOLEAN AddressSizeDetected = FALSE;
        UINT32 numIrq = 0;

        PAGED_CODE();

        // Setup prior to scanning hardware resources
        pDevExt->NumberOfBARS = 0;
        pDevExt->Use64BitAddresses = FALSE;

        // search through the resources
        for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); i++) {
                descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
                if (!descriptor) {
                        return STATUS_DEVICE_CONFIGURATION_ERROR;
                }

                switch (descriptor->Type) {
                case CmResourceTypePort:
                        bar = pDevExt->NumberOfBARS;
                        if (bar < MAX_NUMBER_OF_BARS) {
                                // Save IO Space info
                                pDevExt->NumberOfBARS++;
                                pDevExt->BarType[bar] = CmResourceTypePort;
                                pDevExt->BarPhysicalAddress[bar] = descriptor->u.Port.Start;
                                pDevExt->BarLength[bar] = descriptor->u.Port.Length;
                                pDevExt->BarVirtualAddress[bar] = NULL;
                                // Save in BoardCfg space
                                pDevExt->BoardConfig.PciConfig.BarCfg[bar] = BAR_CFG_BAR_PRESENT | BAR_CFG_BAR_TYPE_IO;
                                // compute and store bar size in (2**n)
                                barSize = 0;
                                barLength = pDevExt->BarLength[bar] - 1;
                                while (barLength > 0) {
                                        barSize++;
                                        barLength >>= 1;
                                }
                                pDevExt->BoardConfig.PciConfig.BarCfg[bar] |= ((UINT64) barSize << BAR_CFG_BAR_SIZE_OFFSET);

                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL PCI IO port found, BAR #%d, Length=0x%x, Size=%d\n", bar, (UINT32) pDevExt->BarLength[bar], barSize));
                        }
                        break;

                case CmResourceTypeMemory:
                        bar = pDevExt->NumberOfBARS;
                        if (bar < MAX_NUMBER_OF_BARS) {
                                // Save Memory Space info
                                pDevExt->NumberOfBARS++;
                                pDevExt->BarType[bar] = CmResourceTypeMemory;
                                pDevExt->BarPhysicalAddress[bar] = descriptor->u.Memory.Start;
                                pDevExt->BarLength[bar] = descriptor->u.Memory.Length;
                                // set system bar size
                                if (AddressSizeDetected == FALSE) {
                                        AddressSizeDetected = TRUE;
                                        pDevExt->Use64BitAddresses = FALSE;
                                }
                                // map the memory

#if 1

                                pDevExt->BarVirtualAddress[bar] = MmMapIoSpace(descriptor->u.Memory.Start, descriptor->u.Memory.Length, MmNonCached);
#else                           // Windows 10 use MmMapIoSpaceEx.
                                pDevExt->BarVirtualAddress[bar] = MmMapIoSpaceEx(descriptor->u.Memory.Start, descriptor->u.Memory.Length, PAGE_NOCACHE || PAGE_READWRITE);
#endif
                                // Save in BoardCfg space
                                pDevExt->BoardConfig.PciConfig.BarCfg[bar] = BAR_CFG_BAR_PRESENT | BAR_CFG_BAR_TYPE_MEMORY;
                                if ((descriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) == CM_RESOURCE_MEMORY_PREFETCHABLE) {
                                        pDevExt->BoardConfig.PciConfig.BarCfg[bar] |= BAR_CFG_MEMORY_PREFETCHABLE;
                                }
                                // compute and store bar size in (2**n)
                                barSize = 0;
                                barLength = pDevExt->BarLength[bar] - 1;
                                while (barLength > 0) {
                                        barSize++;
                                        barLength >>= 1;
                                }
                                pDevExt->BoardConfig.PciConfig.BarCfg[bar] |= ((UINT64) barSize << BAR_CFG_BAR_SIZE_OFFSET);

                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Memory port found, BAR #%d, Length=0x%x, Size=%d\n", bar, (UINT32) pDevExt->BarLength[bar], barSize));
                        }
                        break;

                case CmResourceTypeMemoryLarge:
                        bar = pDevExt->NumberOfBARS;
                        if (bar < MAX_NUMBER_OF_BARS) {
                                // Save Memory Space info
                                pDevExt->NumberOfBARS++;
                                pDevExt->BarType[bar] = CmResourceTypeMemoryLarge;
                                if ((descriptor->Flags & CM_RESOURCE_MEMORY_LARGE_40) == CM_RESOURCE_MEMORY_LARGE_40) {
                                        pDevExt->BarPhysicalAddress[bar] = descriptor->u.Memory40.Start;
                                        pDevExt->BarLength[bar] = descriptor->u.Memory40.Length40;
                                } else if ((descriptor->Flags & CM_RESOURCE_MEMORY_LARGE_48) == CM_RESOURCE_MEMORY_LARGE_48) {
                                        pDevExt->BarPhysicalAddress[bar] = descriptor->u.Memory48.Start;
                                        pDevExt->BarLength[bar] = descriptor->u.Memory48.Length48;
                                } else if ((descriptor->Flags & CM_RESOURCE_MEMORY_LARGE_64) == CM_RESOURCE_MEMORY_LARGE_64) {
                                        pDevExt->BarPhysicalAddress[bar] = descriptor->u.Memory64.Start;
                                        pDevExt->BarLength[bar] = descriptor->u.Memory64.Length64;
                                } else {
                                        // Error!
                                        pDevExt->NumberOfBARS--;
                                        pDevExt->BarVirtualAddress[bar] = NULL;
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Large Memory space detected, error, Flags =  0x%x\n", descriptor->Flags));
                                        break;
                                }
                                // set system bar size
                                if (AddressSizeDetected == FALSE) {
                                        AddressSizeDetected = TRUE;
                                        pDevExt->Use64BitAddresses = TRUE;
                                }
                                // map the memory
#if 1

                                pDevExt->BarVirtualAddress[bar] = MmMapIoSpace(descriptor->u.Memory.Start, descriptor->u.Memory.Length, MmNonCached);
#else                           // Windows 10 use MmMapIoSpaceEx
                                pDevExt->BarVirtualAddress[bar] = MmMapIoSpaceEx(descriptor->u.Memory.Start, descriptor->u.Memory.Length, PAGE_NOCACHE || PAGE_READWRITE);
#endif
                                // Save in BoardCfg space
                                pDevExt->BoardConfig.PciConfig.BarCfg[bar] = BAR_CFG_BAR_PRESENT | BAR_CFG_BAR_TYPE_MEMORY | BAR_CFG_MEMORY_64BIT_CAPABLE;
                                if ((descriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) == CM_RESOURCE_MEMORY_PREFETCHABLE) {
                                        pDevExt->BoardConfig.PciConfig.BarCfg[bar] |= BAR_CFG_MEMORY_PREFETCHABLE;
                                }
                                // compute and store bar size in (2**n)
                                barSize = 0;
                                barLength = pDevExt->BarLength[bar] - 1;
                                while (barLength > 0) {
                                        barSize++;
                                        barLength >>= 1;
                                }
                                pDevExt->BoardConfig.PciConfig.BarCfg[bar] |= ((UINT64) barSize << BAR_CFG_BAR_SIZE_OFFSET);

                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Large Memory port found, BAR #%d, Length=0x%x, Size=%d\n", bar, (UINT32) pDevExt->BarLength[bar], barSize));
                        }
                        break;

                case CmResourceTypeInterrupt:
                        if ((descriptor->Flags & (CM_RESOURCE_INTERRUPT_MESSAGE | CM_RESOURCE_INTERRUPT_LATCHED)) == (CM_RESOURCE_INTERRUPT_MESSAGE | CM_RESOURCE_INTERRUPT_LATCHED)) {
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Message Signaled Interrupts found %u\n", numIrq));
                        }
                        numIrq++;
                        break;
                default:
                        // Ignore the other resources
                        break;
                }
        }
        return status;
}

/*! DMADriverBoardConfigInit
 *
 * \brief Get the PCI Configuration from the card and initialize the
 * BoardConfig structure.
 * \note Some of the BoardConfig structure is configured when the resources
 * are being initialized.
 * \param pDevExt
 * \return status
 */
NTSTATUS DMADriverBoardConfigInit(PDEVICE_EXTENSION pDevExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        INT32 size;
        PUINT8 pPCIConfSpace;
        UINT8 offset;
        UINT8 MessageID;
        PPCI_MSIX_CAPABILITY pPciMSIXCapabilities;
        UINT16 MsgCtrl;

        // Get the common config data
        size = pDevExt->BusInterface.GetBusData(pDevExt->BusInterface.Context, PCI_WHICHSPACE_CONFIG, &pDevExt->BoardConfig.PciConfig, 0,       // offset
                                                sizeof(PCI_CONFIG_HEADER));
        if (pDevExt->BoardConfig.PciConfig.CapabilitiesPtr != 0) {
                pPCIConfSpace = (PUINT8) & pDevExt->BoardConfig.PciConfig;
                offset = pDevExt->BoardConfig.PciConfig.CapabilitiesPtr;
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL CapabilitiesPtr 0x%02x\n", offset));
                do {
                        MessageID = pPCIConfSpace[offset];

                        if (MessageID == PCI_CAPABILITY_ID_MSI) {
                                pDevExt->MSISupported = TRUE;
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL MSI supported, offset 0x%02x\n", offset));
                        }

                        if (MessageID == PCI_CAPABILITY_ID_MSIX) {
                                pPciMSIXCapabilities = (PPCI_MSIX_CAPABILITY) & pPCIConfSpace[offset];
                                // This is the MSIX Config info, offset is pointing to the Message Control word
                                MsgCtrl = pPciMSIXCapabilities->MessageControl;
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL MSIX supported, Control 0x%04x offset %02x next 0x%02x TableOffsetBIR 0x%08x PBAOffsetBIR 0x%08x\n",
                                       MsgCtrl, offset, pPciMSIXCapabilities->PCIeNextCapabilityPtr, pPciMSIXCapabilities->TableOffsetBIR,
                                       pPciMSIXCapabilities->PBAOffsetBIR));
                                pDevExt->MSIXSupported = TRUE;

                                // If the number of vectors is still set to default, use the table size instead.
                                if (pDevExt->MSINumberVectors == MAX_NUM_DMA_ENGINES) {
                                        pDevExt->MSINumberVectors = (MsgCtrl & MSG_CTRL_TABLE_SIZE_MASK) + 1;
                                }
                        }
                        offset++;
                        offset = pPCIConfSpace[offset];
                } while ((offset != 0x00) && (offset < sizeof(PCI_CONFIG_HEADER)));
        }
       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL MSI num vectors %u\n", pDevExt->MSINumberVectors));

        // For Legacy, both MSI & MSI X Overrides have to = 0.
        if (pDevExt->MSIXSupported && pDevExt->MSIXOverride) {
                pDevExt->MSIXSupported = TRUE;
                pDevExt->BoardConfig.CardStatusInfo |= CARD_IRQ_MSIX_ENABLED;
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL MSIX supported\n"));
        } else {
                pDevExt->MSIXSupported = FALSE;
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL MSIX not supported\n"));
        }

        if (pDevExt->MSISupported && pDevExt->MSIOverride) {
                pDevExt->MSISupported = TRUE;
                pDevExt->BoardConfig.CardStatusInfo |= CARD_IRQ_MSI_ENABLED;
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL MSI supported\n"));
        } else {
                pDevExt->MSISupported = FALSE;
               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL MSI not supported\n"));
        }

        return status;
}

/*! DmaDriverGetRegHardwareInfo
 *
 * \brief - Retrieve information about the driver hardware parameters from the registry (if any)
 *
 * \note Windows XP does not support MSI / MSI-X interrupts. In fact it will mistakenly succeed
 *    in allocation multiple interrupts.
 * \param pDevExt     Pointer to the per instance data store for the driver
 * \return status (NTSTATUS) - STATUS_SUCCESS if all goes well
 */
NTSTATUS DMADriverGetRegHardwareInfo(PDEVICE_EXTENSION pDevExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        WDFKEY hKey;
        WDFKEY subkey;
        WDFKEY subsubkey;
        UINT32 MSISupported;
        UINT32 MSIXSupported;
        UINT32 MSILimit;

        // Open the HKLM/SYSTEM/CurrentControlSet/Enum/VEN_xxxx...\Device Parameters
        status = WdfDeviceOpenRegistryKey(pDevExt->Device, PLUGPLAY_REGKEY_DEVICE, STANDARD_RIGHTS_ALL, WDF_NO_OBJECT_ATTRIBUTES, &hKey);
        if (status == STATUS_SUCCESS) {
                // Now open the Interrupt Management Sub Key
                status = WdfRegistryOpenKey(hKey, &InterruptManagementName, KEY_READ, WDF_NO_OBJECT_ATTRIBUTES, &subkey);
                if (status == STATUS_SUCCESS) {
                        // Open the MessageSignaledInterruptProperties sub key
                        status = WdfRegistryOpenKey(subkey, &MSIPropertiesName, KEY_READ, WDF_NO_OBJECT_ATTRIBUTES, &subsubkey);
                        if (status == STATUS_SUCCESS) {
                                // See if there is a MSIXSupported entry
                                status = WdfRegistryQueryULong(subsubkey, &MSIXSupportedName, (PULONG) & MSIXSupported);
                                if (status == STATUS_SUCCESS) {
                                        // This is intended to override the hardware config block, unless MSIX is not available (Spartan 6).
                                        if (MSIXSupported) {
                                                pDevExt->MSIXOverride = TRUE;
                                        } else {
                                                pDevExt->MSIXOverride = FALSE;
                                        }
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL MSIXOverride %s\n", pDevExt->MSIXOverride==TRUE?"TRUE":"FALSE"));
                                }
                                else {
                                   KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Could not open MSIXSupported\n"));
                                }
                                // See if there is a MSISupported entry
                                status = WdfRegistryQueryULong(subsubkey, &MSISupportedName, (PULONG) & MSISupported);
                                if (status == STATUS_SUCCESS) {
                                        if (MSISupported) {
                                                pDevExt->MSIOverride = TRUE;
                                        } else {
                                                pDevExt->MSIOverride = FALSE;
                                        }
                                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL MSIOverride %s\n", pDevExt->MSIOverride==TRUE?"TRUE":"FALSE"));

                                        // Now see if there is a limit to the number of vectors
                                        status = WdfRegistryQueryULong(subsubkey, &MSILimitName, (PULONG) & MSILimit);
                                        if (status == STATUS_SUCCESS) {
                                                if (MSILimit > 0) {
                                                        pDevExt->MSINumberVectors = MSILimit;
                                                } else {
                                                        pDevExt->MSINumberVectors = 1;
                                                }
                                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL MessageNumberLimit %u\n", pDevExt->MSINumberVectors));
                                        }
                                        else {
                                           KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Could not open MessageNumberLimit\n"));
                                        }
                                }
                                else {
                                   KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Could not open MSISupported\n"));
                                }
                        }
                        else {
                           KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Could not open MessageSignaledInterruptProperties\n"));
                        }
                }
                else {
                   KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Could not open Interrupt Management\n"));
                }
        }
        else {
           KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL Could not open registry\n"));
        }
        return (status);
}

/*!DmaDriverGetRegistryInfo -
 *
 * \brief Retieve information about the driver parameters from the registry (if any)
 * \param pDevExt     Pointer to the per instance data store for the driver
 * \return status (NTSTATUS) - STATUS_SUCCESS if all goes well
*/
NTSTATUS DMADriverGetRegistryInfo(PDEVICE_EXTENSION pDevExt)
{
        NTSTATUS status = STATUS_SUCCESS;
        WDFKEY hKey;
        UINT32 InterruptMode;
        UINT32 NumberDMADescr;

        // Open the Registry for our entry
        status = WdfDriverOpenParametersRegistryKey(WdfGetDriver(), STANDARD_RIGHTS_ALL, WDF_NO_OBJECT_ATTRIBUTES, &hKey);
        if (NT_SUCCESS(status)) {
                // Get the Interrupt Mode
                if (WdfRegistryQueryULong(hKey, &InterruptModeName, (PULONG) & InterruptMode) == STATUS_SUCCESS) {
                        pDevExt->InterruptMode = InterruptMode;
                       KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL InterruptMode =  0x%x\n", pDevExt->InterruptMode));

                }
                // Get the Number of DMA Descriptor override
                if (WdfRegistryQueryULong(hKey, &NumberDMADescName, (PULONG) & NumberDMADescr) == STATUS_SUCCESS) {
                        if (NumberDMADescr > MINIMUM_NUMBER_DESCRIPTORS) {
                                pDevExt->NumberOfDescriptors = NumberDMADescr;
                               KdPrintEx((1, DPFLTR_ERROR_LEVEL, "USL NumberDMADescr =  0x%x\n", pDevExt->NumberOfDescriptors));
                        }
                }
                WdfRegistryClose(hKey);
        }
        else
        {
           KdPrintEx((1, DPFLTR_INFO_LEVEL,"Could not open registry\n"));
        }

        return (status);
}
