#pragma once
/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
PURPOSE.

Module Name:

Private.h

Abstract:

Environment:

Kernel mode

--*/
#include "Include\PlxTypes.h"

#if !defined(_PCI9030_H_)
#define _PCI9030_H_

extern ULONG g_ulIsrCount;

#define MAX_NUMBER_OF_BARS 				3
#define MAX_BARS                        3       // TYPE-0 configuration header values.
#define PCI_STD_CONFIG_HEADER_SIZE      64      // 64 Bytes is the standard header size

#define PLX_CHIP_TYPE                       0x9030
#define PLX_DRIVER_NAME                     "Plx9030"
#define PLX_DRIVER_NAME_UNICODE             L"Plx9030"
#define PLX_LOG_FILE_UNICODE                L"\\SystemRoot\\Plx9030.txt" // Log file name
#define MAX_PLX_REG_OFFSET                  PCI9030_MAX_REG_OFFSET       // Max PLX register offset
#define DEFAULT_SIZE_COMMON_BUFFER          0                            // Default size of Common Buffer
#define PLX_CHIP_CONTEXT_TOTAL_REGS         (0x58 / sizeof(U32))         // Number of regs to save/restore during power change

#define PLX_MAX_NAME_LENGTH                 0x20          // Max length of registered device name
#define MIN_WORKING_POWER_STATE             PowerDeviceD2 // Minimum state required for local register access

// Macros for Memory access to/from user-space addresses
#define DEV_MEM_TO_USER_8( VaUser, VaDev, count)    READ_REGISTER_BUFFER_UCHAR(  (UCHAR*)(VaDev),  (UCHAR*)(VaUser),  (count))
#define DEV_MEM_TO_USER_16(VaUser, VaDev, count)    READ_REGISTER_BUFFER_USHORT((USHORT*)(VaDev), (USHORT*)(VaUser),  (count) >> 1)
#define DEV_MEM_TO_USER_32(VaUser, VaDev, count)    READ_REGISTER_BUFFER_ULONG(  (ULONG*)(VaDev),  (ULONG*)(VaUser),  (count) >> 2)
#define USER_TO_DEV_MEM_8( VaDev, VaUser, count)    WRITE_REGISTER_BUFFER_UCHAR(  (UCHAR*)(VaDev),  (UCHAR*)(VaUser), (count))
#define USER_TO_DEV_MEM_16(VaDev, VaUser, count)    WRITE_REGISTER_BUFFER_USHORT((USHORT*)(VaDev), (USHORT*)(VaUser), (count) >> 1)
#define USER_TO_DEV_MEM_32(VaDev, VaUser, count)    WRITE_REGISTER_BUFFER_ULONG(  (ULONG*)(VaDev),  (ULONG*)(VaUser), (count) >> 2)


// Macros for I/O port access
#define IO_PORT_READ_8(port)                        READ_PORT_UCHAR (  (UCHAR*)(port))
#define IO_PORT_READ_16(port)                       READ_PORT_USHORT( (USHORT*)(port))
#define IO_PORT_READ_32(port)                       READ_PORT_ULONG (  (ULONG*)(port))
#define IO_PORT_WRITE_8(port, val)                  WRITE_PORT_UCHAR(  (UCHAR*)(port),  (UCHAR)(val))
#define IO_PORT_WRITE_16(port, val)                 WRITE_PORT_USHORT((USHORT*)(port), (USHORT)(val))
#define IO_PORT_WRITE_32(port, val)                 WRITE_PORT_ULONG(  (ULONG*)(port),  (ULONG)(val))


// Macros for device memory access

#define DEV_MEM_READ_8                              READ_REGISTER_UCHAR
#define DEV_MEM_READ_16                             READ_REGISTER_USHORT
#define DEV_MEM_READ_32                             READ_REGISTER_ULONG
#define DEV_MEM_WRITE_8                             WRITE_REGISTER_UCHAR
#define DEV_MEM_WRITE_16                            WRITE_REGISTER_USHORT
#define DEV_MEM_WRITE_32                            WRITE_REGISTER_ULONG



// Macros for PLX chip register access
#define PLX_9000_REG_READ(pdx, offset)                   \
    DEV_MEM_READ_32(                                     \
        (U32*)(((U8*)((pdx)->PciBar[0].pVa)) + (offset)) \
        )

