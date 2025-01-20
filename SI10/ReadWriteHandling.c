#include "precomp.h"

#if TRACE_ENABLED
#include "ReadWriteHandling.tmh"
#endif // TRACE_ENABLED

#pragma warning(disable:4127)  // Constants in while loops. Sorry, I like them.

VOID
PLxEvtIoRead(
	IN WDFQUEUE  	Queue,
	IN WDFREQUEST	Request,
	IN size_t		Length
)
{
	NTSTATUS		status = STATUS_NOT_SUPPORTED;
	size_t			transferSize = 0;

	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(Length);

	KdPrint(("--> DMADriverEvtIoRead, Request %p", Request));

	//pDevExt = DMADriverGetDeviceContext(WdfIoQueueGetDevice(Queue));

	// For now, fail the request until we implement the read
	if (!NT_SUCCESS(status))
	{
		KdPrint(("<-- DMADriverEvtIoRead failed 0x%x", status));
	}
	else
	{
		KdPrint(("<-- DMADriverEvtIoRead"));
	}

	// Finish request
	WdfRequestCompleteWithInformation(Request, status, transferSize);
}

/*! DMADriverEvtIoWrite
*
* \brief
* \param Queue
* \param Request
* \param Length
* \return none
*/
VOID
PLxEvtIoWrite(
	IN WDFQUEUE  	Queue,
	IN WDFREQUEST	Request,
	IN size_t		Length
)
{
	NTSTATUS		status = STATUS_NOT_SUPPORTED;
	size_t			transferSize = 0;

	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(Length);

	KdPrint(("--> DMADriverEvtIoWrite, Request %p", Request));

	//pDevExt = DMADriverGetDeviceContext(WdfIoQueueGetDevice(Queue));

	// For now, fail the request until we implement the write
	if (!NT_SUCCESS(status))
	{
		KdPrint(("<-- DMADriverEvtIoWrite failed 0x%x", status));
	}
	else
	{
		KdPrint(("<-- DMADriverEvtIoWrite"));
	}

	// Finish request
	WdfRequestCompleteWithInformation(Request, status, transferSize);
}

