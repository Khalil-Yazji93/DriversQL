#include "precomp.h"
#include "Include\PlxIoctl.h"

#if TRACE_ENABLED
#include "IoctlHandling.tmh"
#endif // TRACE_ENABLED

#pragma warning(disable:4127)	// Constants in while loops.

/*! DMADriverEvtIoDeviceControl
*
*  \brief This is the main IOCtl dispatch for
* 	the driver. The job of this routine is to validate the request and in some
*	cases route the request to the approriate DMA queue or function
*  \param Queue - WDF Managed Queue where request came from
*  \param Request - Pointer to the IOCtl request
*  \param InputBufferLength - Size of the IOCtl Input buffer
* 	\param OutputBufferLength - Size of the IOCtl Output buffer
*  \param IoControlCode - IOCTL parameter
*  \return NTSTATUS
* 	\note This routine is called at IRQL < DISPATCH_LEVEL.
*/
VOID
PLxEvtIoDeviceControl(
	IN WDFQUEUE			Queue,
	IN WDFREQUEST		Request,
	IN size_t			OutputBufferLength,
	IN size_t			InputBufferLength,
	IN unsigned long	IoControlCode
)
{
	WDFDEVICE			device = WdfIoQueueGetDevice(Queue);
	PDEVICE_EXTENSION  	pDevExt;
	NTSTATUS 			status = STATUS_SUCCESS;

	size_t				infoSize = 0;
	BOOLEAN 			completeRequest = TRUE; // Default to complete the request here
	WDF_DMA_DIRECTION	dmaDirection = WdfDmaDirectionWriteToDevice;

	UNREFERENCED_PARAMETER(device);
	UNREFERENCED_PARAMETER(OutputBufferLength);
# ifndef DBG 
	UNREFERENCED_PARAMETER(InputBufferLength);
#endif
	KdPrint((" "));
	KdPrint(("--> PLxEvtIoDeviceControl, Request %p", Request));
	KdPrint(("IoControlCode = %x  OutputBufferLength = %Id, InputBufferLength = %Id", IoControlCode, OutputBufferLength, InputBufferLength));

	pDevExt = PLxGetDeviceContext(WdfIoQueueGetDevice(Queue));

	if (pDevExt == NULL)
	{
		WdfRequestCompleteWithInformation(Request, STATUS_NO_SUCH_DEVICE, 0);
		return;
	}

	switch (IoControlCode)
	{
	case SI10_IOCTL_PCI_BAR_SPACE_READ:
		dmaDirection = WdfDmaDirectionReadFromDevice;
	case SI10_IOCTL_PCI_BAR_SPACE_WRITE:
		KdPrint(("    DO_MEM_ACCESS\n"));
		completeRequest = FALSE;
		ReadWriteMemAccess(pDevExt, Request, dmaDirection);
		break;

	case IOCTL_SI10_DOWNLOAD_FPGA_CODE:
		KdPrint(("    DO_LOAD_FPGA\n"));
		completeRequest = FALSE;
		LoadFPGABinary(pDevExt, Request);
		break;

	case SI10_IOCTL_MAILBOX_STRING_WRITE:
		KdPrint(("    DO_MAILBOX_STRING_WRITE\n"));
		completeRequest = FALSE;
		MailboxStringWrite(pDevExt, Request);
		break;

	case SI10_IOCTL_DRIVER_VERSION:
		KdPrint(("    DO_GET_VERSION_NUMBER\n"));
		completeRequest = FALSE;
		GetVersionNumber(pDevExt, Request);
		break;

	case SI10_IOCTL_ENABLE_INTERRUPT_CALLBACK:
		KdPrint(("    DO_ENABLE_INTERRUPT_CALLBACK\n"));
		completeRequest = FALSE;
		EnableInterruptCallback(pDevExt, Request);
		break;

	case SI10_IOCTL_GET_ISR_COUNT:
		KdPrint(("    DO_ISR_COUNT\n"));
		completeRequest = FALSE;
		GetISRCount(pDevExt, Request);
		break;

	case SI10_IOCTL_GET_IRQ_SOURCE:
		KdPrint(("    DO_IRQ_SOURCE\n"));
		completeRequest = FALSE;
		GetIRQSource(pDevExt, Request);
		break;

	default:
		KdPrint(("    Invalid request code %x\n", IoControlCode));
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;

	}

	if (completeRequest)
	{
		WdfRequestCompleteWithInformation(Request, status, (ULONG_PTR)infoSize);
	}
	KdPrint(("<-- PLxEvtIoDeviceControl, status 0x%x", status));
}