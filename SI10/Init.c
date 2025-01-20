/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
PURPOSE.

Module Name:

Init.c

Abstract:

Contains most of initialization functions

Environment:

Kernel mode

--*/

#include "precomp.h"

//#include "Init.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, PLxInitializeDeviceExtension)
#pragma alloc_text (PAGE, PLxPrepareHardware)
#pragma alloc_text (PAGE, PLxInitializeDMA)
#pragma alloc_text (PAGE, PLxScanPciResources)
#endif



PVOID LocalMmMapIoSpace(
	_In_ PHYSICAL_ADDRESS PhysicalAddress,
	_In_ SIZE_T NumberOfBytes
)
{
	typedef
		PVOID
		(*PFN_MM_MAP_IO_SPACE_EX) (
			_In_ PHYSICAL_ADDRESS PhysicalAddress,
			_In_ SIZE_T NumberOfBytes,
			_In_ ULONG Protect
			);

	UNICODE_STRING         name;
	PFN_MM_MAP_IO_SPACE_EX pMmMapIoSpaceEx;

	RtlInitUnicodeString(&name, L"MmMapIoSpaceEx");
	pMmMapIoSpaceEx = (PFN_MM_MAP_IO_SPACE_EX)(ULONG_PTR)MmGetSystemRoutineAddress(&name);

	if (pMmMapIoSpaceEx != NULL) {
		//
		// Call WIN10 API if available
		//        
		return pMmMapIoSpaceEx(PhysicalAddress,
			NumberOfBytes,
			PAGE_READWRITE | PAGE_NOCACHE);
	}

	//
	// Supress warning that MmMapIoSpace allocates executable memory.
	// This function is only used if the preferred API, MmMapIoSpaceEx
	// is not present. MmMapIoSpaceEx is available starting in WIN10.
	//
#pragma warning(suppress: 30029)
	return MmMapIoSpace(PhysicalAddress, NumberOfBytes, MmNonCached);
}