#define PLX_9000_REG_WRITE(pdx, offset, value)            \
    DEV_MEM_WRITE_32(                                     \
        (U32*)(((U8*)((pdx)->PciBar[0].pVa)) + (offset)), \
        (value)                                           \
        )




/***********************************************************
* The following definition sets the maximum size that an
* MDL can describe.  Although not documented in the DDK
* until recently, the memory allocated for an MDL is 64k,
* which limits the number of physical page entries allowed
* after the MDL.
*
* Assuming 4k page sizes, the limit calculation yields a
* size of just under 64MB on a 32-bit system.  On a system with
* 64-bit addressing, the max size per MDL is less than 32MB.
* The calculation for determining the max size is:
*
*   Total Mem = PAGE_SIZE * ((64k - sizeof(MDL)) / sizeof(PLX_UINT_PTR))
*
* The total size is decremented by an additional amount because
* Windows may still fail mappings in certain cases if the size
* MAX_MDL_SIZE is too large.
**********************************************************/
#define MAX_MDL_SIZE                        (PAGE_SIZE * ((65535 - sizeof(MDL)) / sizeof(PLX_UINT_PTR))) - (1 << 21)



/***********************************************************
* The following definition sets the maximum number of
* consecutive virtual addresses for a PCI BAR space.  Since
* individual MDLs are limited to 64MB, when mapping a space
* to obtain a user-mode virtual address, each individual
* MDL for a space must be mapped separately.  The virtual
* addresses must be saved for later unmapping.
*
* The following calculation determines the theoretical
* limitation of a user mapping for a PCI space:
*
*   Max mapping size = (Max # virt addresses) * (Max MDL size)
*
* If the max virtual address count is 5, for example, and
* each MDL is limited to 60MB, the theoretical limit that the
* driver can map will be a 300MB PCI space.
*
* NOTE: Increasing this number has no effect if a mapping
*       fails due to system resource constraints.  It is
*       primarily used to store multiple addresses so they
*       can later be unmapped properly.
**********************************************************/
#define MAX_VIRTUAL_ADDR                    10      // Max number of virtual addresses for a space



/***********************************************************
* The following definition determines the maximum size
* of all contiguous page-locked buffers the driver may try
* to allocate.
*
* The driver allocates a DMA adapter object for each
* device it owns.  When the adapter object is allocated,
* with IoGetDmaAdapter, the maximum DMA transfer length must
* be specified.  During creation of the DMA adapter object,
* the OS reserves enough resources (i.e. map registers) to
* handle simultaneously allocated buffers up to the size
* specified in IoGetDmaAdapter.  Since the DMA Adapter
* object is used to allocate common buffers, the total
* size is limited to the DMA length used in IoGetDmaAdapter.
*
* In other words, if IoGetDmaAdapter is called with a DMA
* length of 8MB, the driver can allocate, for example, only
* two 4MB buffers.  A call to allocate an additional buffer
* will fail since the 8MB limit is reached.  As a result,
* the definition below can be set to a large number, such
* as 100MB, so as not to severely limit buffer allocation.
*
* NOTE: The OS will only reserve enough resources to handle
*       a large-enough buffer.  It will NOT actually reserve
*       memory.  Available memory is determined at the time
*       of the allocation request and requests may still
*       fail, depending upon system resources.
**********************************************************/
#define PHYS_MEM_MAX_SIZE_ALL               (100 << 20)     // Max total allowable size of physically memory to allocate



// Information stored in Registry
typedef struct _PLX_REGISTRY_INFO
{
	U32 Size_CommonBuffer;
} PLX_REGISTRY_INFO;


// PCI Interrupt wait object
typedef struct _PLX_WAIT_OBJECT
{
	LIST_ENTRY  ListEntry;
	VOID       *pOwner;
	U32         Notify_Flags;                   // Registered interrupt(s) for notification
	U32         Notify_Doorbell;                // Registered doorbell interrupt(s) for notification
	U32         Source_Ints;                    // Interrupt(s) that caused notification
	U32         Source_Doorbell;                // Doorbells that caused notification
	PKEVENT     pKEvent;
} PLX_WAIT_OBJECT;