/*! ReadWriteMemAccess
*
*
* \brief
* \param pDevExt
* \param Request
* \param Rd_wr_n
* \return
*/
VOID
ReadWriteMemAccess(
	IN PDEVICE_EXTENSION 	pDevExt,
	IN WDFREQUEST			Request,
	WDF_DMA_DIRECTION		Rd_Wr_n
)
{
	NTSTATUS 		status = STATUS_SUCCESS;
	// Default is to complete request, forwarded requests should not be completed.
	BOOLEAN 		completeRequest = TRUE;
	size_t			bufferSize;
	size_t 			transferSize = 0;
	SI10_PDO_MEM_STRUCT	pDoMemStruct;
	UINT32			BarNum;
	UINT64			Offset;
	UINT64			CardOffset;
	UINT32			Length;
	PUINT8			pBufferSafe;
	PUINT8 			cardAddress;
	PUINT8 			bufferAddress;
	INT32			sizeOfTransfer = 4;

	KdPrint(("--> ReadWriteDeviceControl, Request %p, IRQL=%d",
		Request, KeGetCurrentIrql()));

	// Get the input buffer pointer
	status = WdfRequestRetrieveInputBuffer(Request,
		sizeof(SI10_DO_MEM_STRUCT),	// Min size
		(PVOID *)&pDoMemStruct,
		&bufferSize);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("WdfRequestRetrieveInputBuffer failed 0x%x", status));
	}
	else if (pDoMemStruct == NULL)
	{
		KdPrint(("Input buffer is NULL"));
		status = STATUS_INVALID_PARAMETER;
	}
	else if (bufferSize != sizeof(SI10_DO_MEM_STRUCT))
	{
		KdPrint(("WdfRequestRetrieveInputBuffer returned incorrect size, size=%Id, expected=%d",
			bufferSize, sizeof(SI10_DO_MEM_STRUCT)));
		status = STATUS_INVALID_PARAMETER;
	}
	else
	{
		// We are good to go,  save off the input parameters
		// if the IOCTL is buffered, the same memory address is being used
		// for both the input and output buffers
		BarNum = pDoMemStruct->BarNum;
		Offset = pDoMemStruct->Offset;
		CardOffset = pDoMemStruct->CardOffset;
		Length = (UINT32)pDoMemStruct->Length;

		// validate the DO_MEM_STRUCT members
		if ((BarNum >= pDevExt->NumberOfBARS) ||
			(Length == 0))
		{
			KdPrint(("Invalid parameter in DoMemStruct"));
			status = STATUS_INVALID_PARAMETER;
			goto DOMEMIOCTLDONE;
		}

		// get the output buffer pointer
		status = WdfRequestRetrieveOutputBuffer(Request,
			(size_t)(Offset + Length),	// Min size
			(PVOID *)&pBufferSafe,
			&bufferSize);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x", status));
			goto DOMEMIOCTLDONE;
		}
		else if (pBufferSafe == NULL)
		{
			KdPrint(("Output buffer is NULL"));
			status = STATUS_INVALID_PARAMETER;
			goto DOMEMIOCTLDONE;
		}
		else if (bufferSize != (size_t)(Offset + Length))
		{
			KdPrint(("WdfRequestRetrieveOutputBuffer returned incorrect size, size=%Id, expected=%ld",
				bufferSize, (UINT32)(Offset + Length)));
			status = STATUS_INVALID_PARAMETER;
			goto DOMEMIOCTLDONE;
		}
		else if ((CardOffset + Length) > pDevExt->BarLength[BarNum])
		{
			KdPrint(("CardOffset + Length is out of range"));
			status = STATUS_INVALID_PARAMETER;
			goto DOMEMIOCTLDONE;
		}

		// setup starting addresses
		cardAddress = (PUINT8)pDevExt->BarVirtualAddress[BarNum] +
			CardOffset;
		bufferAddress = (PUINT8)pBufferSafe + Offset;

		// check alignment to determine transfer size
		while ((sizeOfTransfer > 1) &&
			(((Length % sizeOfTransfer) != 0) ||
			(((UINT64)cardAddress % sizeOfTransfer) != 0) ||
				(((UINT64)bufferAddress % sizeOfTransfer) != 0)))
		{
			sizeOfTransfer >>= 1;
		}

		if (Rd_Wr_n == WdfDmaDirectionReadFromDevice)
		{
			KdPrint(("    MemoryRead: BAR %d, Card Offset = 0x%x, Card Addrs = [0x%IX], Length %d\n",
				BarNum, CardOffset, cardAddress, Length));

			switch (sizeOfTransfer)
			{
			case 4:
				if (pDevExt->BarType[BarNum] == CmResourceTypePort)
				{
					READ_PORT_BUFFER_ULONG((PULONG)cardAddress,
						(PULONG)bufferAddress,
						Length / sizeof(ULONG));
					transferSize = Length;
				}
				else if ((pDevExt->BarType[BarNum] == CmResourceTypeMemory) ||
					(pDevExt->BarType[BarNum] == CmResourceTypeMemoryLarge))
				{
					READ_REGISTER_BUFFER_ULONG((PULONG)cardAddress,
						(PULONG)bufferAddress,
						Length / sizeof(ULONG));
					transferSize = Length;
				}
//				KdPrint(("    Read: Offset = 0x%x, Length %d Value = 0x%x\n", CardOffset, Length, *pulBufferAddress));
				break;
			case 2:
				if (pDevExt->BarType[BarNum] == CmResourceTypePort)
				{
					READ_PORT_BUFFER_USHORT((PUINT16)cardAddress,
						(PUINT16)bufferAddress,
						Length / sizeof(UINT16));
					transferSize = Length;
				}
				else if ((pDevExt->BarType[BarNum] == CmResourceTypeMemory) ||
					(pDevExt->BarType[BarNum] == CmResourceTypeMemoryLarge))
				{
					READ_REGISTER_BUFFER_USHORT((PUINT16)cardAddress,
						(PUINT16)bufferAddress,
						Length / sizeof(UINT16));
					transferSize = Length;
				}
				break;
			case 1:
				if (pDevExt->BarType[BarNum] == CmResourceTypePort)
				{
					READ_PORT_BUFFER_UCHAR(cardAddress,
						bufferAddress,
						Length / sizeof(UINT8));
					transferSize = Length;
				}
				else if ((pDevExt->BarType[BarNum] == CmResourceTypeMemory) ||
					(pDevExt->BarType[BarNum] == CmResourceTypeMemoryLarge))
				{
					READ_REGISTER_BUFFER_UCHAR(cardAddress,
						bufferAddress,
						Length / sizeof(UINT8));
					transferSize = Length;
				}
				break;
			}
		}
		else if (Rd_Wr_n == WdfDmaDirectionWriteToDevice)
		{

			switch (sizeOfTransfer)
			{
			case 4:
//				KdPrint(("    Write: Value = 0x%x Offset = 0x%x, Length %d\n", *pulBuffer, CardOffset, Length));
				if (pDevExt->BarType[BarNum] == CmResourceTypePort)
				{
					WRITE_PORT_BUFFER_ULONG((PULONG)cardAddress,
						(PULONG)bufferAddress,
						Length / sizeof(ULONG));
					transferSize = Length;
				}
				else if ((pDevExt->BarType[BarNum] == CmResourceTypeMemory) ||
					(pDevExt->BarType[BarNum] == CmResourceTypeMemoryLarge))
				{
					WRITE_REGISTER_BUFFER_ULONG((PULONG)cardAddress,
						(PULONG)bufferAddress,
						Length / sizeof(ULONG));
					transferSize = Length;
				}
				break;
			case 2:
//				KdPrint(("    Write: Value = 0x%x Offset = 0x%x, Length %d\n", *pun16Buffer, CardOffset, sizeOfTransfer));
				if (pDevExt->BarType[BarNum] == CmResourceTypePort)
				{
					WRITE_PORT_BUFFER_USHORT((PUINT16)cardAddress,
						(PUINT16)bufferAddress,
						Length / sizeof(UINT16));
					transferSize = Length;
				}
				else if ((pDevExt->BarType[BarNum] == CmResourceTypeMemory) ||
					(pDevExt->BarType[BarNum] == CmResourceTypeMemoryLarge))
				{
					WRITE_REGISTER_BUFFER_USHORT((PUINT16)cardAddress,
						(PUINT16)bufferAddress,
						Length / sizeof(UINT16));
					transferSize = Length;
				}
				break;
			case 1:
				KdPrint(("    Write: Value = 0x%x Offset = 0x%x, Length %d\n", *bufferAddress, CardOffset, sizeOfTransfer));
				if (pDevExt->BarType[BarNum] == CmResourceTypePort)
				{
					WRITE_PORT_BUFFER_UCHAR(cardAddress,
						bufferAddress,
						Length / sizeof(UINT8));
					transferSize = Length;
				}
				else if ((pDevExt->BarType[BarNum] == CmResourceTypeMemory) ||
					(pDevExt->BarType[BarNum] == CmResourceTypeMemoryLarge))
				{
					WRITE_REGISTER_BUFFER_UCHAR(cardAddress,
						bufferAddress,
						Length / sizeof(UINT8));
					transferSize = Length;
				}
				break;
			}
		}
	}

