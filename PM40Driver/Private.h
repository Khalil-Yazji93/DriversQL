#pragma once
// -------------------------------------------------------------------------
//
// PRODUCT:            DMA Driver
// MODULE NAME:        Private.h
//
// MODULE DESCRIPTION:
//
// Contains the private defines for the project.
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

#ifndef PRIVATE_H_
#define PRIVATE_H_

#define TRACE_ENABLED            0

#define IRQL_CALLBACK            1

#define MAX_NUMBER_OF_BARS         6

#define DMA_TYPE_PACKET_SEND    0x02
#define DMA_TYPE_PACKET_RECV    0x04
#define DMA_TYPE_PACKET_WRITE   0x02
#define DMA_TYPE_PACKET_READ    0x04

// MSI-X Capability Defines
#define    MSG_CTRL_MSIX_ENABLE        0x8000
#define    MSG_CTRL_FUNC_MASK_VECTORS    0x4000
#define MSG_CTRL_TABLE_SIZE_MASK    0x03FF

#define MSIX_TABLE_OFFSET_MASK        0xFFFFFFF8
#define MSIX_TABLE_BIR_MASK            0x00000007

#define PBA_OFFSET_MASK                0xFFFFFFF8
#define PBA_BIR_MASK                0x00000007

#define _NELEM(arr)                 (sizeof(arr) / sizeof(arr[0]))
#ifdef CONFIG_X86_64
#define _OFFSETOF(t,m)              ((UINT64) &((t *)0)->m)
#else
#define _OFFSETOF(t,m)              (&((t *)0)->m)
#endif

static _inline void mdelay(UINT32 msec)
{
        LARGE_INTEGER StartTime;
        LARGE_INTEGER CurrentTime;
        LARGE_INTEGER timeout;

        KeQuerySystemTime(&StartTime);
        CurrentTime.QuadPart = StartTime.QuadPart;
        timeout.QuadPart = StartTime.QuadPart + ((INT64) msec * 1000);

        while (CurrentTime.QuadPart < timeout.QuadPart) {
                KeQuerySystemTime(&CurrentTime);
        }
}

typedef struct _DMA_XFER {
        WDFREQUEST Request;
        SIZE_T bytesTransferred;
        UINT64 CardAddress;
        UINT64 UserControl;
        PMDL pMdl;
        UINT32 Mode;
        UINT32 PacketStatus;
} DMA_XFER, *PDMA_XFER;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DMA_XFER, DMAXferContext)

/* The device extension for a DMA Engine */
typedef struct _DMA_ENGINE_DEVICE_EXTENSION {
        UINT8 DmaEngine;
        UINT8 DmaType;          // Block or Packet
        WDF_DMA_DIRECTION DmaDirection;
        UINT32 NumberOfDescriptors;
        LONG NumberOfUsedDescriptors;
        UINT32 DMAEngineMSIVector;
        WDFINTERRUPT Interrupt;
        size_t MaximumTransferLength;

        WDFCOMMONBUFFER DescCommonBuffer;
        WDFQUEUE TransactionQueue;
        WDFDMAENABLER DmaEnabler;
        WDFDMAENABLER DmaEnabler32BitOnly;
        WDFDMATRANSACTION DmaTransaction;
        WDFREQUEST DmaRequest;
        WDFDPC CompletionDpc;

        PDMA_ENGINE_STRUCT pDmaEng;     // Pointer to the DMA Control registers in BAR 0

        PDMA_DESCRIPTOR_STRUCT pHWDescriptorBase;
        PHYSICAL_ADDRESS pHWDescriptorBasePhysical;     // Shared for both Block and Packet Modes
        PDRIVER_DESC_STRUCT pDrvDescBase;       // Pointer to the base memory of the Driver Descs
        PDRIVER_DESC_STRUCT pNextDesc;  // Pointer to the front of the queue
        PDRIVER_DESC_STRUCT pTailDesc;  // Pointer to the back of the queue
        PDRIVER_DESC_STRUCT pIrqDesc; // last descriptor in which IRQ bits were set to detect DMA overrun

        WDFSPINLOCK DmaSpinLock;       // For protecting HW queues

        KMUTEX ThreadMutex;             // Block multiple threads from entering a critical region.

        PDMA_ADAPTER pReadDmaAdapter;

        UINT8 TimeoutCount;
        UINT8 bAddressablePacketMode;
        BOOLEAN bDescriptorAllocSuccess;
        UINT32 PacketMode;

        BOOLEAN bFreeRun;
        UINT16 DMAEngineStatus;

        // Performance counters
        UINT64 BytesInLastSecond;
        UINT64 BytesInCurrentSecond;
        UINT64 HardwareTimeInLastSecond;
        UINT64 HardwareTimeInCurrentSecond;
        UINT64 DMAInactiveTime;
        UINT64 IntsInLastSecond;
        UINT64 DPCsInLastSecond;

        PVOID UserVa;           // Mapped VA for the process
        PMDL PMdl;              // MDL used to map memory
} DMA_ENGINE_DEVICE_EXTENSION, *PDMA_ENGINE_DEVICE_EXTENSION;