NTSTATUS
PLxInitializeDeviceExtension(
	IN PDEVICE_EXTENSION DevExt
)
/*++
Routine Description:

This routine is called by EvtDeviceAdd. Here the device context is
initialized and all the software resources required by the device is
allocated.

Arguments:

DevExt     Pointer to the Device Extension

Return Value:

NTSTATUS

--*/
{
	NTSTATUS    status;
	ULONG       dteCount;
	WDF_IO_QUEUE_CONFIG  queueConfig;

	PAGED_CODE();

	RtlZeroMemory((VOID*)&DevExt->BoardConfig, sizeof (BOARD_CONFIG_STRUCT));
	DevExt->BoardConfig.DriverVersionMajor = SI10_MAJOR_NUM;
	DevExt->BoardConfig.DriverVersionMinor = SI10_MINOR_NUM;
	DevExt->BoardConfig.DriverVersionSubMinor = SI10_SUBMINOR_NUM;
	RtlCopyMemory(DevExt->BoardConfig.DriverVersionBuildNumber, SI10_BUILD_NUM, sizeof(DevExt->BoardConfig.DriverVersionBuildNumber));

	DevExt->MaximumTransferLength = PCI9030_MAXIMUM_TRANSFER_LENGTH;
	if (DevExt->MaximumTransferLength > PCI9030_SRAM_SIZE) {
		DevExt->MaximumTransferLength = PCI9030_SRAM_SIZE;
	}
	KdPrint(("MaximumTransferLength %d", DevExt->MaximumTransferLength));

#ifndef SIMULATE_MEMORY_FRAGMENTATION
	//
	// Calculate the number of DMA_TRANSFER_ELEMENTS + 1 needed to
	// support the MaximumTransferLength.
	//
	dteCount = BYTES_TO_PAGES((ULONG)ROUND_TO_PAGES(
		DevExt->MaximumTransferLength) + PAGE_SIZE);
#else
	//
	// Each DTE describes a Scatter/Gather list element. When
	// SIMULATE_MEMORY_FRAGMENTATION is defined, we bounce I/O
	// request buffers to MDL chains to simulate memory
	// fragmentation. This means we might deal with larger
	// S/G lists than we would were the I/O buffer virtually
	// contiguous, so we need more space to write the DTEs.
	//
	dteCount = BYTES_TO_PAGES(((ULONG)
		ROUND_TO_PAGES(MDL_BUFFER_SIZE) + PAGE_SIZE) * MDL_CHAIN_LENGTH) + 1;
#endif

	KdPrint(( "Number of DTEs %d", dteCount));

	//
	// Set the number of DMA_TRANSFER_ELEMENTs (DTE) to be available.
	//
	DevExt->WriteTransferElements = dteCount;
	DevExt->ReadTransferElements = dteCount;

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
		WdfIoQueueDispatchSequential);

	queueConfig.EvtIoWrite = PLxEvtIoWrite;

	__analysis_assume(queueConfig.EvtIoStop != 0);
	status = WdfIoQueueCreate(DevExt->Device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&DevExt->WriteQueue);
	__analysis_assume(queueConfig.EvtIoStop == 0);

	if (!NT_SUCCESS(status)) {
		KdPrint((
			"WdfIoQueueCreate failed: %!STATUS!", status));
		return status;
	}

	status = WdfDeviceConfigureRequestDispatching(DevExt->Device,
		DevExt->WriteQueue,
		WdfRequestTypeWrite);

	if (!NT_SUCCESS(status)) {
		KdPrint((
			"DeviceConfigureRequestDispatching failed: %!STATUS!", status));
		return status;
	}

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
		WdfIoQueueDispatchSequential);

	queueConfig.EvtIoRead = PLxEvtIoRead;

	__analysis_assume(queueConfig.EvtIoStop != 0);
	status = WdfIoQueueCreate(DevExt->Device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&DevExt->ReadQueue);
	__analysis_assume(queueConfig.EvtIoStop == 0);

	if (!NT_SUCCESS(status)) {
		KdPrint((
			"WdfIoQueueCreate failed: %!STATUS!", status));
		return status;
	}

	status = WdfDeviceConfigureRequestDispatching(DevExt->Device,
		DevExt->ReadQueue,
		WdfRequestTypeRead);

	if (!NT_SUCCESS(status)) {
		KdPrint((
			"DeviceConfigureRequestDispatching failed: %!STATUS!", status));
		return status;
	}


	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
		WdfIoQueueDispatchSequential);

	queueConfig.EvtIoDeviceControl = PLxEvtIoDeviceControl;

	status = WdfIoQueueCreate(DevExt->Device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&DevExt->ControlQueue);
	if (!NT_SUCCESS(status)) {
		KdPrint((
			"WdfIoQueueCreate failed: %!STATUS!",
			status));
		return status;
	}

	status = WdfDeviceConfigureRequestDispatching(DevExt->Device,
		DevExt->ControlQueue,
		WdfRequestTypeDeviceControl);

	if (!NT_SUCCESS(status)) {
		KdPrint((
			"WdfDeviceConfigureRequestDispatching failed: %!STATUS!",
			status));
		return status;
	}


	status = PLxInterruptCreate(DevExt);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	KdPrint(("All queing has worked correctly"));

	return status;
}


VOID
PlxCleanupDeviceExtension(
	_In_ PDEVICE_EXTENSION DevExt
)
/*++

Routine Description:

Frees allocated memory that was saved in the
WDFDEVICE's context, before the device object
is deleted.

Arguments:

DevExt - Pointer to our DEVICE_EXTENSION

Return Value:

None

--*/
{
#ifdef SIMULATE_MEMORY_FRAGMENTATION
	if (DevExt->WriteMdlChain) {
		DestroyMdlChain(DevExt->WriteMdlChain);
		DevExt->WriteMdlChain = NULL;
	}

	if (DevExt->ReadMdlChain) {
		DestroyMdlChain(DevExt->ReadMdlChain);
		DevExt->ReadMdlChain = NULL;
	}
#else
	UNREFERENCED_PARAMETER(DevExt);
#endif
}


NTSTATUS
PLxPrepareHardware(
	IN PDEVICE_EXTENSION DevExt,
	IN WDFCMRESLIST     ResourcesTranslated
)
/*++
Routine Description:

Gets the HW resources assigned by the bus driver from the start-irp
and maps it to system address space.

Arguments:

DevExt      Pointer to our DEVICE_EXTENSION

Return Value:

None

--*/
{
	NTSTATUS				status = STATUS_SUCCESS;

	PAGED_CODE();

	status = PLxScanPciResources(DevExt, ResourcesTranslated);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("<-- DMADriverPrepareHardware call PLxScanPciResources failed  0x%x",
			status));
		return status;
	}

	// Setup the board DMA Resources
	/*
	status = PLxBoardDmaInit(DevExt);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("<-- DMADriverPrepareHardware call PLxBoardDmaInit failed  0x%x",
			status));
		return status;
	}
	*/
	return status;
}

