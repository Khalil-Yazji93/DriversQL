#include "pch.h"
#include "DmaDriverDll.h"       // DLL External functions, structures and declarations
#include "DmaDriverInt.h"       // DLL Internal functions, structures and declarations

// Local Prototypes
VOID InitializeDll();
VOID CleanupDll();

// Internal variables
CDmaDriverDll* DriverList[MAXIMUM_NUMBER_OF_BOARDS];

// Main DLL Entry Point
BOOLEAN APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(lpReserved);

    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        InitializeDll();
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        CleanupDll();
        break;
    }
    return TRUE;
}

/*! InitializeDll
 *
 * \brief This is called when the DLL is created.
 *  It initializes any global variables.
 */
VOID InitializeDll()
{
    for (int i = 0; i < MAXIMUM_NUMBER_OF_BOARDS; i++) {
        DriverList[i] = NULL;
    }
}

/*! CleanupDll
 *
 * \brief This is called when the DLL is cleaned up.
 * This detaches any ongoing connections and cleans up any global variables.
 */
VOID CleanupDll()
{
    for (int i = 0; i < MAXIMUM_NUMBER_OF_BOARDS; i++) {
        if (DriverList[i] != NULL) {
            // Make sure we are disconnected
            DisconnectFromBoard(i);
            // Cleanup the class
            delete DriverList[i];
            DriverList[i] = NULL;
        }
    }
}

//--------------------------------------------------------------------
//  Public DLL Calls - Main interface to the DLL
//--------------------------------------------------------------------

/*! ConnectToBoard
 *
 * \brief Connects to a board.  'board' is used to select between
 *  multiple board instances (more than 1 board may be present);
 *  If the board class has not been created, it does it now.
 * \param board
 * \param pDmaInfo
 * \return status
 */