// Argument for interrupt source access functions
typedef struct _PLX_INTERRUPT_DATA
{
	struct _DEVICE_EXTENSION *pdx;
	U32                       Source_Ints;
	U32                       Source_Doorbell;
} PLX_INTERRUPT_DATA;


// Information about contiguous, page-locked buffers
typedef struct _PLX_PHYS_MEM_OBJECT
{
	LIST_ENTRY   ListEntry;
	VOID        *pOwner;
	VOID        *pKernelVa;
	U64          CpuPhysical;                   // CPU Physical Address
	U64          BusPhysical;                   // Bus Physical Address
	U32          Size;                          // Buffer size
	PMDL         pMdl;
	BOOLEAN      bCacheable;
	LIST_ENTRY   List_Mappings;                 // List of mappings for this physical memory
	KSPIN_LOCK   Lock_MappingsList;             // Spinlock for mappings list
} PLX_PHYS_MEM_OBJECT;


// Physical memory user mapping
typedef struct _PLX_USER_MAPPING
{
	LIST_ENTRY  ListEntry;
	VOID       *pOwner;
	U8          BarIndex;
	VOID       *pUserVa[MAX_VIRTUAL_ADDR];
} PLX_USER_MAPPING;


// PCI BAR Space information
typedef struct _PLX_PCI_BAR_INFO
{
	ULONG64  *pVa;                      // BAR Kernel Virtual Address
	PLX_PCI_BAR_PROP  Properties;               // BAR Properties
	PMDL              pMdl;                     // MDL for the BAR space
} PLX_PCI_BAR_INFO;


// DMA channel information 
typedef struct _PLX_DMA_INFO
{
	VOID                *pOwner;                // Object that requested to open the channel
	BOOLEAN              bOpen;                 // Flag to note if DMA channel is open
	BOOLEAN              bSglPending;           // Flag to note if an SGL DMA is pending
	BOOLEAN              bLocalAddrConstant;    // Flag to keep track if local address remains constant
	PMDL                 pMdl;                  // MDL of the user buffer for locking and unlocking
	PLX_PHYS_MEM_OBJECT  SglBuffer;             // Current SGL descriptor list buffer
} PLX_DMA_INFO;


// Argument for ISR synchronized register access
typedef struct _PLX_REG_DATA
{
	struct _DEVICE_EXTENSION *pdx;
	U32                       offset;
	U32                       BitsToSet;
	U32                       BitsToClear;
} PLX_REG_DATA;

#ifndef PACKED
#define PACKED                      
#endif /* PACKED */

// RW_PCI_CONFIG_STRUCT
// PCI Config Read/Write Structure - Information for reading or writing CI onfiguration space
typedef struct _RW_PCI_CONFIG_STRUCT
{
	UINT32  Offset;             // Byte starting offset in application buffer
	UINT32	Length;             // Transaction length in bytes
#ifndef __WINNT__
	PUINT8  Buffer;             // Buffer Pointer
#endif  // Linux Only
} RW_PCI_CONFIG_STRUCT, *PRW_PCI_CONFIG_STRUCT;


#define PCI_DEVICE_SPECIFIC_SIZE 192    // Device specific bytes [64..256].

typedef struct _PCI_CONFIG_HEADER
{
	UINT16	VendorId;           // VendorID[15:0] 0-1
	UINT16	DeviceId;           // DeviceID[15: 2-3
	UINT16	Command;            // Command[15:0] 4-5
	UINT16	Status;             // Status[15:0] 6-7
	UINT8   RevisionId;         // RevisionId[7:0] 8
	UINT8	Interface;          // Interface[7:0] 9
	UINT8	SubClass;           // SubClass[7:0] 0xA
	UINT8	BaseClass;          // BaseClass[7:0] 0xB
	UINT8	CacheLineSize;		// 0xC
	UINT8	LatencyTimer;		// 0xD
	UINT8	HeaderType;			// 0xE
	UINT8	BIST;				// 0xF
	UINT32	BarCfg[MAX_BARS];   // BAR[5:0] Configuration	0x10, 0x14, 0x18, 0x1C, 0x20, 0x24
	UINT32	CardBusCISPtr;		// 0x28
	UINT16	SubsystemVendorId;  // SubsystemVendorId[15:0] 0x2C
	UINT16	SubsystemId;        // SubsystemId[15:0] 0x2E
	UINT32	ExpRomCfg;          // Expansion ROM Configuration 0x30
	UINT8	CapabilitiesPtr;	// 0x34
	UINT8	Reserved1[3];       // 0x35
	UINT32	Reserved2;          // 0x38
	UINT8	InterruptLine;      // 0x3C
	UINT8	InterruptPin;       // 0x3D
	UINT8	MinimumGrant;       // 0x3E
	UINT8	MaximumLatency;     // 0x3F
	UINT8	DeviceSpecific[PCI_DEVICE_SPECIFIC_SIZE];
} PACKED PCI_CONFIG_HEADER, *PPCI_CONFIG_HEADER;