/*! PLxScanPciResources
*
* \brief This routine scans the device for resources and maps the memory areas.
* \param pDevExt
* \param ResourcesTranslated
* \return status
*/
NTSTATUS
PLxScanPciResources(
	IN PDEVICE_EXTENSION 	pDevExt,
	IN WDFCMRESLIST			ResourcesTranslated
)
{
	NTSTATUS				status = STATUS_SUCCESS;
	PCM_PARTIAL_RESOURCE_DESCRIPTOR	descriptor;
	UINT8					bar;
	UINT32					i;
	UINT8					barSize;
	UINT64					barLength;
	BOOLEAN					AddressSizeDetected = FALSE;

	PAGED_CODE();

	// Setup prior to scanning hardware resources
	pDevExt->NumberOfBARS = 0;
	pDevExt->Use64BitAddresses = FALSE;

	// search through the resources
	for (i = 0; i<WdfCmResourceListGetCount(ResourcesTranslated); i++)
	{
		descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
		if (!descriptor)
		{
			return STATUS_DEVICE_CONFIGURATION_ERROR;
		}

		switch (descriptor->Type)
		{
		case CmResourceTypePort:
			bar = pDevExt->NumberOfBARS;
			if (bar < MAX_NUMBER_OF_BARS)
			{
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
				while (barLength > 0)
				{
					barSize++;
					barLength >>= 1;
				}
				pDevExt->BoardConfig.PciConfig.BarCfg[bar] |= ((UINT64)barSize << BAR_CFG_BAR_SIZE_OFFSET);

				KdPrint(("PCI IO port found, BAR #%d, Physical Addrs = [0x%I64X], Virtual Addrs = [0x%I64X] Length=0x%x, Size=%d",
					bar, pDevExt->BarPhysicalAddress[bar], pDevExt->BarVirtualAddress[bar], (UINT32)pDevExt->BarLength[bar], barSize));
			}
			break;

		case CmResourceTypeMemory:
			bar = pDevExt->NumberOfBARS;
			if (bar < MAX_NUMBER_OF_BARS)
			{
				// Save Memory Space info
				pDevExt->NumberOfBARS++;
				pDevExt->BarType[bar] = CmResourceTypeMemory;
				pDevExt->BarPhysicalAddress[bar] = descriptor->u.Memory.Start;
				pDevExt->BarLength[bar] = descriptor->u.Memory.Length;
				// set system bar size
				if (AddressSizeDetected == FALSE)
				{
					AddressSizeDetected = TRUE;
					pDevExt->Use64BitAddresses = FALSE;
				}
				// map the memory
				pDevExt->BarVirtualAddress[bar] = MmMapIoSpace(descriptor->u.Memory.Start,
					descriptor->u.Memory.Length,
					MmNonCached);
				// Save in BoardCfg space
				pDevExt->BoardConfig.PciConfig.BarCfg[bar] = BAR_CFG_BAR_PRESENT | BAR_CFG_BAR_TYPE_MEMORY;
				if ((descriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) == CM_RESOURCE_MEMORY_PREFETCHABLE)
				{
					pDevExt->BoardConfig.PciConfig.BarCfg[bar] |= BAR_CFG_MEMORY_PREFETCHABLE;
				}
				// compute and store bar size in (2**n)
				barSize = 0;
				barLength = pDevExt->BarLength[bar] - 1;
				while (barLength > 0)
				{
					barSize++;
					barLength >>= 1;
				}
				pDevExt->BoardConfig.PciConfig.BarCfg[bar] |= ((UINT64)barSize << BAR_CFG_BAR_SIZE_OFFSET);

				KdPrint(("Memory port found, BAR #%d, Physical Addrs = [0x%I64X], Virtual Addrs = [0x%I64X] Length=0x%x, Size=%d",
					bar, pDevExt->BarPhysicalAddress[bar], pDevExt->BarVirtualAddress[bar], (UINT32)pDevExt->BarLength[bar], barSize));
			}
			break;

		case CmResourceTypeMemoryLarge:
			bar = pDevExt->NumberOfBARS;
			if (bar < MAX_NUMBER_OF_BARS)
			{
				// Save Memory Space info
				pDevExt->NumberOfBARS++;
				pDevExt->BarType[bar] = CmResourceTypeMemoryLarge;
				if ((descriptor->Flags & CM_RESOURCE_MEMORY_LARGE_40) == CM_RESOURCE_MEMORY_LARGE_40)
				{
					pDevExt->BarPhysicalAddress[bar] = descriptor->u.Memory40.Start;
					pDevExt->BarLength[bar] = descriptor->u.Memory40.Length40;
				}
				else if ((descriptor->Flags & CM_RESOURCE_MEMORY_LARGE_48) == CM_RESOURCE_MEMORY_LARGE_48)
				{
					pDevExt->BarPhysicalAddress[bar] = descriptor->u.Memory48.Start;
					pDevExt->BarLength[bar] = descriptor->u.Memory48.Length48;
				}
				else if ((descriptor->Flags & CM_RESOURCE_MEMORY_LARGE_64) == CM_RESOURCE_MEMORY_LARGE_64)
				{
					pDevExt->BarPhysicalAddress[bar] = descriptor->u.Memory64.Start;
					pDevExt->BarLength[bar] = descriptor->u.Memory64.Length64;
				}
				else
				{
					// Error!
					pDevExt->NumberOfBARS--;
					pDevExt->BarVirtualAddress[bar] = NULL;
					KdPrint(("Large Memory space detected, error, Flags =  0x%x",
						descriptor->Flags));
					break;
				}
				// set system bar size
				if (AddressSizeDetected == FALSE)
				{
					AddressSizeDetected = TRUE;
					pDevExt->Use64BitAddresses = TRUE;
				}
				// map the memory
				pDevExt->BarVirtualAddress[bar] = MmMapIoSpace(descriptor->u.Memory.Start,
					descriptor->u.Memory.Length,
					MmNonCached);
				// Save in BoardCfg space
				pDevExt->BoardConfig.PciConfig.BarCfg[bar] = BAR_CFG_BAR_PRESENT | BAR_CFG_BAR_TYPE_MEMORY |
					BAR_CFG_MEMORY_64BIT_CAPABLE;
				if ((descriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) == CM_RESOURCE_MEMORY_PREFETCHABLE)
				{
					pDevExt->BoardConfig.PciConfig.BarCfg[bar] |= BAR_CFG_MEMORY_PREFETCHABLE;
				}
				// compute and store bar size in (2**n)
				barSize = 0;
				barLength = pDevExt->BarLength[bar] - 1;
				while (barLength > 0)
				{
					barSize++;
					barLength >>= 1;
				}
				pDevExt->BoardConfig.PciConfig.BarCfg[bar] |= ((UINT64)barSize << BAR_CFG_BAR_SIZE_OFFSET);

				KdPrint(("Large Memory port found, BAR #%d, Length=0x%x, Size=%d",
					bar, (UINT32)pDevExt->BarLength[bar], barSize));
			}
			break;

		case CmResourceTypeInterrupt:
			if ((descriptor->Flags & (CM_RESOURCE_INTERRUPT_MESSAGE | CM_RESOURCE_INTERRUPT_LATCHED)) ==
				(CM_RESOURCE_INTERRUPT_MESSAGE | CM_RESOURCE_INTERRUPT_LATCHED))
			{
				KdPrint(("Message Signaled Interrupts found"));
			}
		default:
			// Ignore the other resources
			break;
		}
	}
	return status;
}