/* The device extension for the device object */
typedef struct _DEVICE_EXTENSION {
        WDFDEVICE Device;

        // IOCTL Queue
        WDFQUEUE IoctlQueue;

        // Interrupt Object
        WDFINTERRUPT Interrupt[MAX_NUM_DMA_ENGINES+1];

        // Board Configuration Structure
        BOARD_CONFIG_STRUCT BoardConfig;

        // PCI Resources
        BUS_INTERFACE_STANDARD BusInterface;    // PCI Bus interface
        UINT8 NumberOfBARS;
        BOOLEAN Use64BitAddresses;
        BOOLEAN MSISupported;
        BOOLEAN MSIXSupported;
        BOOLEAN MSIOverride;
        BOOLEAN MSIXOverride;
        UINT8 BarType[MAX_NUMBER_OF_BARS];
        PHYSICAL_ADDRESS BarPhysicalAddress[MAX_NUMBER_OF_BARS];
        UINT64 BarLength[MAX_NUMBER_OF_BARS];
        PVOID BarVirtualAddress[MAX_NUMBER_OF_BARS];

        // DMA Resources
        UINT32 NumberOfDescriptors;
        size_t MaximumDmaTransferLength;
        PBAR0_REGISTER_MAP_STRUCT pDmaRegisters;
        PDMA_ENGINE_DEVICE_EXTENSION pDmaEngineDevExt[MAX_NUM_DMA_ENGINES];
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExtMSIVector[MAX_NUM_DMA_ENGINES];
        PDEVICE_OBJECT PhysicalDeviceObject;
        PDEVICE_OBJECT FunctionalDeviceObject;
        UINT32 NumIRQVectors;
        UINT32 NumberDMAEngines;
        UINT32 MSINumberVectors;

        // Watchdog timer Resources
        WDFTIMER WatchdogTimer;

        // User Interrupt Resources
        KTIMER UsrIntReqTimer;
        KDPC UsrIntReqDpc;
        WDFREQUEST UsrIntRequest;
        WDFSPINLOCK UsrIntSpinLock;
        WDFDPC UsrIrqDpc;
        UINT32 InterruptMode;
        /*
         * Used like a counting semaphore. The interrupt producer increments, the consumer decrements.
         */
        UINT64 UsrIntCount;

        /*
         * Optionally mask off user interrupts during the IRQ service routine.
         */
        BOOLEAN UsrIntMaskAfterIRQ;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION, DMADriverGetDeviceContext)

/* The device extension for the device object */
typedef struct _DPC_CTX {
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
} DPC_CTX, *PDPC_CTX;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DPC_CTX, DPCContext)

/* The device extension for the device object */
typedef struct _DPC_UICTX {
        PDEVICE_EXTENSION pDevExt;
} DPC_UICTX, *PDPC_UICTX;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DPC_UICTX, UIDPCContext)