PM40DRIVERDLL_API UINT32 ConnectToBoard(UINT32 board,    // Board to target
    PDMA_INFO_STRUCT pDmaInfo        // Pointer to Struct that stores DMA Info.

) {
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Create driver instance
    if (DriverList[board] == NULL) {
        DriverList[board] = new CDmaDriverDll(board);
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->ConnectToBoard(pDmaInfo);
    }
    else {
        printf("%s: CDmaDriverDll create failed.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! DisconnectFromBoard
 *
 * \brief Disconnect from a board.  'board' is used to select between
 *  multiple board instances (more than 1 board may be present);
 *  If the board class has not been created, return;
 *  otherwise, call DisconnectFromBoard.  Does not delete the board class.
 * \param board
 * \return STATUS_SUCCESS, STATUS_INVALID_BOARDNUM, STATUS_INCOMPLETE
 */
PM40DRIVERDLL_API UINT32 DisconnectFromBoard(UINT32 board        // Board to target
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->DisconnectFromBoard();
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}


PM40DRIVERDLL_API UINT32 GetDLLInfo(PDMA_INFO_STRUCT pDmaInfo
)
{
    pDmaInfo->DLLMajorVersion = VER_MAJOR_NUM;
    pDmaInfo->DLLMinorVersion = VER_MINOR_NUM;
    pDmaInfo->DLLSubMinorVersion = VER_SUBMINOR_NUM;
    pDmaInfo->DLLBuildNumberVersion = VER_BUILD_NUM;

    return 0;
}

/*! GetBoardCfg
//
// Get Board Configuration from the Driver
//
// The Driver auto discovers the board's resources during Driver
//   initialization (via hardware capabilities and configuration
//   register advertisements) and places the information in a
//   BOARD_CONFIG_STRUCT structure. BOARD_CONFIG_STRUCT provides
//   the necessary information about the board so that the
//   application knows what resources are available and how to
//   access them.
//
// GetBoardCfg gets a copy of the current BOARD_CONFIG_STRUCT
//   structure kept by the Driver.  No hardware accesses are
//   initiated by calling GetBoardCfg.
*/
PM40DRIVERDLL_API UINT32 GetBoardCfg(UINT32 board,       // Board to target
    PBOARD_CONFIG_STRUCT BoardCfg       /// Returned structure
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->GetBoardCfg(BoardCfg);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! GetDMAEngineCap
//
// \brief Gets the DMA Engine Capabilities of DMA Engine number DMAEngine
//   on board number 'board' and returns them in DMACap.
//
// DMA Engine Capabilities are defined as follows::
//   DmaCap[    0] : 1 == Engine Present; 0 == Engine not present
//   DmaCap[ 2: 1] : Direction
//                   00 == System to Card Engine (Write to Card)
//                   01 == Card to System Engine (Read from Card)
//                   10 == Reserved
//                   11 == Bidirectional (Write and Read)
//   DmaCap[    3] : Reserved
//   DmaCap[ 5: 4] : Engine Type/Programming Model
//                   00 - Block DMA
//                   01 - Packet DMA
//                   10 - Reserved
//                   11 - Reserved
//   DmaCap[ 7: 6] : Reserved
//   DmaCap[15: 8] : EngineNumber[7:0]
//                   Unique identifying number for this DMA Engine
//   DmaCap[23:16] : ImplCardAddresWidth[7:0]
//                   Size in bytes of the Card Address space that this
//                   DMA Engine is connected to == 2^ImplCardAddresWidth.
//                   ImplCardAddresWidth == 0 indicates that the DMA Engine
//                   is connected to a Stream/FIFO and does not implement
//                   addressable Card Memory
//   DmaCap[31:24] : Reserved
//
// \return DMA Capabilities in the DMACap sent to the call.
*/
PM40DRIVERDLL_API UINT32 GetDMAEngineCap(UINT32 board,   // Board number to target
    UINT32 DMAEngine,       // DMA Engine number to use
    PDMA_CAP_STRUCT DMACap  // Returned DMA Engine Capabilitie
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->GetDMAEngineCap(DMAEngine, DMACap);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! DoMem
//
// \brief Sends a DoMem IOCTL call to the driver.
//
// Uses the system CPU to perform a memory copy between
//   application's memory 'Buffer' starting at byte offset 'Offset' and
//   board 'board' Base Address Register (BAR) 'BarNum' starting at byte offset
//   'CardOffset'.  The copy operation length in bytes is specified by 'Length'.
//
// DoMem is primarily intended for control reads and writes.
//   DoMem is not useful for high bandwidth data transfers because
//   the system CPU uses very small burst sizes which results in
//   poor efficiency.
//
// The STAT_STRUCT is updated with the status information returned by the driver
// for this transaction.
//
// \return Processing status of the call.
 */
PM40DRIVERDLL_API UINT32 DoMem(UINT32 board,     // Board number to target
    UINT32 Rd_Wr_n,   // 1==Read, 0==Write
    UINT32 BarNum,    // Base Address Register (BAR) to access
    PUINT8 Buffer,    // Data buffer
    UINT64 Offset,    // Offset in data buffer to start transfer
    UINT64 CardOffset,        // Offset in BAR to start transfer
    UINT64 Length,    // Byte length of transfer
    PSTAT_STRUCT Status       // Completion Status
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->DoMem(Rd_Wr_n, BarNum, Buffer, Offset, CardOffset, Length, Status);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}


PM40DRIVERDLL_API UINT32 Bar0Write(UINT nAddrs, PUINT32 nValue)
{
    static int nArray[32];
    int	nBoard = 0;
    int nRdWr = 0;	//Write;
    int nBarNum = 0;
    UINT64 n64MemOffset = 0;
    UINT64 n64Length = 4;
    UINT64 n64CardAddrs = (UINT64)nAddrs;
    STAT_STRUCT Status;

    ZeroMemory((void*)&Status, sizeof Status);

    int nStatus = DoMem(nBoard, nRdWr, nBarNum, (UINT8*)nValue, n64MemOffset, n64CardAddrs, n64Length, &Status);

    if (nStatus) {
        printf("%s Failed to Write to BAR 0 nAddrs %x Value %x", __func__, nAddrs, nArray[0]);
        return STATUS_INCOMPLETE;
    }
    return 0;
}

PM40DRIVERDLL_API UINT32 Bar0Read(UINT nAddrs, PUINT32 nValue)
{
    static int nArray[32];
    int	nBoard = 0;
    int nRdWr = 1;	//Read;
    int nBarNum = 0;
    UINT8* pcArray = (UINT8*)nArray;
    UINT64 n64MemOffset = 0;
    UINT64 n64Length = 4;
    UINT64 n64CardAddrs = (UINT64)nAddrs;
    STAT_STRUCT Status;

    ZeroMemory((void*)&Status, sizeof Status);

    int nStatus = DoMem(nBoard, nRdWr, nBarNum, pcArray, n64MemOffset, n64CardAddrs, n64Length, &Status);

    if (nStatus) {
        printf("%s Failed to Read from BAR 0 nAddrs %x Value %x", __func__, nAddrs, nArray[0]);
        return STATUS_INCOMPLETE;
    }
    if (nValue != NULL) *nValue = nArray[0];
    return 0;
}


PM40DRIVERDLL_API UINT32 ReadADCData(UINT32 nStartAddress, UINT32 nNumberOFWordsToRead, UINT32* nWordsToRead)
{
    UINT64		CardOffset = (UINT64)nStartAddress;
    UINT32		TransferSize = nNumberOFWordsToRead * 4;
    UINT32		Mode = 0;
    UINT32		status;
    UINT64      UserInfo = 0;
    INT32       CardNum = 0;
    INT32       DMAEngineOffset = 0;
    UINT32      MaxPacketSize = 16384;


    status = SetupPacketMode(
        CardNum,
        DMAEngineOffset,
        (PUINT8)NULL,
        (PUINT32)NULL,
        NULL,
        PACKET_MODE_ADDRESSABLE,
        MaxPacketSize);

    status = PacketReadEx(
        CardNum,
        DMAEngineOffset,
        &UserInfo,
        CardOffset,
        Mode,
        (PUINT8)nWordsToRead,
        &TransferSize);

    status = ShutdownPacketMode(CardNum, DMAEngineOffset);

    return status;
}


/*! GetDmaPerf
 *
 * \brief Gets DMA Performance information from board 'board'
 *  and DMA Engine 'EngineNum'.
 *
 * The DMA Engine and Driver record performance information
 * when DMA operations are in process.  This information
 * is obtained by calling GetDmaPerf.
 *
 * \param board
 * \param EngineOffset
 * \param TypeDirection
 * \param Status
 * \returns The DMA performance status in the DMA_STAT_STRUCT sent to the call.
 */
PM40DRIVERDLL_API UINT32 GetDmaPerf(UINT32 board,        // Board number to target
    INT32 EngineOffset,  // DMA Engine number offset to use
    UINT32 TypeDirection,        // DMA Type & Direction Flags
    PDMA_STAT_STRUCT Status      // Returned performance metrics
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->GetDmaPerf(EngineOffset, TypeDirection, Status);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! WritePCIConfig
//
// \brief Sends a WRITE_PCI_CONFIG_IOCTL call to the driver.
//
// Copies to 'Offset' from the beginning of the PCI Configuration space for our 'board'
//  from application's memory 'Buffer' for the 'Length' specified.
//
// The STAT_STRUCT is updated with the status information returned by the driver
// for this transaction.
//
// \return Processing status of the call.
 */
PM40DRIVERDLL_API UINT32 WritePCIConfig(UINT32 board,    // Board number to target
    PUINT8 Buffer,   // Data buffer
    UINT32 Offset,   // Offset in PCI Config space to start transfer
    UINT32 Length,   // Byte length of transfer
    PSTAT_STRUCT Status      // Completion Status
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->WritePCIConfig(Buffer, Offset, Length, Status);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! ReadPCIConfig
//
// \brief Sends a READ_PCI_CONFIG_IOCTL call to the driver.
//
// Copies from 'Offset' from the beginning of the PCI Configuration space for our 'board'
//  to application's memory 'Buffer' for the 'Length' specified.
//
// The STAT_STRUCT is updated with the status information returned by the driver
// for this transaction.
//
// \return Processing status of the call.
 */
PM40DRIVERDLL_API UINT32 ReadPCIConfig(UINT32 board,     // Board number to target
    PUINT8 Buffer,    // Data buffer
    UINT32 Offset,    // Offset in PCI Config space to start transfer
    UINT32 Length,    // Byte length of transfer
    PSTAT_STRUCT Status       // Completion Status
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->ReadPCIConfig(Buffer, Offset, Length, Status);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

//--------------------------------------------------------------------
// FIFO Packet Mode Function calls
//--------------------------------------------------------------------

/*! PacketReceive
*
* \brief
* \param board
* \param EngineOffset
* \param BufferToken
* \param Buffer
* \param Length
* \return Status
*/
PM40DRIVERDLL_API UINT32 PacketReceive(UINT32 board,     // Board number to target
    INT32 EngineOffset,       // DMA Engine number offset to use
    PUINT32 BufferToken, PVOID Buffer, PUINT32 Length)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketReceiveEx(EngineOffset, NULL, BufferToken, Buffer, Length);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! PacketReceiveEx
 *
 * \brief
 * \param board
 * \param EngineOffset
 * \param UserStatus
 * \param BufferToken
 * \param Buffer
 * \param Length
 * \return Status
 */
PM40DRIVERDLL_API UINT32 PacketReceiveEx(UINT32 board,   // Board number to target
    INT32 EngineOffset,     // DMA Engine number offset to use
    PUINT64 UserStatus,     // User Status returned from the EOP DMA Descriptor
    PUINT32 BufferToken, PVOID Buffer, PUINT32 Length)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketReceiveEx(EngineOffset, UserStatus, BufferToken, Buffer, Length);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! PacketReceiveNB
*
* \brief Nonblocking PacketReceive Command.
* \note Will return immediately if no pending request exists in the queue.
* \param board
* \param EngineOffset
* \param UserStatus
* \param BufferToken
* \param Buffer
* \param Length
* \return DriverList[board]->PacketReceiveNB(EngineOffset, UserStatus,  BufferToken,  Buffer, Length);
*/
PM40DRIVERDLL_API UINT32 PacketReceiveNB(UINT32 board,   // Board number to target
    INT32 EngineOffset,     // DMA Engine number offset to use
    PUINT64 UserStatus,     // User Status returned from the EOP DMA Descriptor
    PUINT32 BufferToken, PVOID Buffer, PUINT32 Length)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketReceiveNB(EngineOffset, UserStatus, BufferToken, Buffer, Length);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! PacketReturnReceive
 *
 * \brief Returns a Recieve buffer token (and buffer ownership) back to the driver.
 *  Returns Processing status of the call.
 * \param board
 * \param EngineOffset
 * \param BufferToken
 * \return DriverList[board]->PacketReturnReceive(EngineOffset, BufferToken);
 */
PM40DRIVERDLL_API UINT32 PacketReturnReceive(UINT32 board,       // Board number to target
    INT32 EngineOffset, // DMA Engine number offset to use
    PUINT32 BufferToken // Token for the buffer to return
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketReturnReceive(EngineOffset, BufferToken);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

PM40DRIVERDLL_API UINT32 PacketReceives(UINT32 board,    // Board number to target
    INT32 EngineOffset,      // DMA Engine number offset to use
    PPACKET_RECVS_STRUCT pPacketRecvs)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketReceives(EngineOffset, pPacketRecvs);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

// PacketSend
//
// Send a packet containing 'buffer' for 'length' bytes

// Returns Processing status of the call.

PM40DRIVERDLL_API UINT32 PacketSend(UINT32 board,        // Board number to target
    INT32 EngineOffset,  // DMA Engine number offset to use
    UINT64 CardOffset, PUINT8 Buffer, UINT32 Length)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketSendEx(EngineOffset, 0, CardOffset, Buffer, Length);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! PacketSendEx
 *
 * \brief Send a packet containing 'buffer' for 'length' bytes
 * \param board
 * \param EngineOffset
 * \param UserControl
 * \param CardOffset
 * \param Buffer
 * \param Length
 * \return Status
 */
PM40DRIVERDLL_API UINT32 PacketSendEx(UINT32 board,      // Board number to target
    INT32 EngineOffset,        // DMA Engine number offset to use
    UINT64 UserControl, UINT64 CardOffset, PUINT8 Buffer, UINT32 Length)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketSendEx(EngineOffset, UserControl, CardOffset, Buffer, Length);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

//--------------------------------------------------------------------
// Addressable Packet Mode Function calls
//--------------------------------------------------------------------

/*! PacketRead
 *
 * \brief Read a packet containing 'buffer' for 'length' bytes
 * \param board
 * \param EngineOffset
 * \param UserStatus
 * \param CardOffset
 * \param Buffer
 * \param Length
 * \return Status
 */
PM40DRIVERDLL_API UINT32 PacketRead(UINT32 board,        // Board to target
    INT32 EngineOffset,  // DMA Engine number offset to use
    PUINT64 UserStatus,  // User Status returned from the EOP DMA Descriptor
    UINT64 CardOffset,   // Card Address to start read from
    PUINT8 Buffer,       // Address of data buffer
    PUINT32 Length       // Length to Read
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketReadEx(EngineOffset, UserStatus, CardOffset, 0, Buffer, Length);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! PacketWrite
 *
 * \brief Writes a packet containing 'buffer' for 'length' bytes
 * \param board
 * \param EngineOffset
 * \param UserControl
 * \param CardOffset
 * \param Buffer
 * \param Length
 * \return Status
 */
PM40DRIVERDLL_API UINT32 PacketWrite(UINT32 board,       // Board number to target
    INT32 EngineOffset, // DMA Engine number offset to use
    UINT64 UserControl, UINT64 CardOffset, PUINT8 Buffer, UINT32 Length)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketWriteEx(EngineOffset, UserControl, CardOffset, 0, Buffer, Length);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! PacketReadEx
 *
 * \brief Read a packet containing 'buffer' for 'length' bytes
 * \param board
 * \param EngineOffset
 * \param UserStatus
 * \param CardOffset
 * \param Mode
 * \param Buffer
 * \param Length
 * \return Status
 */
PM40DRIVERDLL_API UINT32 PacketReadEx(UINT32 board,      // Board to target
    INT32 EngineOffset,        // DMA Engine number offset to use
    PUINT64 UserStatus,        // User Status returned from the EOP DMA Descriptor
    UINT64 CardOffset, // Card Address to start read from
    UINT32 Mode,       // Control Mode Flags
    PUINT8 Buffer,     // Address of data buffer
    PUINT32 Length     // Length to Read
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketReadEx(EngineOffset, UserStatus, CardOffset, Mode, Buffer, Length);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! PacketWriteEx
 *
 * \brief Writes a packet containing 'buffer' for 'length' bytes
 * \param board
 * \param EngineOffset
 * \param UserControl
 * \param CardOffset
 * \param Mode
 * \param Buffer
 * \param Length
 * \return Status
 */
PM40DRIVERDLL_API UINT32 PacketWriteEx(UINT32 board,     // Board number to target
    INT32 EngineOffset,       // DMA Engine number offset to use
    UINT64 UserControl, UINT64 CardOffset, UINT32 Mode,       // Control Mode Flags
    PUINT8 Buffer, UINT32 Length)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->PacketWriteEx(EngineOffset, UserControl, CardOffset, Mode, Buffer, Length);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

//--------------------------------------------------------------------
// Common Packet Mode Function calls
//--------------------------------------------------------------------

/*! SetupPacketMode
 *
 * \brief Does the necessary setup for Packet mode. This includes allocating a
 *  receive packet buffer in the driver and initializing the descriptors and
 *  associated structures
 * \param board
 * \param EngineOffset
 * \param Buffer
 * \param BufferSize
 * \param MaxPacketSize
 * \param PacketMode
 * \param NumberDescriptors
 * \return Status
 */
PM40DRIVERDLL_API UINT32 SetupPacketMode(UINT32 board,   // Board number to target
    INT32 EngineOffset,     // DMA Engine number offset to use
    PUINT8 Buffer,  // Data buffer
    PUINT32 BufferSize,     // Total size of Recieve Buffer
    PUINT32 MaxPacketSize,  // Largest single packet size
    INT32 PacketMode,       // Sets mode, FIFO or Addressable Packet mode
    INT32 NumberDescriptors // Number of DMA Descriptors to allocate (Addressable mode)
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->SetupPacket(EngineOffset, Buffer, BufferSize, MaxPacketSize, PacketMode, NumberDescriptors);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! ShutdownPacketMode
 *
 * \brief Does the necessary shutdown of Packet mode. This includes freeing
 *  receive packet buffer in the driver
 *  Returns Processing status of the call.
 * \param board
 * \param EngineOffset
 * \return Status
 */
PM40DRIVERDLL_API UINT32 ShutdownPacketMode(UINT32 board,        // Board number to target
    INT32 EngineOffset   // DMA Engine number offset to use
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->ReleasePacketBuffers(EngineOffset);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! ResetDMAEngine
 *
 * \brief Resets te DMA Engine regardless of current state.
 * \note Use carefully.
 * \param board
 * \param EngineOffset
 * \param TypeDirection
 * \return Status
 */
PM40DRIVERDLL_API UINT32 ResetDMAEngine(UINT32 board,    // Board number to target
    INT32 EngineOffset,      // DMA Engine number offset to use
    UINT32 TypeDirection     // DMA Type & Direction Flags
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->ResetDMAEngine(EngineOffset, TypeDirection);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

// Windows User Interrupt / Timeout.
/*! UserIRQWait
 *
 * \brief Calls down to wait for a User Interrupt to occur or timeout waiting.
 * \param board
 * \param dwTimeoutMilliSec
 * \return status
 */
PM40DRIVERDLL_API UINT32 UserIRQWait(UINT32 board,       // Board number to target
    DWORD dwTimeoutMilliSec     // User Interrupt timeout value
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->UserIRQWait(dwTimeoutMilliSec);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! UserIRQCancel
 *
 * \brief Cancels a waiting User Interrupt request.
 * \param board
 * \return status
 */
PM40DRIVERDLL_API UINT32 UserIRQCancel(UINT32 board      // Board number to target
)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->UserIRQCancel();
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

/*! UserIRQControl
 *
 * \brief Controls user interrupt settings
 * \param board
 * \param enable - enable/disable user interrupts
 * \param mask - mask off user interrupts in the user interrupt handler
 * \return status
 */
PM40DRIVERDLL_API UINT32 UserIRQControl(UINT32 board, UINT32 enable)
{
    if (board >= MAXIMUM_NUMBER_OF_BOARDS) {
        return STATUS_INVALID_BOARDNUM;
    }
    // Connect to board
    if (DriverList[board] != NULL) {
        return DriverList[board]->UserIRQControl(enable);
    }
    else {
        printf("%s: No driver class instance.\n", __func__);
        return STATUS_INCOMPLETE;
    }
}