NTSTATUS
PLxInitializeDMA(
	IN PDEVICE_EXTENSION DevExt
)
/*++
Routine Description:

Initializes the DMA adapter.

Arguments:

DevExt      Pointer to our DEVICE_EXTENSION

Return Value:

None

--*/
{
	NTSTATUS    status;
	WDF_OBJECT_ATTRIBUTES attributes;

	PAGED_CODE();

	//
	// PLx PCI9030 DMA_TRANSFER_ELEMENTS must be 16-byte aligned
	//
	WdfDeviceSetAlignmentRequirement(DevExt->Device,
		PCI9030_DTE_ALIGNMENT_16);

	//
	// Create a new DMA Enabler instance.
	// Use Scatter/Gather, 64-bit Addresses, Duplex-type profile.
	//
	{
		WDF_DMA_ENABLER_CONFIG   dmaConfig;

		WDF_DMA_ENABLER_CONFIG_INIT(&dmaConfig,
			WdfDmaProfileScatterGather64Duplex,
			DevExt->MaximumTransferLength);

		KdPrint((
			" - The DMA Profile is WdfDmaProfileScatterGather64Duplex"));

		//
		// Opt-in to DMA version 3, which is required by
		// WdfDmaTransactionSetSingleTransferRequirement
		//
		//??      dmaConfig.WdmDmaVersionOverride = 3;

		status = WdfDmaEnablerCreate(DevExt->Device,
			&dmaConfig,
			WDF_NO_OBJECT_ATTRIBUTES,
			&DevExt->DmaEnabler);

		if (!NT_SUCCESS(status)) {

			KdPrint((
				"WdfDmaEnablerCreate failed: %!STATUS!", status));
			return status;
		}

		//
		// The PLX device does not have a hard limit on the number of DTEs
		// it can process for each DMA operation. However, the DTEs are written
		// to common buffers whose size is the effective upper bound to the
		// length of the Scatter/Gather lists we can work with.
		//
		WdfDmaEnablerSetMaximumScatterGatherElements(
			DevExt->DmaEnabler,
			min(DevExt->WriteTransferElements, DevExt->ReadTransferElements));
	}

	//
	// Allocate common buffer for building writes
	//
	// NOTE: This common buffer will not be cached.
	//       Perhaps in some future revision, cached option could
	//       be used. This would have faster access, but requires
	//       flushing before starting the DMA in PLxStartWriteDma.
	//
	DevExt->WriteCommonBufferSize =
		sizeof(DMA_TRANSFER_ELEMENT) * DevExt->WriteTransferElements;

	_Analysis_assume_(DevExt->WriteCommonBufferSize > 0);
	status = WdfCommonBufferCreate(DevExt->DmaEnabler,
		DevExt->WriteCommonBufferSize,
		WDF_NO_OBJECT_ATTRIBUTES,
		&DevExt->WriteCommonBuffer);

	if (!NT_SUCCESS(status)) {
		KdPrint((
			"WdfCommonBufferCreate (write) failed: %!STATUS!", status));
		return status;
	}


	DevExt->WriteCommonBufferBase =
		WdfCommonBufferGetAlignedVirtualAddress(DevExt->WriteCommonBuffer);

	DevExt->WriteCommonBufferBaseLA =
		WdfCommonBufferGetAlignedLogicalAddress(DevExt->WriteCommonBuffer);

	RtlZeroMemory(DevExt->WriteCommonBufferBase,
		DevExt->WriteCommonBufferSize);

	KdPrint((
		"WriteCommonBuffer 0x%p  (#0x%I64X), length %I64d",
		DevExt->WriteCommonBufferBase,
		DevExt->WriteCommonBufferBaseLA.QuadPart,
		WdfCommonBufferGetLength(DevExt->WriteCommonBuffer)));

	//
	// Allocate common buffer for building reads
	//
	// NOTE: This common buffer will not be cached.
	//       Perhaps in some future revision, cached option could
	//       be used. This would have faster access, but requires
	//       flushing before starting the DMA in PLxStartReadDma.
	//
	DevExt->ReadCommonBufferSize =
		sizeof(DMA_TRANSFER_ELEMENT) * DevExt->ReadTransferElements;

	_Analysis_assume_(DevExt->ReadCommonBufferSize > 0);
	status = WdfCommonBufferCreate(DevExt->DmaEnabler,
		DevExt->ReadCommonBufferSize,
		WDF_NO_OBJECT_ATTRIBUTES,
		&DevExt->ReadCommonBuffer);

	if (!NT_SUCCESS(status)) {
		KdPrint((
			"WdfCommonBufferCreate (read) failed %!STATUS!", status));
		return status;
	}

	DevExt->ReadCommonBufferBase =
		WdfCommonBufferGetAlignedVirtualAddress(DevExt->ReadCommonBuffer);

	DevExt->ReadCommonBufferBaseLA =
		WdfCommonBufferGetAlignedLogicalAddress(DevExt->ReadCommonBuffer);

	RtlZeroMemory(DevExt->ReadCommonBufferBase,
		DevExt->ReadCommonBufferSize);

	KdPrint((
		"ReadCommonBuffer  0x%p  (#0x%I64X), length %I64d",
		DevExt->ReadCommonBufferBase,
		DevExt->ReadCommonBufferBaseLA.QuadPart,
		WdfCommonBufferGetLength(DevExt->ReadCommonBuffer)));

	//
	// Since we are using sequential queue and processing one request
	// at a time, we will create transaction objects upfront and reuse
	// them to do DMA transfer. Transactions objects are parented to
	// DMA enabler object by default. They will be deleted along with
	// along with the DMA enabler object. So need to delete them
	// explicitly.
	//
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, TRANSACTION_CONTEXT);

	status = WdfDmaTransactionCreate(DevExt->DmaEnabler,
		&attributes,
		&DevExt->ReadDmaTransaction);

	if (!NT_SUCCESS(status)) {
		KdPrint((
			"WdfDmaTransactionCreate(read) failed: %!STATUS!", status));
		return status;
	}

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, TRANSACTION_CONTEXT);
	//
	// Create a new DmaTransaction.
	//
	status = WdfDmaTransactionCreate(DevExt->DmaEnabler,
		&attributes,
		&DevExt->WriteDmaTransaction);

	if (!NT_SUCCESS(status)) {
		KdPrint((
			"WdfDmaTransactionCreate(write) failed: %!STATUS!", status));
		return status;
	}