typedef struct _BOARD_CONFIG_STRUCT
{
	UINT8   NumDmaWriteEngines; // Number of S2C DMA Engines
	UINT8   FirstDmaWriteEngine;// Number of 1st S2C DMA Engine
	UINT8   NumDmaReadEngines;  // Number of C2S DMA Engines
	UINT8   FirstDmaReadEngine; // Number of 1st C2S DMA Engine
	UINT8   DMARegistersBAR;    // BAR Number where DMA Registers reside
	UINT8   reserved1;			// Reserved
	UINT8	DriverVersionMajor;	// Driver Major Revision number
	UINT8	DriverVersionMinor;	// Driver Minor Revision number
	UINT8	DriverVersionSubMinor;	// Driver Sub Minor Revision number 
	char	DriverVersionBuildNumber[12];	// Driver Build number
	UINT32	CardStatusInfo;			// BAR 0 + 0x4000 DMA_Common_Control_and_Status
	UINT32	DMABackEndCoreVersion;	// DMA Back End Core version number
	UINT32	PCIExpressCoreVersion;	// PCI Express Core version number
	UINT32	UserVersion;		// User Version Number (optional)
								// The following structure is placed at the end due to packing issues in Linux
								// Do Not Move or the Get Board Cfg command will fail.
	struct _PCI_CONFIG_HEADER   PciConfig;
} PACKED BOARD_CONFIG_STRUCT, *PBOARD_CONFIG_STRUCT;

// Board Config Struc defines
#define CARD_IRQ_MSI_ENABLED                0x0008
#define CARD_IRQ_MSIX_ENABLED               0x0040
#define CARD_MAX_PAYLOAD_SIZE_MASK          0x0700
#define CARD_MAX_READ_REQUEST_SIZE_MASK     0x7000

// BarCfg defines
#define BAR_CFG_BAR_SIZE_OFFSET     	    (24)
#define BAR_CFG_BAR_PRESENT				    0x0001
#define BAR_CFG_BAR_TYPE_MEMORY			    0x0000
#define BAR_CFG_BAR_TYPE_IO		 		    0x0002
#define BAR_CFG_MEMORY_PREFETCHABLE		    0x0004
#define BAR_CFG_MEMORY_64BIT_CAPABLE	    0x0008

// ExpRomCfg defines
#define EXP_ROM_BAR_SIZE_OFFSET	            (24)
#define EXP_ROM_PRESENT			            0x0001
#define EXP_ROM_ENABLED			            0x0002

