#include "pch.h"
#include <malloc.h>

#pragma warning(disable:4201)
#include <winioctl.h>

#include "DmaDriverDll.h"
#include "DmaDriverInt.h"
#include "DMADriverGuid.h"

//! \note   This is an example of an exported variable
//          DMADRIVERDLL_API int nDmaDriverDll=0;

//#define TIMEOUT_IO    false;

#define _DEBUG 1

//--------------------------------------------------------------------
//
//  Private DLL Interface
//
//--------------------------------------------------------------------

/*! CDmaDriverDll Default Constructor
 *
 * \brief This defaults to connecting to BoardNum == 0.
 */
CDmaDriverDll::CDmaDriverDll()
{
    AttachedToDriver = FALSE;
    BoardNumber = 0;
    hDevInfo = NULL;
    pDeviceInterfaceDetailData = NULL;
    hDevice = INVALID_HANDLE_VALUE;
    // Assume no DMA Engines found
    DmaInfo.PacketRecvEngineCount = 0;
    DmaInfo.PacketSendEngineCount = 0;
    DmaInfo.AddressablePacketMode = FALSE;
    pRxPacketBufferHandle = 0;
    return;
}

/*! CDmaDriverDll Constructor
 *
 * \brief This takes a board number to connect to.
 */
CDmaDriverDll::CDmaDriverDll(UINT32 BoardNum)
{
    AttachedToDriver = FALSE;
    BoardNumber = BoardNum;
    hDevInfo = NULL;
    pDeviceInterfaceDetailData = NULL;
    hDevice = INVALID_HANDLE_VALUE;

    // Assume no DMA Engines found
    DmaInfo.PacketRecvEngineCount = 0;
    DmaInfo.PacketSendEngineCount = 0;
    DmaInfo.AddressablePacketMode = FALSE;
    pRxPacketBufferHandle = 0;
    return;
}

/*! CDmaDriverDll Destructor
 *
 * \brief Make sure everything is cleaned up.
 */
CDmaDriverDll::~CDmaDriverDll()
{
    // Reset to no DMA Engines found
    DmaInfo.PacketRecvEngineCount = 0;
    DmaInfo.PacketSendEngineCount = 0;
    DmaInfo.AddressablePacketMode = FALSE;

    if (AttachedToDriver == TRUE) {
        int i;
        for (i = 0; i < DmaInfo.PacketRecvEngineCount; i++) {
            ReleasePacketBuffers(i);
        }
        DisconnectFromBoard();
    }
    if (hDevInfo != NULL) {
        SetupDiDestroyDeviceInfoList(hDevInfo);
        hDevInfo = NULL;
    }

    if (pDeviceInterfaceDetailData != NULL) {
        free(pDeviceInterfaceDetailData);
        pDeviceInterfaceDetailData = NULL;
    }
}

/*! ConnectToBoard
 *
 * \brief This connects to the board you are interested in talking to.
 * \param pDmaInfo
 * \return status
 */