DOMEMIOCTLDONE:
	// If we are done, complete the request
	if (completeRequest)
	{
		WdfRequestCompleteWithInformation(Request, status, transferSize);
	}
}

/*! ReadWritePCIConfig
*
* \brief Provides Read and Write access to the boards PCI Configuration space
* \param pDevExt
* \param Request
* \param Rd_Wr_n
* \return
*/
VOID
ReadWritePCIConfig(
	IN PDEVICE_EXTENSION 	pDevExt,
	IN WDFREQUEST			Request,
	WDF_DMA_DIRECTION		Rd_Wr_n
)
{
	NTSTATUS 				status = STATUS_SUCCESS;
	size_t					BufferSize;
	size_t 					transferSize = 0;
	PRW_PCI_CONFIG_STRUCT	pPCIConfigXfer;
	PUINT8					pIOBuffer;

	KdPrint(("--> ReadWritePCIConfig, Request %p, IRQL=%d",
		Request, KeGetCurrentIrql()));

	// Get the input buffer pointer
	status = WdfRequestRetrieveInputBuffer(Request,
		sizeof(RW_PCI_CONFIG_STRUCT),	// Min size
		(PVOID*)&pPCIConfigXfer,
		&BufferSize);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("WdfRequestRetrieveInputBuffer failed 0x%x", status));
	}
	else if (pPCIConfigXfer == NULL)
	{
		KdPrint(("Input buffer is NULL"));
		status = STATUS_INVALID_PARAMETER;
	}
	else if (BufferSize != sizeof(RW_PCI_CONFIG_STRUCT))
	{
		KdPrint(("WdfRequestRetrieveInputBuffer returned incorrect size, size=%Id, expected=%d",
			BufferSize, sizeof(RW_PCI_CONFIG_STRUCT)));
		status = STATUS_INVALID_PARAMETER;
	}
	else
	{
		do
		{
			// get the output buffer pointer to copy data from/to
			status = WdfRequestRetrieveOutputBuffer(Request, (size_t)(pPCIConfigXfer->Length), (PVOID *)&pIOBuffer, &BufferSize);
			if (!NT_SUCCESS(status))
			{
				KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x", status));
				break;
			}
			else if (pIOBuffer == NULL)
			{
				KdPrint(("Input/Output buffer is NULL"));
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			else if (BufferSize != (size_t)(pPCIConfigXfer->Length))
			{
				KdPrint(("WdfRequestRetrieveOutputBuffer returned incorrect size, size=%Id, expected=%ld",
					BufferSize, (UINT32)(pPCIConfigXfer->Length)));
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			// Now that the buffer has been validated do the actual copy to/from PCI config space
			if (Rd_Wr_n == WdfDmaDirectionReadFromDevice)
			{
				// Get the common config data
				transferSize = pDevExt->BusInterface.GetBusData(pDevExt->BusInterface.Context,
					PCI_WHICHSPACE_CONFIG, (PVOID)pIOBuffer,
					pPCIConfigXfer->Offset, pPCIConfigXfer->Length);
			}
			else if (Rd_Wr_n == WdfDmaDirectionWriteToDevice)
			{
				// Get the common config data
				transferSize = pDevExt->BusInterface.SetBusData(pDevExt->BusInterface.Context,
					PCI_WHICHSPACE_CONFIG, (PVOID)pIOBuffer,
					pPCIConfigXfer->Offset, pPCIConfigXfer->Length);
			}
		} while (FALSE);
	}
	WdfRequestCompleteWithInformation(Request, status, transferSize);
}

VOID
PLxReadRequestComplete(
	IN WDFDMATRANSACTION  DmaTransaction,
	IN NTSTATUS           Status
)
/*++

Routine Description:

Arguments:

Return Value:

--*/
{
	WDFREQUEST         request;
	size_t             bytesTransferred;

#ifndef SIMULATE_MEMORY_FRAGMENTATION
	//
	// Get the associated request from the transaction.
	//
	request = WdfDmaTransactionGetRequest(DmaTransaction);
#else
	PVOID buffer;
	size_t length;
	WDFDEVICE device;
	PDEVICE_EXTENSION devExt;

	device = WdfDmaTransactionGetDevice(DmaTransaction);
	devExt = PLxGetDeviceContext(device);

	//
	// This means that the DMA transaction was not initialized
	// with WdfDmaTransactionInitializeUsingRequest, so the
	// request was not implicitly associated with it. Instead,
	// we saved it in the object's context space.
	//
	request = PLxGetTransactionContext(DmaTransaction)->Request;

	//
	// Copy the MDL chain's contents back to the original request buffer.
	//
	Status = WdfRequestRetrieveOutputBuffer(request, 0, &buffer, &length);
	if (!NT_SUCCESS(Status)) {
		KdPrint((
			"WdfRequestRetrieveOutputBuffer failed: %!STATUS!",
			Status);
	}
	else {
		CopyMdlChainToBuffer(devExt->ReadMdlChain, buffer, length);
	}
#endif // SIMULATE_MEMORY_FRAGMENTATION

	ASSERT(request);

	//
	// Get the final bytes transferred count.
	//
	bytesTransferred = WdfDmaTransactionGetBytesTransferred(DmaTransaction);

	KdPrint((
		"PLxReadRequestComplete:  Request %p, Status %!STATUS!, "
		"bytes transferred %d\n",
		request, Status, (int)bytesTransferred));

	WdfDmaTransactionRelease(DmaTransaction);

	//
	// Complete this Request.
	//
	WdfRequestCompleteWithInformation(request, Status, bytesTransferred);

}

VOID
PLxWriteRequestComplete(
	IN WDFDMATRANSACTION  DmaTransaction,
	IN NTSTATUS           Status
)
/*++

Routine Description:

Arguments:

Return Value:

--*/
{
	WDFREQUEST         request;
	size_t             bytesTransferred;

	//
	// Initialize locals
	//

#ifdef ASSOC_WRITE_REQUEST_WITH_DMA_TRANSACTION

	request = WdfDmaTransactionGetRequest(DmaTransaction);

#else
	//
	// If CreateDirect was used then there will be no assoc. Request.
	//
	{
		PTRANSACTION_CONTEXT transContext = PLxGetTransactionContext(DmaTransaction);

		request = transContext->Request;
		transContext->Request = NULL;

	}
#endif

	//
	// Get the final bytes transferred count.
	//
	bytesTransferred = WdfDmaTransactionGetBytesTransferred(DmaTransaction);

	KdPrint(("PLxWriteRequestComplete:  Request %p, Status %!STATUS!, "
		"bytes transferred %d\n",
		request, Status, (int)bytesTransferred));

	WdfDmaTransactionRelease(DmaTransaction);

	WdfRequestCompleteWithInformation(request, Status, bytesTransferred);

}

/*! LoadFPGABinary
*
*
* \brief
* \param pDevExt
* \param Request
* \return
*/


VOID
LoadFPGABinary(
	IN PDEVICE_EXTENSION 	pDevExt,
	IN WDFREQUEST			Request
)
{
	NTSTATUS 			status = STATUS_SUCCESS;
	size_t				transferSize = 0;
	BOOLEAN 			completeRequest = TRUE;
	PUINT8				pLoadFPGAArray;
	size_t				nFPGABinaryLength = 0;
	PUINT8				pFPGABinary;
	PULONG				cardAddress;

	// Get the input buffer pointer
	status = WdfRequestRetrieveInputBuffer(Request,
		0,	// Min size
		(PVOID *)&pLoadFPGAArray,
		&nFPGABinaryLength);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("WdfRequestRetrieveInputBuffer failed 0x%x", status));
	}
	else if (pLoadFPGAArray == NULL)
	{
		KdPrint(("Load FPGA array is NULL"));
		status = STATUS_INVALID_PARAMETER;
	}
	else

	{
		// We are good to go,  save off the input parameters
		// if the IOCTL is buffered, the same memory address is being used
		// for both the input and output buffers

		KdPrint(("FPGA Binary length = %d, Binary addrs = [0x%I64X]", nFPGABinaryLength, pLoadFPGAArray));

		pFPGABinary = pLoadFPGAArray;

		// validate the LOAD_FPGA_STRUCT members
		if ((nFPGABinaryLength <= 0) || (pFPGABinary == NULL))
		{
			KdPrint(("Invalid parameter in LoadFPGAStruct"));
			status = STATUS_INVALID_PARAMETER;
			goto LOADFPGAIOCTLDONE;
		}

		char Data;
		ULONG Value;
		ULONG ValueWithClk;
		int bit;
		UINT64			CardOffset = 0x54 / (sizeof(ULONG));

		// Set Program- & Data & Clk to Low
		cardAddress = (PULONG)pDevExt->BarVirtualAddress[0] + CardOffset;		//BAR = 0
		KdPrint(("Toggle data and clock, Card Addrs = [%Ix]", cardAddress));

		Value = 0x02480482;
		WRITE_REGISTER_BUFFER_ULONG(cardAddress, &Value, 1);
		// Set Program- to high Data & Clk to Low
		Value |= 0x00000004;
		WRITE_REGISTER_BUFFER_ULONG(cardAddress, &Value, 1);

		ULONG nRegister = 0;
		int ii = 0;
		while ((nRegister & 0x00000020) == 0) {
			READ_REGISTER_BUFFER_ULONG(cardAddress, &nRegister, 1);
			ii++;
			if (ii == 20000) {
				break;
			}
		};
		KdPrint(("Looped %d times, Register = 0x%x ", ii, nRegister));
		size_t nn;
		for (nn = 0; nn < nFPGABinaryLength; nn++) {
			Data = pFPGABinary[nn];
			for (bit = 0; bit < 8; bit++) {
				Value = (0x00000000 | (((Data << bit) & 0x80) << 1)) | 0x02480482 | 0x00000004;
				ValueWithClk = Value | 0x00000800;
				WRITE_REGISTER_BUFFER_ULONG(cardAddress, &Value, 1);
				WRITE_REGISTER_BUFFER_ULONG(cardAddress, &ValueWithClk, 1); //Clk bit
				WRITE_REGISTER_BUFFER_ULONG(cardAddress, &Value, 1);

			}
		}

		// Must keep PROG bit set high at all times substequent to completing FPGA configuration to prevent
		// FPGA from clearing itself.
		// Clear bit 7 and Set bit 2
		Value = 0x02480406;
		WRITE_REGISTER_BUFFER_ULONG(cardAddress, &Value, 1);
		KdPrint(("Bytes Written %d, nDataSize %d", nn, nFPGABinaryLength));

	};
LOADFPGAIOCTLDONE:

	// If we are done, complete the request
	if (completeRequest)
	{
		WdfRequestCompleteWithInformation(Request, status, transferSize);
	}
}