//
// The device extension for the device object
//
typedef struct _DEVICE_EXTENSION {

	WDFDEVICE               Device;

	// Following fields are specific to the hardware
	// Configuration
	BOARD_CONFIG_STRUCT		BoardConfig;

	// PCI Resources
	BUS_INTERFACE_STANDARD	BusInterface;		// PCI Bus interface
	UINT8					NumberOfBARS;
	BOOLEAN					Use64BitAddresses;
	BOOLEAN					MSISupported;
	BOOLEAN					MSIXSupported;
	BOOLEAN					MSIOverride;
	BOOLEAN					MSIXOverride;
	UINT8					BarType[MAX_NUMBER_OF_BARS];
	PHYSICAL_ADDRESS		BarPhysicalAddress[MAX_NUMBER_OF_BARS];
	UINT64					BarLength[MAX_NUMBER_OF_BARS];
	PVOID					BarVirtualAddress[MAX_NUMBER_OF_BARS];

	// HW Resources

	PPCI9030_REGS           Regs;             // Registers address
	PUCHAR                  RegsBase;         // Registers base address
	ULONG                   RegsLength;       // Registers base length

	PUCHAR                  PortBase;         // Port base address
	ULONG                   PortLength;       // Port base length

	PUCHAR                  SRAMBase;         // SRAM base address
	ULONG                   SRAMLength;       // SRAM base length

	PUCHAR                  SRAM2Base;        // SRAM (alt) base address
	ULONG                   SRAM2Length;      // SRAM (alt) base length

	WDFINTERRUPT            Interrupt;     // Returned by InterruptCreate

	union {
		INT_CSR bits;
		ULONG   ulong;
	}                       IntCsr;

	union {
		DMA_CSR bits;
		UCHAR   uchar;
	}                       Dma0Csr;

	union {
		DMA_CSR bits;
		UCHAR   uchar;
	}                       Dma1Csr;


	// DmaEnabler
	WDFDMAENABLER           DmaEnabler;
	ULONG                   MaximumTransferLength;

	// IOCTL handling
	WDFQUEUE                ControlQueue;
	BOOLEAN                 RequireSingleTransfer;

#ifdef SIMULATE_MEMORY_FRAGMENTATION
	PMDL                    WriteMdlChain;
	PMDL                    ReadMdlChain;
#endif

	// Write
	WDFQUEUE                WriteQueue;
	WDFDMATRANSACTION       WriteDmaTransaction;

	ULONG                   WriteTransferElements;
	WDFCOMMONBUFFER         WriteCommonBuffer;
	size_t                  WriteCommonBufferSize;
	_Field_size_(WriteCommonBufferSize) PUCHAR WriteCommonBufferBase;
	PHYSICAL_ADDRESS        WriteCommonBufferBaseLA;  // Logical Address

													  // Read
	ULONG                   ReadTransferElements;
	WDFCOMMONBUFFER         ReadCommonBuffer;
	size_t                  ReadCommonBufferSize;
	_Field_size_(ReadCommonBufferSize) PUCHAR ReadCommonBufferBase;
	PHYSICAL_ADDRESS        ReadCommonBufferBaseLA;   // Logical Address

	WDFDMATRANSACTION       ReadDmaTransaction;
	WDFQUEUE                ReadQueue;

	ULONG                   HwErrCount;

	PKEVENT					pIntEvent;

	ULONG					SI10_IRQ_Source;


}  DEVICE_EXTENSION, *PDEVICE_EXTENSION;

//
// This will generate the function named PLxGetDeviceContext to be use for
// retreiving the DEVICE_EXTENSION pointer.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION, PLxGetDeviceContext)

#if !defined(ASSOC_WRITE_REQUEST_WITH_DMA_TRANSACTION)
//
// The context structure used with WdfDmaTransactionCreate
//
typedef struct TRANSACTION_CONTEXT {

	WDFREQUEST     Request;

} TRANSACTION_CONTEXT, *PTRANSACTION_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TRANSACTION_CONTEXT, PLxGetTransactionContext)

#endif

//
// Function prototypes
//
DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD PLxEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP PlxEvtDeviceCleanup;

EVT_WDF_OBJECT_CONTEXT_CLEANUP PlxEvtDriverContextCleanup;

EVT_WDF_DEVICE_D0_ENTRY PLxEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT PLxEvtDeviceD0Exit;
EVT_WDF_DEVICE_PREPARE_HARDWARE PLxEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE PLxEvtDeviceReleaseHardware;

EVT_WDF_IO_QUEUE_IO_READ PLxEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE PLxEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL PLxEvtIoDeviceControl;

EVT_WDF_INTERRUPT_ISR PLxEvtInterruptIsr;
EVT_WDF_INTERRUPT_DPC PLxEvtInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE PLxEvtInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE PLxEvtInterruptDisable;

VOID
ReadWriteMemAccess(
	IN PDEVICE_EXTENSION			pDevExt,
	IN WDFREQUEST					Request,
	IN WDF_DMA_DIRECTION			Rd_Wr_n
);

VOID
LoadFPGABinary(
	IN PDEVICE_EXTENSION			pDevExt,
	IN WDFREQUEST					Request
);