/* The queue extension to point to the DMA Engine */
typedef struct _QUEUE_CTX {
        PDMA_ENGINE_DEVICE_EXTENSION pDmaExt;
} QUEUE_CTX, *PQUEUE_CTX;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CTX, QueueContext)
// InterruptStatus defines
#define IRQ_DMA_COMPLETE(x) ((UINT64) 0x0001 << (x))
// DMADriver.c Prototypes
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD DMADriverEvtDeviceAdd;
EVT_WDF_DEVICE_CONTEXT_CLEANUP DMADriverEvtDriverContextCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE DMADriverEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE DMADriverEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY DMADriverEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT DMADriverEvtDeviceD0Exit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_RESTART DMADriverEvtDeviceSelfManagedIoRestart;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP DMADriverEvtDeviceSelfManagedIoCleanup;

EVT_WDF_IO_IN_CALLER_CONTEXT DMADriverIoInCallerContext;

#define REQ_CTX_SIG     0xDEADBEEF

typedef struct _REQUEST_CONTEXT {
        UINT32 Signature;
        PMDL pMdl;
        PVOID pVA;
        UINT32 Length;
        UINT8 DMAEngine;
} REQUEST_CONTEXT, *PREQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, RequestContext)

void FreeReqCtx(PREQUEST_CONTEXT reqContext);

// Init.c Prototypes
EVT_WDF_IO_QUEUE_IO_READ DMADriverEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE DMADriverEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL DMADriverEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL DMADriverEvtIoReadWriteDeviceControl;

NTSTATUS DMADriverInitializeDeviceExtension(PDEVICE_EXTENSION pDevExt);

NTSTATUS DMADriverPrepareHardware(IN PDEVICE_EXTENSION pDevExt, IN WDFCMRESLIST ResourcesTranslated);

NTSTATUS DmaDriverBoardDmaRelease(PDEVICE_EXTENSION pDevExt, UINT8 DmaEngNum);

// BoardConfigHandling.c Prototypes

NTSTATUS GetBoardConfigDeviceControl(IN WDFDEVICE device, IN WDFREQUEST Request, IN size_t * pInfoSize);

NTSTATUS GetDmaEngineCapabilities(IN WDFDEVICE device, IN WDFREQUEST Request, IN size_t * pInfoSize);

NTSTATUS GetDmaPerfNumbers(IN WDFDEVICE device, IN WDFREQUEST Request, IN size_t * pInfoSize);

// Init.c Prototypes

NTSTATUS DMADriverBoardDmaInit(PDEVICE_EXTENSION pDevExt);

NTSTATUS DMADriverBoardConfigInit(PDEVICE_EXTENSION pDevExt);

// IrqHandling.c Prototypes

NTSTATUS DMADriverInterruptCreate(IN PDEVICE_EXTENSION pDevExt);