/*! MailboxStringWrite
*
*
* \brief
* \param pDevExt
* \param Request
* \return
*/


VOID
MailboxStringWrite(
	IN PDEVICE_EXTENSION 	pDevExt,
	IN WDFREQUEST			Request
)
{
	NTSTATUS 		status = STATUS_SUCCESS;
	// Default is to complete request, forwarded requests should not be completed.
	BOOLEAN 		completeRequest = TRUE;
	size_t			bufferSize;
	size_t 			transferSize = 0;
	PMAILBOX_STRING_STRUCT	pMailboxStringStruct;
	UINT32			nArrayLength;
	PUINT8			pBufferSafe;
	PUINT8 			MailboxStatusAddrs;
	PUINT8 			MailboxPortAddrs;
	PUINT8 			bufferAddress;
	UINT32			nMask;

	KdPrint(("--> ReadWriteDeviceControl, Request %p, IRQL=%d",
		Request, KeGetCurrentIrql()));

	// Get the input buffer pointer
	status = WdfRequestRetrieveInputBuffer(Request,
		sizeof(MAILBOX_STRING_STRUCT),	// Min size
		(PVOID *)&pMailboxStringStruct,
		&bufferSize);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("WdfRequestRetrieveInputBuffer failed 0x%x", status));
	}
	else if (pMailboxStringStruct == NULL)
	{
		KdPrint(("Input buffer is NULL"));
		status = STATUS_INVALID_PARAMETER;
	}
	else if (bufferSize != sizeof(MAILBOX_STRING_STRUCT))
	{
		KdPrint(("WdfRequestRetrieveInputBuffer returned incorrect size, size=%Id, expected=%d",
			bufferSize, sizeof(MAILBOX_STRING_STRUCT)));
		status = STATUS_INVALID_PARAMETER;
	}
	else
	{
		// We are good to go,  save off the input parameters
		// if the IOCTL is buffered, the same memory address is being used
		// for both the input and output buffers
		nArrayLength = pMailboxStringStruct->nLength;
		nMask = pMailboxStringStruct->nMask;

		// get the output buffer pointer
		status = WdfRequestRetrieveOutputBuffer(Request,
			(size_t)(nArrayLength),	// Min size
			(PVOID *)&pBufferSafe,
			&bufferSize);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x", status));
			goto DOMAILBOXSTRINGDONE;
		}
		else if (pBufferSafe == NULL)
		{
			KdPrint(("Output buffer is NULL"));
			status = STATUS_INVALID_PARAMETER;
			goto DOMAILBOXSTRINGDONE;
		}
		else if (bufferSize != (size_t)(nArrayLength))
		{
			KdPrint(("WdfRequestRetrieveOutputBuffer returned incorrect size, size=%Id, expected=%ld",
				bufferSize, (UINT32)(nArrayLength)));
			status = STATUS_INVALID_PARAMETER;
			goto DOMAILBOXSTRINGDONE;
		}

		// setup starting addresses
		MailboxStatusAddrs = (PUINT8)pDevExt->BarVirtualAddress[2] + SI10_MAILBOX_STATUS;
		MailboxPortAddrs = (PUINT8)pDevExt->BarVirtualAddress[2] + SI10_MAILBOX_PORT;
		bufferAddress = (PUINT8)pBufferSafe;

		int nMailboxCheckSum = 0;
		unsigned int nTotalLength = nArrayLength;
		if (nMask & MAILBOX_CS) nTotalLength++;
		if (nMask & MAILBOX_EOS) nTotalLength++;

		for (unsigned int nIndex = 0; nIndex < nTotalLength; nIndex++) {
			int nTimeOut = 0;
			UINT8 nMailboxStatus = 0xff;

			while (nMailboxStatus & MAILBOX_PC_TO_CARD_FULL) {												//Wait until there is room in the mailbox
				if ((nTimeOut++) >= 1000) {
					status = MAILBOX_TIMED_OUT;
					goto DOMAILBOXSTRINGDONE;
				};
				READ_REGISTER_BUFFER_UCHAR(MailboxStatusAddrs, &nMailboxStatus, sizeof(UINT8));
			};

			if (nIndex < nArrayLength) {
				KdPrint(("Timeout = %d, Value = 0x%02x", nTimeOut, bufferAddress[nIndex]));					//Write a character
				WRITE_REGISTER_BUFFER_UCHAR(MailboxPortAddrs, &bufferAddress[nIndex], sizeof(UINT8));
				if (bufferAddress[nIndex] & 0x80) {
					nMailboxCheckSum = bufferAddress[nIndex];
				}
				else {
					nMailboxCheckSum += bufferAddress[nIndex];
				}
			}
			else {
				if (nIndex == nArrayLength) {																//Write the check sum
					UINT8 nValue = (UINT8)(nMailboxCheckSum & 0x7f);
					KdPrint(("Timeout = %d, Value = 0x%02x", nTimeOut, nValue));
					WRITE_REGISTER_BUFFER_UCHAR(MailboxPortAddrs, &nValue, sizeof(UINT8));
				}
				else {
					UINT8 nValue = 0xff;																	//Write the EOS 0xff
					KdPrint(("Timeout = %d, Value = 0x%02x", nTimeOut, nValue));
					WRITE_REGISTER_BUFFER_UCHAR(MailboxPortAddrs, &nValue, sizeof(UINT8));
				}
			}
		}
	}