#ifdef SIMULATE_MEMORY_FRAGMENTATION
	//
	// Allocate MDL chains that will be used to simulate memory fragmentation.
	//
	status = BuildMdlChain(&DevExt->WriteMdlChain);
	if (!NT_SUCCESS(status)) {
		KdPrint((
			"BuildMdlChain(Write) failed: %!STATUS!",
			status);
		return status;
	}

	status = BuildMdlChain(&DevExt->ReadMdlChain);
	if (!NT_SUCCESS(status)) {
		KdPrint((
			"BuildMdlChain(Read) failed: %!STATUS!",
			status);
		return status;
	}
#endif

	return status;
}


NTSTATUS
PLxInitWrite(
	IN PDEVICE_EXTENSION DevExt
)
/*++
Routine Description:

Initialize write data structures

Arguments:

DevExt     Pointer to Device Extension

Return Value:

None

--*/
{
	UNREFERENCED_PARAMETER(DevExt);

	KdPrint(( "--> PLxInitWrite"));
	//?????????


	KdPrint(( "<-- PLxInitWrite"));

	return STATUS_SUCCESS;
}


NTSTATUS
PLxInitRead(
	IN PDEVICE_EXTENSION DevExt
)
/*++
Routine Description:

Initialize read data structures

Arguments:

DevExt     Pointer to Device Extension

Return Value:

--*/
{
	UNREFERENCED_PARAMETER(DevExt);
	KdPrint(( "--> PLxInitRead"));

	//?????

	KdPrint(( "<-- PLxInitRead"));

	return STATUS_SUCCESS;
}

VOID
PLxShutdown(
	IN PDEVICE_EXTENSION DevExt
)
/*++

Routine Description:

Reset the device to put the device in a known initial state.
This is called from D0Exit when the device is torn down or
when the system is shutdown. Note that Wdf has already
called out EvtDisable callback to disable the interrupt.

Arguments:

DevExt -  Pointer to our adapter

Return Value:

None

--*/
{
	KdPrint(( "---> PLxShutdown"));

	//
	// WdfInterrupt is already disabled so issue a full reset
	//
	if (DevExt->Regs) {

		PLxHardwareReset(DevExt);
	}

	KdPrint(( "<--- PLxShutdown"));
}

VOID
PLxHardwareReset(
	IN PDEVICE_EXTENSION DevExt
)
/*++
Routine Description:

Called by D0Exit when the device is being disabled or when the system is shutdown to
put the device in a known initial state.

Arguments:

DevExt     Pointer to Device Extension

Return Value:

--*/
{
	UNREFERENCED_PARAMETER(DevExt);

	LARGE_INTEGER delay;
	int nEEPROMCtrlReg = PCI9030_EEPROM_CTRL;

	union {
		EEPROM_CSR  bits;
		ULONG       ulong;
	} eeCSR;

	KdPrint(( "--> PLxIssueFullReset"));

	// Drive the 9030 into soft reset.
	eeCSR.ulong = READ_REGISTER_ULONG((PULONG)&nEEPROMCtrlReg);

	eeCSR.bits.SoftwareReset = TRUE;
	WRITE_REGISTER_ULONG((PULONG)&nEEPROMCtrlReg,	eeCSR.ulong);

	delay.QuadPart = WDF_REL_TIMEOUT_IN_MS(100);
	KeDelayExecutionThread(KernelMode, TRUE, &delay);

	eeCSR.bits.SoftwareReset = FALSE;
	WRITE_REGISTER_ULONG((PULONG)&nEEPROMCtrlReg,	eeCSR.ulong);

	KdPrint(( "<-- PLxIssueFullReset"));
}