UINT32 CDmaDriverDll::ConnectToBoard(PDMA_INFO_STRUCT pDmaInfo)
{
    SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
    UINT32 count = 0;
    DWORD RequiredSize = 0;
    BOOL BoolStatus = TRUE;
    DMA_CAP_STRUCT DMACap;
    BOARD_CONFIG_STRUCT BoardCfg;
    INT8 i;
    int nRet = STATUS_SUCCESSFUL;

    DmaInfo.DLLMajorVersion = VER_MAJOR_NUM;
    DmaInfo.DLLMinorVersion = VER_MINOR_NUM;
    DmaInfo.DLLSubMinorVersion = VER_SUBMINOR_NUM;
    DmaInfo.DLLBuildNumberVersion = VER_BUILD_NUM;

    // Get the device info
    if (hDevInfo == NULL) {
        hDevInfo = SetupDiGetClassDevs(&GUID_ULTRASONICS_DRIVER_INTERFACE, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

        if (hDevInfo == INVALID_HANDLE_VALUE) {
            printf("%s: SetupDiGetClassDevs returned INVALID_HANDLE_VALUE, error = 0x%x\n", __func__, GetLastError());
        }
    }
    // Count the number of devices present
    DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    while (SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_ULTRASONICS_DRIVER_INTERFACE, count++, &DeviceInterfaceData) == TRUE);

    // last one failed, find out why
    if (GetLastError() != ERROR_NO_MORE_ITEMS) {
        printf("%s: SetupDiEnumDeviceInterfaces returned FALSE, index= %d, error = %d\n    Should be ERROR_NO_MORE_ITEMS (%d)\n", __func__, count - 1, GetLastError(), ERROR_NO_MORE_ITEMS);
    }
    // Always counts one too many
    count--;

    // Check to see if there are any boards present
    if (count == 0) {
        printf("%s: No boards present.\n", __func__);
        return STATUS_INCOMPLETE;
    }
    else if (count <= BoardNumber)
        // check to see if the BoardNumber if valid
    {
        printf("%s: Invalid board number.\n", __func__);
        return STATUS_INVALID_BOARDNUM;
    }
    // Get information for device

    for (BoardNumber; BoardNumber < count; BoardNumber++) {
        BoolStatus = SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_ULTRASONICS_DRIVER_INTERFACE, BoardNumber, &DeviceInterfaceData);
        if (BoolStatus == FALSE) {
            printf("%s: SetupDiEnumDeviceInterfaces failed for board, BoardNumber = %d\n  Error: %d\n", __func__, BoardNumber, GetLastError());
            return STATUS_INCOMPLETE;
        }

        /*! \note
         *  Get the Device Interface Detailed Data
         *  This is done in multiple parts:
         *  1.  query for the required size of the data structure (fix part + variable part)
         *  2.  malloc the returned size
         *  3.  Set the cbSize of the data structure to be the fixed size (required by interface)
         *  4.  query for the actual data
         */
        BoolStatus = SetupDiGetDeviceInterfaceDetail(hDevInfo, &DeviceInterfaceData, NULL,      // do not get details yet
            0, &RequiredSize, NULL);

        // this should fail (returning false) and setting error to ERROR_INSUFFICIENT_BUFFER
        if ((BoolStatus == TRUE) || (GetLastError() != ERROR_INSUFFICIENT_BUFFER)) {
            printf("%s: SetupDiGetDeviceInterfaceDetail failed for board, BoardNumber = %d\n  Error: %d\n", __func__, BoardNumber, GetLastError());
            return STATUS_INCOMPLETE;
        }
        // allocate the correct size
        pDeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(RequiredSize);

        if (pDeviceInterfaceDetailData == NULL) {
            printf("%s: Insufficient memory, pDeviceInterfaceDetailData\n", __func__);
            return STATUS_INCOMPLETE;
        }
        // set the size to the fixed data size (not the full size)
        pDeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        // get the data
        BoolStatus = SetupDiGetDeviceInterfaceDetail(hDevInfo, &DeviceInterfaceData, pDeviceInterfaceDetailData, RequiredSize, NULL, NULL);     // Do not need DeviceInfoData at this time

        if (BoolStatus == FALSE) {
            printf("%s: SetupDiGetDeviceInterfaceDetail failed for board, BoardNumber = %d\n  Error: %d\n", __func__, BoardNumber, GetLastError());
            return STATUS_INCOMPLETE;
        }
        // Now connect to the card
        hDevice = CreateFile(pDeviceInterfaceDetailData->DevicePath,
            GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, INVALID_HANDLE_VALUE);


        if (hDevice == INVALID_HANDLE_VALUE) {
            printf("%s: Error opening device.  Error = %d\n", __func__, GetLastError());
            return STATUS_HANDLE_INVALID;
        }
        // set flag for other function calls
        this->AttachedToDriver = true;

        // Assume no DMA Engines found
        DmaInfo.PacketRecvEngineCount = 0;
        DmaInfo.PacketSendEngineCount = 0;
        DmaInfo.AddressablePacketMode = false;
        DmaInfo.DMARegistersBAR = 0;

        GetBoardCfg(&BoardCfg);
        if ((BoardCfg.DriverVersionMajor == VER_MAJOR_NUM) && (BoardCfg.DriverVersionMinor == VER_MINOR_NUM)) {
            DmaInfo.DMARegistersBAR = BoardCfg.DMARegistersBAR;
            // Get DMA Engine cap to extract the engine numbers for the packet and block mode engines
            for (i = 0; i < MAX_NUM_DMA_ENGINES; i++) {
                DmaInfo.PacketRecvEngine[i] = -1;
                DmaInfo.PacketSendEngine[i] = -1;

                GetDMAEngineCap(i, &DMACap);
                if ((DMACap.DmaCapabilities & DMA_CAP_ENGINE_PRESENT) == DMA_CAP_ENGINE_PRESENT) {
                    if ((DMACap.DmaCapabilities & DMA_CAP_ENGINE_TYPE_MASK) & DMA_CAP_PACKET_DMA) {
                        if ((DMACap.DmaCapabilities & DMA_CAP_ENGINE_TYPE_MASK) & DMA_CAP_ADDRESSABLE_PACKET_DMA) {
                            DmaInfo.AddressablePacketMode = true;
                        }
                        if ((DMACap.DmaCapabilities & DMA_CAP_DIRECTION_MASK) == DMA_CAP_SYSTEM_TO_CARD) {
                            DmaInfo.PacketSendEngine[DmaInfo.PacketSendEngineCount++] = i;
                        }
                        else if ((DMACap.DmaCapabilities & DMA_CAP_DIRECTION_MASK) == DMA_CAP_CARD_TO_SYSTEM) {
                            DmaInfo.PacketRecvEngine[DmaInfo.PacketRecvEngineCount++] = i;
                        }
                        else {
                        }
                    }
                }
            }
            memcpy(pDmaInfo, &DmaInfo, sizeof(DMA_INFO_STRUCT));
            return STATUS_SUCCESSFUL;
        }
        else {
            printf("%s: DMADriverDLL ERROR: Driver version mismatch\n", __func__);
            printf("%s: \tDriver Version = %d.%d.%d.%d\n", __func__, BoardCfg.DriverVersionMajor, BoardCfg.DriverVersionMinor, BoardCfg.DriverVersionSubMinor, BoardCfg.DriverVersionBuildNumber);
            memcpy(pDmaInfo, &DmaInfo, sizeof(DMA_INFO_STRUCT));
            nRet = STATUS_DEVICEDRIVER_INCOMPATABLE_VER;
        }
    }

    return nRet;
}

/*! DisconnectFromBoard
 *
 * \brief Disconnects from a board.
 * Cleanup any global data structures that have been created.
 * \return STATUS_SUCCESSFUL
 */
UINT32 CDmaDriverDll::DisconnectFromBoard()
{
    AttachedToDriver = false;

    if (hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(hDevice);
        hDevice = INVALID_HANDLE_VALUE;
    }
    // Free up device detail
    if (pDeviceInterfaceDetailData != NULL) {
        free(pDeviceInterfaceDetailData);
        pDeviceInterfaceDetailData = NULL;
    }

    return STATUS_SUCCESSFUL;
}

/*! GetBoardCfg
 *
 * \brief Sends a GetBoardCfg IOCTL call to the driver.
 * \param Board
 * \returns status - The data in the BOARD_CONFIG_STRUCT sent to the call.
 */