DOMAILBOXSTRINGDONE:
	// If we are done, complete the request
	if (completeRequest)
	{
		WdfRequestCompleteWithInformation(Request, status, transferSize);
	}
}

/*! GetVersionNumber
*
*
* \brief
* \param pDevExt
* \param Request
* \param Rd_wr_n
* \return
*/
VOID
GetVersionNumber(
	IN PDEVICE_EXTENSION 	pDevExt,
	IN WDFREQUEST			Request
)
{
	NTSTATUS 		status = STATUS_SUCCESS;
	// Default is to complete request, forwarded requests should not be completed.
	BOOLEAN 		completeRequest = TRUE;
	size_t			MinSize = 0;
	size_t 			transferSize = 0;
	PSI10_DEVICE_DRIVER_VERSION	pVersion;
	size_t				bytesReturned;

	UNREFERENCED_PARAMETER(pDevExt);

	KdPrint(("--> GetVersionNumber, Request %p, IRQL=%d", Request, KeGetCurrentIrql()));

	status = WdfRequestRetrieveOutputBuffer(Request,
		(size_t)MinSize,	// Min size
		(PVOID *)&pVersion,
		&bytesReturned);

	KdPrint(("VersionSize = %d, pVersion = [0x%IX], bytesReturned = %d", MinSize, pVersion, bytesReturned));

	if (!NT_SUCCESS(status))
	{
		KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x", status));
		goto GOTOVERSIONDONE;
	}
	else if (pVersion == NULL)
	{
		KdPrint(("pVersion is NULL"));
		status = STATUS_INVALID_PARAMETER;
		goto GOTOVERSIONDONE;
	}
	else if (bytesReturned != sizeof(SI10_DEVICE_DRIVER_VERSION))
	{
		KdPrint(("WdfRequestRetrieveOutputBuffer returned incorrect size, size=%Id, expected=%ld",
			bytesReturned, sizeof(SI10_DEVICE_DRIVER_VERSION)));
		status = STATUS_INVALID_PARAMETER;
		goto GOTOVERSIONDONE;
	}

	pVersion->nMajorNumber = SI10_MAJOR_NUM;
	pVersion->nMinorNumber = SI10_MINOR_NUM;
	pVersion->nSubMinorNumber = SI10_SUBMINOR_NUM;
	RtlCopyMemory(pVersion->BuildNumber, SI10_BUILD_NUM, sizeof(pVersion->BuildNumber));
	transferSize = sizeof(SI10_DEVICE_DRIVER_VERSION);

GOTOVERSIONDONE:
	// If we are done, complete the request
	if (completeRequest)
	{
		WdfRequestCompleteWithInformation(Request, status, transferSize);
	}
}