VOID DMADriverAckDmaInterrupt(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

EVT_WDF_INTERRUPT_ISR DMADriverInterruptIsr;
EVT_WDF_INTERRUPT_ISR DMADriverInterruptUserIRQMSIIsr;
EVT_WDF_INTERRUPT_DPC DMADriverInterruptDpc;
EVT_WDF_INTERRUPT_DPC DMADriverUsrIntDpc;
EVT_WDF_INTERRUPT_ENABLE DMADriverInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE DMADriverInterruptDisable;

// IoctlHandling.c Prototypes

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL DMADriverDmaEvtIoDeviceControl;

INT32 GetDMAEngineContext(PDEVICE_EXTENSION pDevExt, UINT32 EngineNum, PDMA_ENGINE_DEVICE_EXTENSION * ppDmaExt);

WDFREQUEST FindRequestByRequest(IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, IN WDFREQUEST MatchReq);

// PacketIoctl.c Prototypes

NTSTATUS PacketBufferAllocate(IN PDEVICE_EXTENSION pDevExt, IN WDFREQUEST Request);

NTSTATUS PacketBufferRelease(IN PDEVICE_EXTENSION pDevExt, IN WDFREQUEST Request);

NTSTATUS PacketFlushRequests(IN PDEVICE_EXTENSION pDevExt, IN WDFREQUEST Request, IN size_t InputBufferLength);

NTSTATUS ResetDMAEngineIoctlHandler(IN PDEVICE_EXTENSION pDevExt, IN WDFREQUEST Request);

// PacketInit.c Prototypes

NTSTATUS DMADriverIntiializeDMADescriptors(IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, IN UINT32 NumberDescriptors, IN UINT32 DescFlags);

NTSTATUS AllocateRxBuffer(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

NTSTATUS InitializeRxDescriptors(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, IN PVOID RetVirtAddr, IN UINT32 AllocSize);

NTSTATUS InitializeAddressablePacketDescriptors(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

NTSTATUS InitializeTxDescriptors(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

NTSTATUS InitializeDriverRxDescriptors(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, IN SIZE_T AllocSize, IN UINT32 MaxPacketSize);

VOID FreeRxDescriptors(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

VOID FreeDriverRxDescriptors(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

VOID ShutdownDMAEngine(IN PDEVICE_EXTENSION, IN PDMA_ENGINE_DEVICE_EXTENSION);

VOID HardResetDMAEngine(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

// PacketDMA.c Prototypes
EVT_WDF_DPC PacketS2CDpc;
EVT_WDF_DPC PacketC2SDpc;

VOID PacketS2CDpc(IN WDFDPC Dpc);

VOID PacketC2SDpc(IN WDFDPC Dpc);

NTSTATUS PacketStartSend(IN WDFREQUEST Request, IN PDEVICE_EXTENSION pDevExt, IN PPACKET_SEND_STRUCT pSendPacket);

NTSTATUS PacketStartWrite(IN WDFREQUEST Request, IN PDEVICE_EXTENSION pDevExt, IN PPACKET_WRITE_STRUCT pWritePacket);

NTSTATUS PacketStartRead(IN WDFREQUEST Request, IN PDEVICE_EXTENSION pDevExt, IN PPACKET_READ_STRUCT pReadPacket);

NTSTATUS PacketProcessCompletedReceives(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

NTSTATUS PacketProcessCompletedReceiveNB(IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, IN WDFREQUEST Request);

NTSTATUS PacketProcessReturnedDescriptors(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, IN UINT32 ReturnToken);

NTSTATUS PacketReadComplete(IN PDEVICE_EXTENSION pDevExt, IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE PacketReadRequestCancel;

VOID PacketReadRequestCancel(IN WDFQUEUE Queue, IN WDFREQUEST Request);

NTSTATUS PacketProcessCompletedFreeRunDescriptors(IN PDMA_ENGINE_DEVICE_EXTENSION pDmaExt, PPACKET_RECVS_STRUCT pPacketRecvs);

// WatchdogTimerHandling.c Prototypes
NTSTATUS DMADriverWatchdogTimerInit(IN WDFDEVICE Device);

EVT_WDF_TIMER DMADriverWatchdogTimerCall;
EVT_WDF_TIMER DMADriverUsrIntReqTimerCall;

VOID DMADriverWatchdogTimerStart(IN WDFDEVICE Device);

VOID DMADriverWatchdogTimerStop(IN WDFDEVICE Device);

VOID DMADriverWatchdogTimerDelete(IN WDFDEVICE Device);

// User Interrupt Timer Prototypes
EVT_WDF_DPC UserIRQDpc;

NTSTATUS DMADriverUsrIntReqTimerInit(IN PDEVICE_EXTENSION pDevExt);

VOID DMADriverUsrIntReqTimerStart(IN PDEVICE_EXTENSION pDevExt, IN UINT32 dwTimeoutMilliSec);

VOID DMADriverUsrIntReqTimerStop(IN PDEVICE_EXTENSION pDevExt, BOOLEAN Wait);

VOID DMADriverUsrIntReqTimerDelete(IN PDEVICE_EXTENSION pDevExt);

VOID UserIrqComplete(PDEVICE_EXTENSION pDevExt, NTSTATUS Status);

// ReadWriteHandling.c Prototypes

VOID ReadWriteMemAccess(IN PDEVICE_EXTENSION pDevExt, IN WDFREQUEST Request, IN WDF_DMA_DIRECTION Rd_Wr_n);

VOID ReadWritePCIConfig(IN PDEVICE_EXTENSION pDevExt, IN WDFREQUEST Request, WDF_DMA_DIRECTION Rd_Wr_n);

// Thread blocking routine
VOID DMADriverLockInit(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);
VOID DMADriverLock(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);
VOID DMADriverUnlock(PDMA_ENGINE_DEVICE_EXTENSION pDmaExt);

#endif                          /* PRIVATE_H_ */