UINT32 CDmaDriverDll::GetBoardCfg(PBOARD_CONFIG_STRUCT Board)
{
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // Send GET_BOARD_CONFIG_IOCTL
    if (!DeviceIoControl(hDevice, GET_BOARD_CONFIG_IOCTL, NULL, 0, (LPVOID)Board, sizeof(BOARD_CONFIG_STRUCT), &bytesReturned, &os)) {
        LastErrorStatus = GetLastError();
        if (LastErrorStatus == ERROR_IO_PENDING) {
            // Wait here (forever) for the Overlapped I/O to complete
            if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                LastErrorStatus = GetLastError();
                printf("%s: GetBoardCfg IOCTL call failed. Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }
        else {
            printf("%s: GetBoardCfg IOCTL call failed. Error = %d\n", __func__, GetLastError());
            status = LastErrorStatus;
        }
    }
    // check returned structure size
    if ((bytesReturned != sizeof(BOARD_CONFIG_STRUCT)) && (status == STATUS_SUCCESSFUL)) {
        printf("%s: GetBoardCfg IOCTL returned invalid size (%d)\n", __func__, bytesReturned);
        status = STATUS_INCOMPLETE;
    }

    CloseHandle(os.hEvent);
    return status;
}

/*! GetDMAEngineCap
 *
 * \brief Returns the capabilities of the DMA Engine.
 * \param EngineNum
 * \param DMACap
 * \return status
 */
UINT32 CDmaDriverDll::GetDMAEngineCap(INT32 EngineNum, PDMA_CAP_STRUCT DMACap)
{
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // Send BLOCK_DIRECT_GET_PERF_IOCTL IOCTL
    if (DeviceIoControl(hDevice, GET_DMA_ENGINE_CAP_IOCTL, (LPVOID)&EngineNum, sizeof(UINT32), (LPVOID)DMACap, sizeof(DMA_CAP_STRUCT), &bytesReturned, &os) == 0) {
        LastErrorStatus = GetLastError();
        if (LastErrorStatus == ERROR_IO_PENDING) {
            // Wait here (forever) for the Overlapped I/O to complete
            if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                LastErrorStatus = GetLastError();
                printf("%s: GetDMACap IOCTL call failed. Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }
        else {
            printf("%s: GetDMACap IOCTL call failed. Error = %d\n", __func__, LastErrorStatus);
            status = LastErrorStatus;
        }
    }
    // check returned structure size
    if ((bytesReturned != sizeof(DMA_CAP_STRUCT)) && (status == STATUS_SUCCESSFUL)) {
        printf("%s: GetDMACap IOCTL returned invalid size (%d)\n", __func__, bytesReturned);
        status = STATUS_INCOMPLETE;
    }

    CloseHandle(os.hEvent);
    return status;
}

/*! WritePCIConfig
 *
 * \brief Sends a WRITE_PCI_CONFIG IOCTL call to the driver.
 * \param Buffer
 * \param Offset
 * \param Length
 * \return Completion Status in the STAT_STRUCT sent to the call.
 */
UINT32 CDmaDriverDll::WritePCIConfig(PUINT8 Buffer, UINT32 Offset, UINT32 Length, PSTAT_STRUCT Status)
{
    RW_PCI_CONFIG_STRUCT WrPCIStruct;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // fill in the WritePCIConfig Structure
    WrPCIStruct.Offset = Offset;
    WrPCIStruct.Length = Length;

    // Send Write PCI Configuration IOCTL
    if (!DeviceIoControl(hDevice, WRITE_PCI_CONFIG_IOCTL, (LPVOID)&WrPCIStruct, sizeof(RW_PCI_CONFIG_STRUCT), (LPVOID)Buffer, (DWORD)Length, &bytesReturned, &os)) {
        LastErrorStatus = GetLastError();
        if (LastErrorStatus == ERROR_IO_PENDING) {
            // Wait here (forever) for the Overlapped I/O to complete
            if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                LastErrorStatus = GetLastError();
                printf("%s: WritePCIConfig IOCTL call failed. Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }
        else {
            Status->CompletedByteCount = 0;
            printf("%s: WritePCIConfig IOCTL call failed. Error = %d\n", __func__, LastErrorStatus);
            status = LastErrorStatus;
        }
    }
    // save the returned size
    Status->CompletedByteCount = bytesReturned;
    // check returned structure size
    if ((bytesReturned != Length) && (status == STATUS_SUCCESSFUL)) {
        printf("%s: WritePCIConfig IOCTL returned invalid size (%d), expected length %d\n", __func__, bytesReturned, Length);
        status = STATUS_INCOMPLETE;
    }

    CloseHandle(os.hEvent);
    return status;
}

/*! ReadPCIConfig
 *
 * \brief Sends a READ_PCI_CONFIG IOCTL call to the driver.
 * \param Buffer
 * \param Offset
 * \param Length
 * \return Completion Status in the STAT_STRUCT sent to the call.
 */
UINT32 CDmaDriverDll::ReadPCIConfig(PUINT8 Buffer, UINT32 Offset, UINT32 Length, PSTAT_STRUCT Status)
{
    RW_PCI_CONFIG_STRUCT RdPCIStruct;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // fill in the WritePCIConfig Structure
    RdPCIStruct.Offset = Offset;
    RdPCIStruct.Length = Length;

    // Send Write PCI Configuration IOCTL
    if (!DeviceIoControl(hDevice, READ_PCI_CONFIG_IOCTL, (LPVOID)&RdPCIStruct, sizeof(RW_PCI_CONFIG_STRUCT), (LPVOID)Buffer, (DWORD)Length, &bytesReturned, &os)) {
        LastErrorStatus = GetLastError();
        if (LastErrorStatus == ERROR_IO_PENDING) {
            // Wait here (forever) for the Overlapped I/O to complete
            if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                LastErrorStatus = GetLastError();
                printf("%s: ReadPCIConfig IOCTL call failed. Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }
        else {
            Status->CompletedByteCount = 0;
            printf("%s: ReadPCIConfig IOCTL call failed. Error = %d\n", __func__, LastErrorStatus);
            status = LastErrorStatus;
        }
    }
    // save the returned size
    Status->CompletedByteCount = bytesReturned;
    // check returned structure size
    if ((bytesReturned != Length) && (status == STATUS_SUCCESSFUL)) {
        printf("%s: ReadPCIConfig IOCTL returned invalid size (%d), expected length %d\n", __func__, bytesReturned, Length);
        status = STATUS_INCOMPLETE;
    }

    CloseHandle(os.hEvent);
    return status;
}

/*! DoMem
 *
 * \brief Sends a DoMem IOCTL call to the driver.
 * \param Rd_Wr_n
 * \param BarNum
 * \param Buffer
 * \param Offset
 * \param CardOffset
 * \param Length
 * \return Completion status in the STAT_STRUCT sent to the call.
 */
UINT32 CDmaDriverDll::DoMem(UINT32 Rd_Wr_n, UINT32 BarNum, PUINT8 Buffer, UINT64 Offset, UINT64 CardOffset, UINT64 Length, PSTAT_STRUCT Status)
{
    DO_MEM_STRUCT doMemStruct;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD ioctlCode;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // fill in the doMem Structure
    doMemStruct.BarNum = BarNum;
    doMemStruct.Offset = Offset;
    doMemStruct.CardOffset = CardOffset;
    doMemStruct.Length = Length;

    // determine the ioctl code
    if (Rd_Wr_n == READ_FROM_CARD) {
        ioctlCode = DO_MEM_READ_ACCESS_IOCTL;
    }
    else {
        ioctlCode = DO_MEM_WRITE_ACCESS_IOCTL;
    }

    // Send DoMem IOCTL
    if (!DeviceIoControl(hDevice, ioctlCode, (LPVOID)&doMemStruct, sizeof(DO_MEM_STRUCT), (LPVOID)Buffer, (DWORD)Length, &bytesReturned, &os)) {
        LastErrorStatus = GetLastError();
        if (LastErrorStatus == ERROR_IO_PENDING) {
            // Wait here (forever) for the Overlapped I/O to complete
            if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                LastErrorStatus = GetLastError();
                printf("%s: GetOverlappedResult() call failed. Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }
        else {
            Status->CompletedByteCount = 0;
            printf("%s: DoMem IOCTL call failed. Error = %d\n", __func__, LastErrorStatus);
            status = LastErrorStatus;
        }
    }
    // save the returned size
    Status->CompletedByteCount = bytesReturned;
    // check returned structure size
//  printf("DoMem bytesReturned=%d, Length=%d\n", bytesReturned, Length);

    if ((bytesReturned != Length) && (status == STATUS_SUCCESSFUL)) {
        printf("%s: DoMem IOCTL returned invalid size (%d), expected length %llu\n", __func__, bytesReturned, Length);
        status = STATUS_INCOMPLETE;
    }

    CloseHandle(os.hEvent);
    return status;
}

/*! GetDmaPerf
 *
 * \brief Gets DMA Performance numbers from the board.
 * \param EngineNumOffset
 * \param TypeDirection
 * \param Status
 * \return Completion Status
 */
UINT32 CDmaDriverDll::GetDmaPerf(INT32 EngineNumOffset, UINT32 TypeDirection,   // DMA Type & Direction Flags
    PDMA_STAT_STRUCT Status)
{
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    INT32 EngineNum = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // determine the ioctl code
    if ((TypeDirection & DMA_CAP_DIRECTION_MASK) == DMA_CAP_CARD_TO_SYSTEM) {
        if ((TypeDirection & DMA_CAP_ENGINE_TYPE_MASK) == DMA_CAP_PACKET_DMA) {
            if (EngineNumOffset < DmaInfo.PacketRecvEngineCount) {
                EngineNum = DmaInfo.PacketRecvEngine[EngineNumOffset];
            }
        }
    }
    else                  // Direction is S2C
    {
        if ((TypeDirection & DMA_CAP_ENGINE_TYPE_MASK) == DMA_CAP_PACKET_DMA) {
            if (EngineNumOffset < DmaInfo.PacketSendEngineCount) {
                EngineNum = DmaInfo.PacketSendEngine[EngineNumOffset];
            }
        }
    }

    // Send GET_PERF_IOCTL IOCTL
    if (!DeviceIoControl(hDevice, GET_PERF_IOCTL, (LPVOID)&EngineNum, sizeof(UINT32), (LPVOID)Status, sizeof(DMA_STAT_STRUCT), &bytesReturned, &os)) {
        LastErrorStatus = GetLastError();
        if (LastErrorStatus == ERROR_IO_PENDING) {
            // Wait here (forever) for the Overlapped I/O to complete
            if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                LastErrorStatus = GetLastError();
                printf("%s: GetPerf: Overlapped failed. Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
            // Make sure we returned something useful
        }
        else {
            printf("%s: GetDmaPerf IOCTL call failed. Error = %d\n", __func__, LastErrorStatus);
            status = LastErrorStatus;
        }
    }
    // check returned structure size
    if ((bytesReturned != sizeof(DMA_STAT_STRUCT) && status == STATUS_SUCCESSFUL)) {
        printf("%s: GetDmaPerf IOCTL returned invalid size (%d)\n", __func__, bytesReturned);
        status = STATUS_INCOMPLETE;
    }
    CloseHandle(os.hEvent);
    return status;
}

//**************************************************
// FIFO Packet Mode Function calls
//**************************************************

/*! PacketSendEx - Includes User Control
 *
 * \brief Send a PACKET_SEND_IOCTL call to the driver.
 *        Includes 64-bit UserControl.
 * \param EngineOffset - DMA Engine number offset to use
 * \param UserControl - User Control to set in the first DMA Descriptor
 * \param CardOffset
 * \param Buffer
 * \param Length
 * \return Completion status.
 */
UINT32 CDmaDriverDll::PacketSendEx(INT32 EngineOffset,  // DMA Engine number offset to use
    UINT64 UserControl,  // User Control to set in the first DMA Descriptor
    UINT64 CardOffset, PUINT8 Buffer, UINT32 Length)
{
    PACKET_SEND_STRUCT PacketSend;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (os.hEvent == NULL) {
        return GetLastError();
    }

    if (EngineOffset < DmaInfo.PacketSendEngineCount) {
        // Select a Packet Send DMA Engine
        PacketSend.EngineNum = DmaInfo.PacketSendEngine[EngineOffset];
        PacketSend.CardOffset = CardOffset;
        PacketSend.Length = Length;
        PacketSend.UserControl = UserControl;

        if (!DeviceIoControl(hDevice, PACKET_SEND_IOCTL, &PacketSend, sizeof(PACKET_SEND_STRUCT), (LPVOID)Buffer, (DWORD)Length, &bytesReturned, &os)) {
            LastErrorStatus = GetLastError();
#ifdef TIMEOUT_IO
            while ((LastErrorStatus == ERROR_IO_PENDING) || (LastErrorStatus == ERROR_IO_INCOMPLETE)) {
                // Wait here (forever) for the Overlapped I/O to complete
                if (!GetOverlappedResult(hDevice, &os, &bytesReturned, FALSE)) {
                    LastErrorStatus = GetLastError();
                    if ((LastErrorStatus == ERROR_IO_PENDING) || (LastErrorStatus == ERROR_IO_INCOMPLETE)) {
                        if (!--TimeoutCount) {
                            printf("Packet Send Overlapped Timed Out. Error = %d\n", LastErrorStatus);
                            status = LastErrorStatus;
                            break;
                        }
                    }
                    else {
                        printf("%s: Packet Send Overlapped failed. Error = %d\n", __func__, LastErrorStatus);
                        status = LastErrorStatus;
                        break;
                    }
                }
                else {
                    break;
                }
                // Make sure we returned something useful
            }
#else
            if (LastErrorStatus == ERROR_IO_PENDING) {
                // Wait here (forever) for the Overlapped I/O to complete
                if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                    LastErrorStatus = GetLastError();
                    printf("%s: Packet Send overlapped failed. Error = %d\n", __func__, LastErrorStatus);
                    status = LastErrorStatus;
                }
            }
#endif                          // TIME OUT I/O
        }
    }
    else {
        printf("%s: DLL: Packet Send failed. No Packet Send Engine\n", __func__);
        status = STATUS_INVALID_MODE;
    }
    CloseHandle(os.hEvent);
    return status;
}

UINT32 CDmaDriverDll::_PacketReceive(
    INT32 EngineOffset,       // DMA Engine number offset to use
    PUINT64 UserStatus,       // User Status returned from the last DMA Descriptor
    PUINT32 BufferToken,
    PVOID Buffer,
    PUINT32 Length,
    BOOLEAN Blocking)
{
    PACKET_RECEIVE_STRUCT PacketRecv;
    PACKET_RET_RECEIVE_STRUCT PacketRecvRet;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;
#ifdef PLATFORM_X64
    PUINT64 pBuffer = (PUINT64)Buffer;
#else                           // 32 bit code
    PUINT32 pBuffer = (PUINT32)Buffer;
#endif                          // 64 Bit version

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (os.hEvent == NULL) {
        return GetLastError();
    }

    if (EngineOffset < DmaInfo.PacketRecvEngineCount) {
        PacketRecv.EngineNum = DmaInfo.PacketRecvEngine[EngineOffset];
        // Indicate Rx Token (if any)
        PacketRecv.RxReleaseToken = *BufferToken;
        PacketRecv.Block = Blocking;

        *BufferToken = INVALID_RELEASE_TOKEN;
        *Length = 0;
        if (UserStatus != NULL) {
            *UserStatus = 0;
        }
        // Send Packet Mode Release space
        if (!DeviceIoControl(hDevice, PACKET_RECEIVE_IOCTL, &PacketRecv, sizeof(PACKET_RECEIVE_STRUCT), &PacketRecvRet, sizeof(PACKET_RET_RECEIVE_STRUCT), &bytesReturned, &os)) {
            LastErrorStatus = GetLastError();
            if (LastErrorStatus == ERROR_IO_PENDING) {
                // Wait here (forever) for the Overlapped I/O to complete
                if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                    LastErrorStatus = GetLastError();
                    printf("%s: Overlapped failed. Error = %d\n", __func__, LastErrorStatus);
                    status = LastErrorStatus;
                }
            }
            else {
                printf("%s: failed, Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }

        if (status == STATUS_SUCCESSFUL) {
            if (bytesReturned != sizeof(PACKET_RET_RECEIVE_STRUCT)) {
                printf("%s: IOCTL returned invalid size (%d)\n", __func__, bytesReturned);
                status = STATUS_INVALID_MODE;
            }
            else {
                if (PacketRecvRet.RxToken != INVALID_RELEASE_TOKEN) {
                    if (UserStatus != NULL) {
                        *UserStatus = PacketRecvRet.UserStatus;
                    }
                    *BufferToken = PacketRecvRet.RxToken;
#ifdef PLATFORM_X64
                    * pBuffer = PacketRecvRet.Address;
#else                           // 32 bit code
                    * pBuffer = (UINT32)PacketRecvRet.Address;
#endif                          // 64 Bit version
                    * Length = PacketRecvRet.Length;
                }
                else {
                    status = STATUS_INCOMPLETE;
                }
            }
        }
    }
    else {
        status = STATUS_INVALID_MODE;
    }
    CloseHandle(os.hEvent);
    return status;
}

/*! PacketReceiveEx -
 *
 * \brief Send a PACKET_RECEIVE_IOCTL call to the driver and waits for a completion.
 *  Includes 64-bit User Status.
 * \param EngineOffset
 * \param UserStatus
 * \param BufferToken
 * \param Buffer
 * \param Length
 * \return Completion status.
 */
UINT32 CDmaDriverDll::PacketReceiveEx(INT32 EngineOffset,       // DMA Engine number offset to use
    PUINT64 UserStatus,       // User Status returned from the last DMA Descriptor
    PUINT32 BufferToken, PVOID Buffer, PUINT32 Length)
{
    return _PacketReceive(EngineOffset, UserStatus, BufferToken, Buffer, Length, TRUE);
}

/*! PacketReceiveNB -
*
* \brief Send a PACKET_RECEIVE_IOCTL call to the driver and waits for a completion.
*   Includes 64-bit User Status.
* \param EngineOffset
* \param UserStatus
* \param BufferToken
* \param Buffer
* \param Length
* \return Completion status.
*/
UINT32 CDmaDriverDll::PacketReceiveNB(INT32 EngineOffset,       // DMA Engine number offset to use
    PUINT64 UserStatus,       // User Status returned from the last DMA Descriptor
    PUINT32 BufferToken, PVOID Buffer, PUINT32 Length)
{
    return _PacketReceive(EngineOffset, UserStatus, BufferToken, Buffer, Length, FALSE);
}

/*! PacketReturnReceive
 *
 * \brief Send a PACKET_RECEIVE_IOCTL call to the driver to return a buffer token
 * \param EngineOffset
 * \param BufferToken
 * \return Completion status.
 */
UINT32 CDmaDriverDll::PacketReturnReceive(INT32 EngineOffset,   // DMA Engine number offset to use
    PUINT32 BufferToken)
{
    PACKET_RECEIVE_STRUCT PacketRecv;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (os.hEvent == NULL) {
        return GetLastError();
    }

    if (EngineOffset < DmaInfo.PacketRecvEngineCount) {
        PacketRecv.EngineNum = DmaInfo.PacketRecvEngine[EngineOffset];
        // Indicate Rx Token
        PacketRecv.RxReleaseToken = *BufferToken;

        // For the Packet Return we use the standard Packet Receive call but
        // we do not specify a Out or Return buffer
        if (!DeviceIoControl(hDevice, PACKET_RECEIVE_IOCTL, &PacketRecv, sizeof(PACKET_RECEIVE_STRUCT), NULL, 0, &bytesReturned, &os)) {
            LastErrorStatus = GetLastError();
            if (LastErrorStatus == ERROR_IO_PENDING) {
                // Wait here (forever) for the Overlapped I/O to complete
                if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                    LastErrorStatus = GetLastError();
                    printf("%s: Packet Return Overlapped failed. Error = %d\n", __func__, LastErrorStatus);
                    status = LastErrorStatus;
                }
            }
            else {
                printf("%s: Packet Return Rx failed for Token %d. Error = %d\n", __func__, PacketRecv.RxReleaseToken, LastErrorStatus);
                status = LastErrorStatus;
            }
        }
    }
    else {
        status = STATUS_INVALID_MODE;
    }
    CloseHandle(os.hEvent);
    return status;
}

/*! PacketReceives
 *
 * \brief Send a PACKET_RECEIVES_IOCTL call to the driver and waits for a completion
 * \param EngineOffset
 * \param pPacketRecvs
 * \return Completion status.
 */
UINT32 CDmaDriverDll::PacketReceives(INT32 EngineOffset,        // DMA Engine number offset to use
    PPACKET_RECVS_STRUCT pPacketRecvs)
{
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD PacketSize = 0;
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (os.hEvent == NULL) {
        return GetLastError();
    }

    if (EngineOffset < DmaInfo.PacketRecvEngineCount) {
        pPacketRecvs->EngineNum = DmaInfo.PacketRecvEngine[EngineOffset];

        PacketSize = sizeof(PACKET_RECVS_STRUCT) + (pPacketRecvs->AvailNumEntries * sizeof(PACKET_ENTRY_STRUCT));

        // Send Packet Recieve multiples request
        if (!DeviceIoControl(hDevice, PACKET_RECEIVES_IOCTL, NULL, 0, pPacketRecvs, PacketSize, &bytesReturned, &os)) {
            LastErrorStatus = GetLastError();
            if (LastErrorStatus == ERROR_IO_PENDING) {
                // Wait here (forever) for the Overlapped I/O to complete
                if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                    LastErrorStatus = GetLastError();
                    printf("%s: Packet Recvs Overlapped failed. Error = %d\n", __func__, LastErrorStatus);
                    status = LastErrorStatus;
                }
            }
            else {
                printf("%s: Packet Recvs failed, Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }
        // Make sure we returned something useful
        if (status == STATUS_SUCCESSFUL) {
            // Make sure we returned something useful
            if (bytesReturned != PacketSize) {
                printf("%s: Packet Recvs IOCTL returned invalid size (%d)\n", __func__, bytesReturned);
                status = STATUS_INCOMPLETE;
            }
        }
    }
    else {
        status = STATUS_INVALID_MODE;
    }
    CloseHandle(os.hEvent);
    return status;
}

//-------------------------------------------------------------------------
// Addressable Packet Mode Function calls
//-------------------------------------------------------------------------

/*! PacketWriteEx
 *
 * \brief Send a PACKET_WRITE_IOCTL call to the driver
 * \param EngineOffset - DMA Engine number offset to use
 * \param UserControl - User Control to set in the first DMA Descriptor
 * \param CardOffset
 * \param Mode - Control Mode Flags
 * \param Buffer
 * \param Length
 * \return Completion status.
 */
UINT32 CDmaDriverDll::PacketWriteEx(INT32 EngineOffset, UINT64 UserControl, UINT64 CardOffset, UINT32 Mode, PUINT8 Buffer, UINT32 Length) {
    PACKET_WRITE_STRUCT sPacketWrite;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (os.hEvent == NULL) {
        return GetLastError();
    }

    if (EngineOffset < DmaInfo.PacketSendEngineCount) {
        // Select a Packet Send DMA Engine
        sPacketWrite.EngineNum = DmaInfo.PacketSendEngine[EngineOffset];
        sPacketWrite.UserControl = UserControl;
        sPacketWrite.ModeFlags = Mode;
        sPacketWrite.CardOffset = CardOffset;
        sPacketWrite.Length = Length;

        if (!DeviceIoControl(hDevice, PACKET_WRITE_IOCTL, &sPacketWrite, sizeof(PACKET_WRITE_STRUCT), (LPVOID)Buffer, (DWORD)Length, &bytesReturned, &os)) {
            LastErrorStatus = GetLastError();
            if (LastErrorStatus == ERROR_IO_PENDING) {
                // Wait here (forever) for the Overlapped I/O to complete
                if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                    LastErrorStatus = GetLastError();
                    printf("%s: Packet Write Overlapped failed. Error = %d\n", __func__, LastErrorStatus);
                    status = LastErrorStatus;
                }
            }
            else {
                printf("%s: Packet Write failed, Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }               // if (!DeviceIoControl...
        // Make sure we returned something useful
        if (status == STATUS_SUCCESSFUL) {
            if (bytesReturned != Length) {
                printf("%s: Packet Write failed. Return size does not equal request (Ret=%d)\n", __func__, bytesReturned);
                status = STATUS_INCOMPLETE;
            }
        }
    }
    else {
        printf("%s: DLL: Packet Write failed. No Packet Send Engine\n", __func__);
        status = STATUS_INVALID_MODE;
    }
    CloseHandle(os.hEvent);
    return status;
}

/*! PacketRead
 *
 * \brief Send a PACKET_READ_IOCTL call to the driver
 * \param EngineOffset - DMA Engine number offset to use
 * \param UserStatus - User Status to set in the first DMA Descriptor
 * \param CardOffset
 * \param Mode - Control Mode Flags
 * \param Buffer
 * \param Length
 * \return Completion status.
 */
UINT CDmaDriverDll::PacketReadEx(INT32 EngineOffset,
    PUINT64 UserStatus,
    UINT64 CardOffset, UINT32 Mode,
    PUINT8 Buffer, PUINT32 Length)
{
    PACKET_READ_STRUCT sPacketRead;
    PACKET_RET_READ_STRUCT sRetPacketRead;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (os.hEvent == NULL) {
        return GetLastError();
    }

    if (EngineOffset < DmaInfo.PacketRecvEngineCount) {
        // Select a Packet Read DMA Engine
        sPacketRead.EngineNum = DmaInfo.PacketRecvEngine[EngineOffset];
        sPacketRead.CardOffset = CardOffset;
        sPacketRead.ModeFlags = Mode;
        sPacketRead.BufferAddress = (UINT64)Buffer;
        sPacketRead.Length = *Length;

        if (!DeviceIoControl(hDevice, PACKET_READ_IOCTL, &sPacketRead, sizeof(PACKET_READ_STRUCT), &sRetPacketRead, sizeof(PACKET_RET_READ_STRUCT), &bytesReturned, &os)) {
            LastErrorStatus = GetLastError();
            if (LastErrorStatus == ERROR_IO_PENDING) {
                // Wait here (forever) for the Overlapped I/O to complete
                if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                    LastErrorStatus = GetLastError();
                    printf("%s: Packet Read Overlapped failed. Error = %d\n", __func__, LastErrorStatus);
                    status = LastErrorStatus;
                }
            }
            else {
                printf("%s: Packet Read failed, Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }               // if (!DeviceIoControl ...
        *UserStatus = 0;
        *Length = 0;
        // Make sure we returned something useful
        if (status == STATUS_SUCCESSFUL) {
            if (bytesReturned == sizeof(PACKET_RET_READ_STRUCT)) {
                *UserStatus = sRetPacketRead.UserStatus;
                *Length = sRetPacketRead.Length;
            }
            else {
                printf("%s: Packet Read failed. Return structure size is mismatched (Ret=%d)\n", __func__, bytesReturned);
                status = STATUS_INCOMPLETE;
            }
        }
    }                       // if (EngineOffset ...
    else {
        printf("%s: DLL: Packet Read failed. No Packet Read Engine\n", __func__);
        status = STATUS_INVALID_MODE;
    }
    CloseHandle(os.hEvent);
    return status;
}

//-------------------------------------------------------------------------
// Common Packet Mode Function calls
//-------------------------------------------------------------------------

/*! SetupPacket
 *
 * \brief Sends PACKET_BUF_ALLOC_IOCTL call to the driver to setup the recieve buffer
 *  and intialize the descriptors for Reading or Receiving packets
 * \param EngineOffset
 * \param Buffer
 * \param BufferSize
 * \param MaxPacketSize
 * \param PacketModeSetting
 * \param NumberDescriptors
 * \return Completion status.
 */
UINT32 CDmaDriverDll::SetupPacket(INT32 EngineOffset,   // DMA Engine number offset to use
    PUINT8 Buffer, PUINT32 BufferSize, PUINT32 MaxPacketSize, INT32 PacketModeSetting, INT32 NumberDescriptors)
{
    BUF_ALLOC_STRUCT BufAlloc;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (os.hEvent == NULL) {
        return GetLastError();
    }

    if (EngineOffset < DmaInfo.PacketRecvEngineCount) {
        // Set the DMA Engine we want to allocate for
        BufAlloc.EngineNum = DmaInfo.PacketRecvEngine[EngineOffset];

        if ((PacketModeSetting == PACKET_MODE_FIFO) || (PacketModeSetting == PACKET_MODE_STREAMING)) {
            // This is an application allocated the buffer
            BufAlloc.AllocationMode = PacketModeSetting;
            // Allocate the size of...
            BufAlloc.Length = *BufferSize;
            // Allocate the number of decriptors based on the Maximum Packet Size we can handle
            BufAlloc.MaxPacketSize = *MaxPacketSize;
            BufAlloc.NumberDescriptors = 0;

            // Send FIFO Packet Mode Setup IOCTL
            if (!DeviceIoControl(hDevice, PACKET_BUF_ALLOC_IOCTL, &BufAlloc, sizeof(BUF_ALLOC_STRUCT), (LPVOID)Buffer, (DWORD)*BufferSize, &bytesReturned, &os)) {
                LastErrorStatus = GetLastError();
                if (LastErrorStatus == ERROR_IO_PENDING) {
                    // Wait here (forever) for the Overlapped I/O to complete
                    if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                        LastErrorStatus = GetLastError();
                        printf("%s: FIFO Packet Mode setup overlap failed. Error = %d\n", __func__, LastErrorStatus);
                        status = LastErrorStatus;
                    }
                }
                else {
                    printf("%s: FIFO Packet Mode setup failed. Error = %d\n", __func__, LastErrorStatus);
                    status = LastErrorStatus;
                }
            }
        }
        else if (PacketModeSetting == PACKET_MODE_ADDRESSABLE) {
            // Setup in Addressable Packet mode
            BufAlloc.AllocationMode = PacketModeSetting;
            BufAlloc.Length = 0;
            BufAlloc.MaxPacketSize = 0;
            BufAlloc.NumberDescriptors = NumberDescriptors;

            // Send Setup Packet Mode Addressable IOCTL
            if (!DeviceIoControl(hDevice, PACKET_BUF_ALLOC_IOCTL, &BufAlloc, sizeof(BUF_ALLOC_STRUCT), NULL, 0, &bytesReturned, &os)) {
                LastErrorStatus = GetLastError();
                if (LastErrorStatus == ERROR_IO_PENDING) {
                    // Wait here (forever) for the Overlapped I/O to complete
                    if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                        LastErrorStatus = GetLastError();
                        printf("%s: Addressable Packet Mode setup overlap failed. Error = %d\n", __func__, LastErrorStatus);
                        status = LastErrorStatus;
                    }
                }
                else {
                    printf("%s: Addressable Packet mode setup failed. Error = %d\n", __func__, LastErrorStatus);
                    status = LastErrorStatus;
                }
            }
        }
        else {
            status = STATUS_INVALID_MODE;
        }
    }
    else {
        printf("%s: No Packet Mode DMA Engines found\n", __func__);
        status = STATUS_INVALID_MODE;
    }
    CloseHandle(os.hEvent);
    return status;
}

/*! ReleasePacketBuffers
 *
 * \brief Sends two PACKET_BUF_DEALLOC_IOCTL calls to the driver to teardown the recieve buffer
 *  and teardown the descriptors for sending packets
 * \param EngineOffset - DMA Engine number offset to use
 * \return Completion status.
 */
UINT32 CDmaDriverDll::ReleasePacketBuffers(INT32 EngineOffset)
{
    BUF_DEALLOC_STRUCT BufDeAlloc;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_INVALID_MODE;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (os.hEvent == NULL) {
        return GetLastError();
    }

    if (EngineOffset < DmaInfo.PacketRecvEngineCount) {
        // Set the DMA Engine we want to de-allocate
        BufDeAlloc.EngineNum = DmaInfo.PacketRecvEngine[EngineOffset];
        // Set the allocation mode to what we used above
        BufDeAlloc.Reserved = 0;
        // Return the Buffer Address we recieved from the Allocate call
        BufDeAlloc.RxBufferAddress = pRxPacketBufferHandle;

        // Send Packet Mode Release
        if (!DeviceIoControl(hDevice, PACKET_BUF_RELEASE_IOCTL, &BufDeAlloc, sizeof(BUF_DEALLOC_STRUCT), NULL, 0, &bytesReturned, &os)) {
            LastErrorStatus = GetLastError();
            if (LastErrorStatus == ERROR_IO_PENDING) {
                // Wait here (forever) for the Overlapped I/O to complete
                if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                    LastErrorStatus = GetLastError();
                    status = LastErrorStatus;
                }
                else {
                    pRxPacketBufferHandle = NULL;
                    status = STATUS_SUCCESSFUL;
                }
            }
            else {
                printf("%s: Packet Rx buffer DeAllocate failed. Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }
        else {
            pRxPacketBufferHandle = NULL;
            status = STATUS_SUCCESSFUL;
        }
    }
    CloseHandle(os.hEvent);
    return status;
}

/*! ResetDMAEngine
 *
 * \brief Sends a RESET_DMA_ENGINE_IOCTL calls to the driver to reset the DMA Engine
 * \param EngineOffset
 * \param TypeDirection
 * \return Completion status.
 */
UINT32 CDmaDriverDll::ResetDMAEngine(INT32 EngineOffset, UINT32 TypeDirection)
{
    RESET_DMA_STRUCT ResetDMA;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_DMA_INVALID_ENGINE;

    // Set the DMA Engine we want to de-allocate
    ResetDMA.EngineNum = -1;

    // determine the ioctl code
    if ((TypeDirection & DMA_CAP_DIRECTION_MASK) == DMA_CAP_CARD_TO_SYSTEM) {
        if ((TypeDirection & DMA_CAP_ENGINE_TYPE_MASK) == DMA_CAP_PACKET_DMA) {
            if (EngineOffset < DmaInfo.PacketRecvEngineCount) {
                ResetDMA.EngineNum = DmaInfo.PacketRecvEngine[EngineOffset];
            }
        }
    }
    else                  // Direction is S2C
    {
        if ((TypeDirection & DMA_CAP_ENGINE_TYPE_MASK) == DMA_CAP_PACKET_DMA) {
            if (EngineOffset < DmaInfo.PacketSendEngineCount) {
                ResetDMA.EngineNum = DmaInfo.PacketSendEngine[EngineOffset];
            }
        }
    }

    if (ResetDMA.EngineNum != -1) {
        os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (os.hEvent == NULL) {
            return GetLastError();
        }
        // Send Reset command
        if (!DeviceIoControl(hDevice, RESET_DMA_ENGINE_IOCTL, &ResetDMA, sizeof(RESET_DMA_STRUCT), NULL, 0, &bytesReturned, &os)) {
            LastErrorStatus = GetLastError();
            if (LastErrorStatus == ERROR_IO_PENDING) {
                // Wait here (forever) for the Overlapped I/O to complete
                if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                    LastErrorStatus = GetLastError();
                    status = LastErrorStatus;
                }
                else {
                    status = STATUS_SUCCESSFUL;
                }
            }
            else {
                printf("%s: Reset DMA Engine failed. Error = %d\n", __func__, LastErrorStatus);
                status = LastErrorStatus;
            }
        }
        else {
            status = STATUS_SUCCESSFUL;
        }
        CloseHandle(os.hEvent);
    }
    else {
        printf("%s: Reset DMA called with bad Engine offset.\n", __func__);
    }
    return status;
}

//-------------------------------------------------------------------------
// User Interrupts
//-------------------------------------------------------------------------

/*! UserIRQWait
 *
 * \brief Waits for the given IRQMask bits interrupt or times out waiting.
 *  The dwIRQMask is given as the interrupt(s) to wait for.
 *  The dwIRQStatus is the returned value of the User Interrupt Control Register.
 *  the dwTimeoutMilliSec is the time out value in ms, 0 = no timeout.
 * \param dwTimeoutMilliSec
 * \note This call uses the BarNum and Offset used in the UserIRQEnable function
 * \return status.
 */
UINT32 CDmaDriverDll::UserIRQWait(DWORD dwTimeoutMilliSec       // Timeout in ms, 0 = no time out.
)
{
    USER_IRQ_WAIT_STRUCT UIRQWait;
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    UIRQWait.dwTimeoutMilliSec = dwTimeoutMilliSec;

    // Send User IRQ Abort message
    if (!DeviceIoControl(hDevice, USER_IRQ_WAIT_IOCTL, &UIRQWait, sizeof(UIRQWait), NULL, 0, &bytesReturned, &os)) {
        LastErrorStatus = GetLastError();
        if (LastErrorStatus == ERROR_IO_PENDING) {
            // Wait here (forever) for the Overlapped I/O to complete
            if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                printf("%s: UserIRQWait OverlappedResult call failed.\n", __func__);
                status = GetLastError();
            }
        }
        else {
            printf("%s: UserIRQWait IOCTL call failed. Error = 0x%x\n", __func__, LastErrorStatus);
            status = LastErrorStatus;
        }
    }
    CloseHandle(os.hEvent);
    return status;
}

/*! UserIRQCancel
 *
 * \brief Waits for the given IRQMask bits interrupt or times out waiting.
 * \return None
 * \note This call uses the BarNum and Offset used in the UserIRQEnable function
 */
UINT32 CDmaDriverDll::UserIRQCancel(VOID)
{
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // Send User IRQ Cancel message
    if (!DeviceIoControl(hDevice, USER_IRQ_CANCEL_IOCTL, NULL, 0, NULL, 0, &bytesReturned, &os)) {
        LastErrorStatus = GetLastError();
        if (LastErrorStatus == ERROR_IO_PENDING) {
            // Wait here (forever) for the Overlapped I/O to complete
            if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                printf("%s: UserIRQCancel OverlappedResult call failed.\n", __func__);
                status = GetLastError();
            }
        }
        else {
            printf("%s: UserIRQWait IOCTL call failed. Error = 0x%x\n", __func__, LastErrorStatus);
            status = LastErrorStatus;
        }
    }
    CloseHandle(os.hEvent);
    return status;
}

/*! UserIRQControl
 *
 * \brief Sets user interrupt control bits.
 * \param userIrqEnable - enable/disable user interrupts
 * \param userIrqMaskAfterIRQ - mask off user IRQs in the user IRQ interrupt handler
 * \return None
 */
UINT32 CDmaDriverDll::UserIRQControl(UINT32 userIrqEnable)
{
    OVERLAPPED os;          // OVERLAPPED structure for the operation
    DWORD bytesReturned = 0;
    DWORD LastErrorStatus = 0;
    UINT32 status = STATUS_SUCCESSFUL;
    USER_IRQ_CONTROL_STRUCT userIrqControl;

    userIrqControl.dwEnableUserIRQ = userIrqEnable;

    os.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // Send User IRQ Cancel message
    if (!DeviceIoControl(hDevice, USER_IRQ_CONTROL_IOCTL, &userIrqControl, sizeof(userIrqControl), NULL, 0, &bytesReturned, &os)) {
        LastErrorStatus = GetLastError();
        if (LastErrorStatus == ERROR_IO_PENDING) {
            // Wait here (forever) for the Overlapped I/O to complete
            if (!GetOverlappedResult(hDevice, &os, &bytesReturned, TRUE)) {
                printf("%s: OverlappedResult call failed.\n", __func__);
                status = GetLastError();
            }
        }
        else {
            printf("%s: IOCTL call failed. Error = 0x%x\n", __func__, LastErrorStatus);
            status = LastErrorStatus;
        }
    }
    CloseHandle(os.hEvent);
    return status;
}