VOID
EnableInterruptCallback(
	IN PDEVICE_EXTENSION 	pDevExt,
	IN WDFREQUEST			Request
)
{
	NTSTATUS 			status = STATUS_SUCCESS;
	size_t				transferSize = 0;
	BOOLEAN 			completeRequest = TRUE;
	size_t				nArrayLength = 0;
	PHANDLE				pCallbackHandle;

	UNREFERENCED_PARAMETER(pDevExt);

	// Get the input buffer pointer
	status = WdfRequestRetrieveInputBuffer(Request,
		0,	// Min size
		(PVOID *)&pCallbackHandle,
		&nArrayLength);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("WdfRequestRetrieveInputBuffer failed 0x%x", status));
	}
	else if (pCallbackHandle == NULL)
	{
		KdPrint(("Pointer to Callback Handle is NULL"));
		status = STATUS_INVALID_PARAMETER;
	}
	else

	{

		KdPrint(("Array length = %d, Addrs of Callback handle = [0x%I64X]", nArrayLength, pCallbackHandle));

		// validate the LOAD_FPGA_STRUCT members
		if (pCallbackHandle == NULL)
		{
			KdPrint(("Invalid parameter in EnableInterruptCallback"));
			status = STATUS_INVALID_PARAMETER;
			goto DOINTERRUPTCALLBACKDONE;
		}
		if (pDevExt->pIntEvent)
		{						// event already registered
			ObDereferenceObject(pDevExt->pIntEvent);
			pDevExt->pIntEvent = NULL;
		}						// event already registered

		status = ObReferenceObjectByHandle(*(PHANDLE)pCallbackHandle, EVENT_MODIFY_STATE, *ExEventObjectType, UserMode, (PVOID*)&pDevExt->pIntEvent, NULL);

		switch (status) {
		case STATUS_SUCCESS: KdPrint(("Succesful Reference object by Handle = %d %x,", status, status));
			break;
		default: KdPrint(("Failed Reference object by Handle = %d %x,", status, status));
			break;
		}
		
	}