VOID
MailboxStringWrite(
	IN PDEVICE_EXTENSION			pDevExt,
	IN WDFREQUEST					Request
);

VOID
GetVersionNumber(
	IN PDEVICE_EXTENSION			pDevExt,
	IN WDFREQUEST					Request
);

VOID
GetISRCount(
	IN PDEVICE_EXTENSION			pDevExt,
	IN WDFREQUEST					Request
);

VOID
GetIRQSource(
	IN PDEVICE_EXTENSION			pDevExt,
	IN WDFREQUEST					Request
);

VOID
EnableInterruptCallback(
	IN PDEVICE_EXTENSION			pDevExt,
	IN WDFREQUEST					Request
);

NTSTATUS
PLxSetIdleAndWakeSettings(
	IN PDEVICE_EXTENSION FdoData
);

NTSTATUS
PLxInitializeDeviceExtension(
	IN PDEVICE_EXTENSION DevExt
);

VOID
PlxCleanupDeviceExtension(
	_In_ PDEVICE_EXTENSION DevExt
);

NTSTATUS
PLxPrepareHardware(
	IN PDEVICE_EXTENSION DevExt,
	IN WDFCMRESLIST     ResourcesTranslated
);

NTSTATUS
PLxScanPciResources(
	IN PDEVICE_EXTENSION DevExt,
	IN WDFCMRESLIST     ResourcesTranslated
);

NTSTATUS
PLxInitRead(
	IN PDEVICE_EXTENSION DevExt
);

NTSTATUS
PLxInitWrite(
	IN PDEVICE_EXTENSION DevExt
);

//
// WDFINTERRUPT Support
//
NTSTATUS
PLxInterruptCreate(
	IN PDEVICE_EXTENSION DevExt
);

VOID
ReadWritePCIConfig(
	IN PDEVICE_EXTENSION 	pDevExt,
	IN WDFREQUEST			Request,
	WDF_DMA_DIRECTION		Rd_Wr_n
);

VOID
PLxReadRequestComplete(
	IN WDFDMATRANSACTION  DmaTransaction,
	IN NTSTATUS           Status
);

VOID
PLxWriteRequestComplete(
	IN WDFDMATRANSACTION  DmaTransaction,
	IN NTSTATUS           Status
);

NTSTATUS
PLxInitializeHardware(
	IN PDEVICE_EXTENSION DevExt
);

VOID
PLxShutdown(
	IN PDEVICE_EXTENSION DevExt
);

EVT_WDF_PROGRAM_DMA PLxEvtProgramReadDma;
EVT_WDF_PROGRAM_DMA PLxEvtProgramWriteDma;

VOID
PLxHardwareReset(
	IN PDEVICE_EXTENSION    DevExt
);

NTSTATUS
PLxInitializeDMA(
	IN PDEVICE_EXTENSION DevExt
);


#ifdef SIMULATE_MEMORY_FRAGMENTATION
//
// Passed to ExAllocatePoolWithTag to track memory allocations
//
#define POOL_TAG 'x5x9'

//
// Constants for the MDL chains we use to simulate memory fragmentation.
//
#define MDL_CHAIN_LENGTH 8
#define MDL_BUFFER_SIZE  ((PCI9030_MAXIMUM_TRANSFER_LENGTH) / MDL_CHAIN_LENGTH)

C_ASSERT(MDL_CHAIN_LENGTH * MDL_BUFFER_SIZE == PCI9030_MAXIMUM_TRANSFER_LENGTH);

NTSTATUS
BuildMdlChain(
	_Out_ PMDL* MdlChain
);

VOID
DestroyMdlChain(
	_In_ PMDL MdlChain
);

VOID
CopyBufferToMdlChain(
	_In_reads_bytes_(Length) PVOID Buffer,
	_In_ size_t Length,
	_In_ PMDL MdlChain
);

VOID
CopyMdlChainToBuffer(
	_In_ PMDL MdlChain,
	_Out_writes_bytes_(Length) PVOID Buffer,
	_In_ size_t Length
);
#endif // SIMULATE_MEMORY_FRAGMENTATION

#pragma warning(disable:4127) // avoid conditional expression is constant error with W4

#endif  // _PCI9030_H_