DOINTERRUPTCALLBACKDONE:
	// If we are done, complete the request
	if (completeRequest)
	{
		WdfRequestCompleteWithInformation(Request, status, transferSize);
	}

}

/*! GetISRCount
*
*
* \brief
* \param pDevExt
* \param Request
* \param Rd_wr_n
* \return
*/
VOID
GetISRCount(
	IN PDEVICE_EXTENSION 	pDevExt,
	IN WDFREQUEST			Request
)
{
	NTSTATUS 		status = STATUS_SUCCESS;
	// Default is to complete request, forwarded requests should not be completed.
	BOOLEAN 		completeRequest = TRUE;
	size_t			MinSize = 0;
	size_t 			transferSize = 0;
	ULONG			*pulCount;
	size_t				bytesReturned;

	UNREFERENCED_PARAMETER(pDevExt);

	KdPrint(("--> Get ISR count, Request %p, IRQL=%d", Request, KeGetCurrentIrql()));

	status = WdfRequestRetrieveOutputBuffer(Request,
		(size_t)MinSize,	// Min size
		(PVOID *)&pulCount,
		&bytesReturned);

	KdPrint((" ULONG size = %d, pulCount = [0x%IX], bytesReturned = %d ISRCount = %u\n", MinSize, pulCount, bytesReturned, g_ulIsrCount));

	if (!NT_SUCCESS(status))
	{
		KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x\n", status));
		goto GOTOISRCOUNTDONE;
	}
	else if (pulCount == NULL)
	{
		KdPrint(("pulCount is NULL"));
		status = STATUS_INVALID_PARAMETER;
		goto GOTOISRCOUNTDONE;
	}
	else if (bytesReturned != sizeof(ULONG))
	{
		KdPrint(("WdfRequestRetrieveOutputBuffer returned incorrect size, size=%Id, expected=%ld\n",
			bytesReturned, sizeof(ULONG)));
		status = STATUS_INVALID_PARAMETER;
		goto GOTOISRCOUNTDONE;
	}

	*pulCount = g_ulIsrCount;
	transferSize = sizeof(ULONG);

GOTOISRCOUNTDONE:
	// If we are done, complete the request
	if (completeRequest)
	{
		WdfRequestCompleteWithInformation(Request, status, transferSize);
	}
}

/*! GetIRQSource
*
*
* \brief
* \param pDevExt
* \param Request
* \param Rd_wr_n
* \return
*/
VOID
GetIRQSource(
	IN PDEVICE_EXTENSION 	pDevExt,
	IN WDFREQUEST			Request
)
{
	NTSTATUS 		status = STATUS_SUCCESS;
	// Default is to complete request, forwarded requests should not be completed.
	BOOLEAN 		completeRequest = TRUE;
	size_t			MinSize = 0;
	size_t 			transferSize = 0;
	ULONG			*pulIRQSource;
	size_t			bytesReturned;

	UNREFERENCED_PARAMETER(pDevExt);

	KdPrint(("--> Get IRQ Source, Request %p, IRQL=%d", Request, KeGetCurrentIrql()));

	status = WdfRequestRetrieveOutputBuffer(Request,
		(size_t)MinSize,	// Min size
		(PVOID *)&pulIRQSource,
		&bytesReturned);

	KdPrint((" ULONG size = %d, pulIRQSource = [0x%IX], bytesReturned = %d IRQSource = %u\n", MinSize, pulIRQSource, bytesReturned, pDevExt->SI10_IRQ_Source));

	if (!NT_SUCCESS(status))
	{
		KdPrint(("WdfRequestRetrieveOutputBuffer failed 0x%x\n", status));
		goto GOTOISRCOUNTDONE;
	}
	else if (pulIRQSource == NULL)
	{
		KdPrint(("pulCount is NULL"));
		status = STATUS_INVALID_PARAMETER;
		goto GOTOISRCOUNTDONE;
	}
	else if (bytesReturned != sizeof(ULONG))
	{
		KdPrint(("WdfRequestRetrieveOutputBuffer returned incorrect size, size=%Id, expected=%ld\n",
			bytesReturned, sizeof(ULONG)));
		status = STATUS_INVALID_PARAMETER;
		goto GOTOISRCOUNTDONE;
	}

	*pulIRQSource = pDevExt->SI10_IRQ_Source;
	transferSize = sizeof(ULONG);

GOTOISRCOUNTDONE:
	// If we are done, complete the request
	if (completeRequest)
	{
		WdfRequestCompleteWithInformation(Request, status, transferSize);
	}
}
#pragma warning(default:4127)