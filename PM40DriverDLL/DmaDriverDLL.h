#pragma once

#ifdef WIN32

#include "Setupapi.h"
#include "basetsd.h"
#else
#include "../../../Include/StdTypes.h"
#endif                          // Windows vs. Linux

#include "version.h"
#include "DmaDriverIoctl.h"


#ifndef _DMADRIVERDLL_H
#define _DMADRIVERDLL_H

#ifdef WIN32

#pragma warning(disable:4458)

// Maximum number of boards
#define MAXIMUM_NUMBER_OF_BOARDS   4

/*! \note
 * The following ifdef block is the standard way of creating macros which make exporting
 * from a DLL simpler. All files within this DLL are compiled with the DMADRIVERDLL_EXPORTS
 * symbol defined on the command line. this symbol should not be defined on any project
 * that uses this DLL. This way any other project whose source files include this file see
 * PM40DRIVERDLL_API functions as being imported from a DLL, whereas this DLL sees symbols
 * defined with this macro as being exported.
 */
#ifdef PM40DRIVERDLL_EXPORTS
#define PM40DRIVERDLL_API extern "C" __declspec(dllexport)
#else
#define PM40DRIVERDLL_API __declspec(dllimport)
#endif

#else
// Maximum number of boards
#define MAXIMUM_NUMBER_OF_BOARDS   4
#define PM40DRIVERDLL_API

#endif                          // Windows vs. Linux

/*!
 * \struct DMA_INFO_STRUCT
 * \brief DMA Info Structure - Contains information on the type and
 *  quantity of DMA Engines
 */
typedef struct _DMA_INFO_STRUCT {
    INT8 PacketSendEngineCount;
    INT8 PacketRecvEngineCount;
    INT8 PacketSendEngine[MAX_NUM_DMA_ENGINES];
    INT8 PacketRecvEngine[MAX_NUM_DMA_ENGINES];
    INT16 DLLMajorVersion;
    INT16 DLLMinorVersion;
    INT16 DLLSubMinorVersion;
    INT16 DLLBuildNumberVersion;
    INT8 AddressablePacketMode;
    INT8 DMARegistersBAR;
} DMA_INFO_STRUCT, * PDMA_INFO_STRUCT;

#define S2C_DIRECTION       TRUE
#define C2S_DIRECTION       FALSE

/*! \struct STAT_STRUCT
 *
 * \brief Status Structure
 *  Status Information from an IOCTL transaction
 */
typedef struct _STAT_STRUCT {
    UINT64 CompletedByteCount;      // Number of bytes transfered
} STAT_STRUCT, * PSTAT_STRUCT;

//--------------------------------------------------------------------
// Externally Visible API
//--------------------------------------------------------------------

/*! ConnectToBoard
 *
 * \brief This connects to the board you are interested in talking to.
 * \param pDmaInfo
 * \return status
 */
PM40DRIVERDLL_API UINT32 ConnectToBoard(UINT32 board,    // Board to target
    PDMA_INFO_STRUCT pDmaInfo);

/*! DisconnectFromBoard
 *
 * \brief Disconnects from a board.
 *  Cleanup any global data structures that have been created.
 *  Does not delete the board class.
 * \return STATUS_SUCCESSFUL
 */
PM40DRIVERDLL_API UINT32 DisconnectFromBoard(UINT32 board        // Board to target
);


PM40DRIVERDLL_API UINT32 GetDLLInfo(PDMA_INFO_STRUCT pDmaInfo
);

/*! GetBoardCfg
 *
 * \brief Retrieves Board Configuration from the Driver
 *  The Driver automatically discovers the board's resources during Driver
 *  initialization (via hardware capabilities and configuration
 *  register advertisements) and places the information in a
 *  BOARD_CONFIG_STRUCT structure. BOARD_CONFIG_STRUCT provides
 *  the necessary information about the board so that the
 *  application knows what resources are available and how to
 *  access them.
 * \param board
 * \param Board
 * \return Status
 * \note GetBoardCfg gets a copy of the current BOARD_CONFIG_STRUCT
 *  structure kept by the Driver.  No hardware accesses are
 *  initiated by calling GetBoardCfg.
 */

PM40DRIVERDLL_API UINT32 GetBoardCfg(UINT32 board,       // Board to target
    PBOARD_CONFIG_STRUCT Board  // Returned structure
);

/*! GetDMAEngineCap
*
* \brief
* Gets the DMA Engine Capabilities of DMA Engine number DMAEngine
* on board number 'board' and returns them in DMACap.
* \note
*   DMA Engine Capabilities are defined as follows::
*       DmaCap[    0] : 1 == Engine Present; 0 == Engine not present
*       DmaCap[ 2: 1] : Direction
*                   00 == System to Card Engine (Write to Card)
*                   01 == Card to System Engine (Read from Card)
*                   10 == Reserved
*                   11 == Bidirectional (Write and Read)
*       DmaCap[    3] : Reserved
*       DmaCap[ 5: 4] : Engine Type/Programming Model
*                   00 - Block DMA
*                   01 - Packet DMA
*                   10 - Reserved
*                   11 - Reserved
*       DmaCap[ 7: 6] : Reserved
*       DmaCap[15: 8] : EngineNumber[7:0]
*                   Unique identifying number for this DMA Engine
*       DmaCap[23:16] : ImplCardAddresWidth[7:0]
*                   Size in bytes of the Card Address space that this
*                   DMA Engine is connected to == 2^ImplCardAddresWidth.
*                   ImplCardAddresWidth == 0 indicates that the DMA Engine
*                   is connected to a Stream/FIFO and does not implement
*                   addressable Card Memory
*       DmaCap[31:24] : Reserved
*
* \param board
* \param DMAEngine
* \param DMACap
* \return DriverList[board]->GetDMAEngineCap(DMAEngine, DMACap).
*/
PM40DRIVERDLL_API UINT32 GetDMAEngineCap(UINT32 board,   // Board number to target
    UINT32 DMAEngine,       // DMA Engine number to use
    PDMA_CAP_STRUCT DMACap  // Returned DMA Engine Capabilities
);

/*! GetDmaPerf
*
* \brief Gets DMA Performance information from board 'board'
* and DMA Engine 'EngineNum'.
* \note
* The DMA Engine and Driver record performance information
* when DMA operations are in process.  This information
* is obtained by calling GetDmaPerf.
* \param board
* \param EngineOffset
* \param TypeDirection
* \param Status
* \return DriverList[board]->GetDmaPerf(EngineOffset, TypeDirection, Status);
*/
PM40DRIVERDLL_API UINT32 GetDmaPerf(UINT32 board,        // Board number to target
    INT32 EngineOffset,  // DMA Engine number offset to use
    UINT32 TypeDirection,        // DMA Type (Block / Packet) & Direction Flags
    PDMA_STAT_STRUCT Status      // Returned performance metrics
);

/*! WritePCIConfig
*
* \brief Sends a WRITE_PCI_CONFIG_IOCTL call to the driver.
* Copies to 'Offset' from the beginning of the PCI Configuration space for our 'board'
* from application's memory 'Buffer' for the 'Length' specified.
* The STAT_STRUCT is updated with the status information returned by the driver
* for this transaction.
* \param board
* \param Buffer
* \param Offset
* \param Length
* \param Status
* \return DriverList[board]->WritePCIConfig(Buffer, Offset, Length, Status);
*/
PM40DRIVERDLL_API UINT32 WritePCIConfig(UINT32 board,    // Board number to target
    PUINT8 Buffer,   // Data buffer
    UINT32 Offset,   // Offset in PCI Config space to start transfer
    UINT32 Length,   // Byte length of transfer
    PSTAT_STRUCT Status      // Completion Status
);

/*! ReadPCIConfig
*
* \brief Sends a READ_PCI_CONFIG_IOCTL call to the driver.
* Copies from 'Offset' from the beginning of the PCI Configuration space for our 'board'
* to application's memory 'Buffer' for the 'Length' specified.
* \note
* The STAT_STRUCT is updated with the status information returned by the driver
* for this transaction.
* \param board
* \param Buffer
* \param Offset
* \param Length
* \param Status
* \return DriverList[board]->ReadPCIConfig(Buffer, Offset, Length, Status)
*/
PM40DRIVERDLL_API UINT32 ReadPCIConfig(UINT32 board,     // Board number to target
    PUINT8 Buffer,    // Data buffer
    UINT32 Offset,    // Offset in PCI Config space to start transfer
    UINT32 Length,    // Byte length of transfer
    PSTAT_STRUCT Status       // Completion Status
);

/*! DoMem
*
* \brief Sends a DoMem IOCTL call to the driver.
*
* \note
*  Uses the system CPU to perform a memory copy between
*  application's memory 'Buffer' starting at byte offset 'Offset' and
*  board 'board' Base Address Register (BAR) 'BarNum' starting at byte offset
*  'CardOffset'.  The copy operation length in bytes is specified by 'Length'.
*
*  DoMem is primarily intended for control reads and writes.
*  DoMem is not useful for high bandwidth data transfers because
*  the system CPU uses very small burst sizes which results in
*  poor efficiency.
*
*  The STAT_STRUCT is updated with the status information returned by the driver
*  for this transaction.
* \param board
* \param Rd_Wr_n
* \param BarNum
* \param Buffer
* \param Offset
* \param CardOffset
* \param Length
* \param Status
* \return DriverList[board]->DoMem(Rd_Wr_n, BarNum, Buffer, Offset, CardOffset, Length, Status);
*/
PM40DRIVERDLL_API UINT32 DoMem(UINT32 board,     // Board number to target
    UINT32 Rd_Wr_n,   // 1==Read, 0==Write
    UINT32 BarNum,    // Base Address Register (BAR) to access
    PUINT8 Buffer,    // Data buffer
    UINT64 Offset,    // Offset in data buffer to start transfer
    UINT64 CardOffset,        // Offset in BAR to start transfer
    UINT64 Length,    // Byte length of transfer
    PSTAT_STRUCT Status       // Completion Status
);


PM40DRIVERDLL_API UINT32 Bar0Write(UINT nAddrs,
    PUINT32 nValue
);

PM40DRIVERDLL_API UINT32 Bar0Read(UINT32 nAddrs,
    PUINT32 nValue
);

PM40DRIVERDLL_API UINT32 ReadADCData(UINT32 nStartAddress,
    UINT32 nNumberOFWordsToRead,
    UINT* WordsToRead
);

//--------------------------------------------------------------------
// FIFO Packet Mode Function calls
//--------------------------------------------------------------------

/*! PacketReceive
*
* \brief FIFO Packet-Mode only.
* \param board
* \param EngineOffset
* \param BufferToken
* \param Buffer
* \param Length
* \return DriverList[board]->PacketReceiveEx(EngineOffset, NULL,  BufferToken,  Buffer, Length);
*/
PM40DRIVERDLL_API UINT32 PacketReceive(UINT32 board,     // Board number to target
    INT32 EngineOffset,       // DMA Engine number offset to use
    PUINT32 BufferToken,      // Token for the returned buffer
    PVOID Buffer,     // Pointer to the recived packet buffer
    PUINT32 Length    // Length of the received packet
);

/*! PacketReceiveEx
*
* \brief PacketReceiveEx
* \param board
* \param EngineOffset
* \param UserStatus
* \param BufferToken
* \param Buffer
* \param Length
* \return DriverList[board]->PacketReceiveEx(EngineOffset, UserStatus,  BufferToken,  Buffer, Length);
*/
PM40DRIVERDLL_API UINT32 PacketReceiveEx(UINT32 board,   // Board to target
    INT32 EngineNum,        // DMA Engine number offset to use
    PUINT64 UserStatus,     // User Status returned from the EOP DMA Descriptor
    PUINT32 BufferToken,    // Token for the returned buffer
    PVOID Buffer,   // Pointer to the recived packet buffer
    PUINT32 Length  // Length of the received packet
);

/*! PacketReceiveNB
*
* \brief PacketReceiveNB - Non-Blocking version of PacketReceiveEx
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
    PUINT32 BufferToken, PVOID Buffer, PUINT32 Length);

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
);

/*! PacketReceives
*
* \brief Multiple Packet receive function for use with FIFO Packet DMA only:
* \param board
* \param EngineOffset
* \param pPacketRecvs
* \return DriverList[board]->PacketReceives(EngineOffset, pPacketRecvs);
*/
PM40DRIVERDLL_API UINT32 PacketReceives(UINT32 board,    // Board number to target
    INT32 EngineNum, // DMA Engine number offset to use
    PPACKET_RECVS_STRUCT pPacketRecvs        // Pointer to Packet Receives struct
);

/*! PacketSend
*
* \brief Send a packet containing 'buffer' for 'length' bytes
*  Returns Processing status of the call. Use for FIFO Packet DMA only:
* \param board
* \param EngineOffset
* \param CardOffset
* \param Buffer
* \param Length
* \return DriverList[board]->PacketSendEx(EngineOffset, 0, CardOffset, Buffer, Length);
*/
PM40DRIVERDLL_API UINT32 PacketSend(UINT32 board,        // Board number to target
    INT32 EngineOffset,  // DMA Engine number offset to use
    UINT64 CardOffset,   // Address of Memory on the card
    PUINT8 Buffer,       // Pointer to the packet buffer to send
    UINT32 Length        // Length of the send packet
);

/* PacketSendEx
*
* \brief Send a packet containing 'buffer' for 'length' bytes
*  Returns Processing status of the call. Extended Functionality - User Control
* \param board
* \param EngineOffset
* \param UserControl
* \param CardOffset
* \param Buffer
* \param Length
* \return DriverList[board]->PacketSendEx(EngineOffset, UserControl, CardOffset, Buffer, Length);
*/
PM40DRIVERDLL_API UINT32 PacketSendEx(UINT32 board,      // Board to target
    INT32 EngineOffset,        // DMA Engine number offset to use
    UINT64 UserControl,        // User Control to set in the first DMA Descriptor
    UINT64 CardOffset, // Address of Memory on the card
    PUINT8 Buffer,     // Pointer to the packet buffer to send
    UINT32 Length      // Length of the send packet
);

//**************************************************
// Addressable Packet Mode Function calls
//**************************************************

/*! PacketRead
*
* \brief Read a packet containing 'buffer' for 'length' bytes
*  Returns Processing status of the call.
* \param board
* \param EngineOffset
* \param UserStatus
* \param CardOffset
* \param Buffer
* \param Length
* \return DriverList[board]->PacketReadEx(EngineOffset, UserStatus, CardOffset, 0, Buffer, Length);
*/
PM40DRIVERDLL_API UINT32 PacketRead(UINT32 board,        // Board to target
    INT32 EngineOffset,  // DMA Engine number offset to use
    PUINT64 UserStatus,  // User Status returned from the EOP DMA Descriptor
    UINT64 CardOffset,   // Card Address to start read from
    PUINT8 Buffer,       // Address of data buffer
    PUINT32 Length       // Length to Read
);

/*! PacketWrite
*
* \brief Writes a packet containing 'buffer' for 'length' bytes
*  Returns Processing status of the call.
* \param board
* \param EngineOffset
* \param UserControl
* \param CardOffset
* \param Buffer
* \param Length
* \return DriverList[board]->PacketWriteEx(EngineOffset, UserControl, CardOffset, 0, Buffer, Length);
*/
PM40DRIVERDLL_API UINT32 PacketWrite(UINT32 board,       // Board to target
    INT32 EngineOffset, // DMA Engine number offset to use
    UINT64 UserControl, // User Control to set in the first DMA Descriptor
    UINT64 CardOffset,  // Card Address to start write to
    PUINT8 Buffer,      // Address of data buffer
    UINT32 Length       // Length of data packet
);

/*! PacketReadEx
*
* \brief Read a packet containing 'buffer' for 'length' bytes
*  Returns Processing status of the call.
* \param board
* \param EngineOffset
* \param UserStatus
* \param CardOffset
* \param Mode
* \param Buffer
* \param Length
* \return DriverList[board]->PacketReadEx(EngineOffset, UserStatus, CardOffset, Mode, Buffer, Length);
*/
PM40DRIVERDLL_API UINT32 PacketReadEx(UINT32 board,      // Board to target
    INT32 EngineOffset,        // DMA Engine number offset to use
    PUINT64 UserStatus,        // User Status returned from the EOP DMA Descriptor
    UINT64 CardOffset, // Card Address to start read from
    UINT32 Mode,       // Control Mode Flags
    PUINT8 Buffer,     // Address of data buffer
    PUINT32 Length     // Length to Read
);

/*! PacketWriteEx
*
* \brief Writes a packet containing 'buffer' for 'length' bytes
* Returns Processing status of the call.
* \param board
* \param EngineOffset
* \param UserControl
* \param CardOffset
* \param Mode
* \param Buffer
* \param Length
* \return DriverList[board]->PacketWriteEx(EngineOffset, UserControl, CardOffset, Mode, Buffer, Length);
*/
PM40DRIVERDLL_API UINT32 PacketWriteEx(UINT32 board,     // Board to target
    INT32 EngineOffset,       // DMA Engine number offset to use
    UINT64 UserControl,       // User Control to set in the first DMA Descriptor
    UINT64 CardOffset,        // Card Address to start write to
    UINT32 Mode,      // Control Mode Flags
    PUINT8 Buffer,    // Address of data buffer
    UINT32 Length     // Length of data packet
);

//**************************************************
// Common Packet Mode Function calls
//**************************************************

/* SetupPacketMode
*
* \brief Does the necessary setup for Packet mode. This includes allocating a
*  receive packet buffer in the driver and initializing the descriptors and
*  associated structures and returns Processing status of the call.
* \param board
* \param EngineOffset
* \param Buffer
* \param BufferSize
* \param MaxPacketSize
* \param PacketMode
* \param NumberDescriptors
* \return DriverList[board]->SetupPacket(EngineOffset, Buffer, BufferSize,
                            MaxPacketSize, PacketMode, NumberDescriptors);
*/
PM40DRIVERDLL_API UINT32 SetupPacketMode(UINT32 board,   // Board number to target
    INT32 EngineOffset,     // DMA Engine number offset to use
    PUINT8 Buffer,  // Data buffer (FIFO Mode)
    PUINT32 BufferSize,     // Size of the Packet Recieve Data Buffer requested (FIFO Mode)
    PUINT32 MaxPacketSize,  // Length of largest packet (FIFO Mode)
    INT32 PacketMode,       // Sets mode, FIFO or Addressable Packet mode
    INT32 NumberDescriptors // Number of DMA Descriptors to allocate (Addressable mode)
);

/*! ShutdownPacketMode
*
* \brief Does the necessary shutdown of Packet mode. This includes freeing
*  receive packet buffer in the driver
*  Returns Processing status of the call.
* \param board
* \param EngineOffset
* \return DriverList[board]->ReleasePacketBuffers(EngineOffset);
*/
PM40DRIVERDLL_API UINT32 ShutdownPacketMode(UINT32 board,        // Board number to target
    INT32 EngineOffset   // DMA Engine number offset to use
);

/*! ResetDMAEngine
*
* \brief Resets te DMA Engine regardless of current state.
*  Returns Processing status of the call.
* \note Use carefully.
* \param board
* \param EngineOffset
* \param TypeDirection
* \return DriverList[board]->ResetDMAEngine(EngineOffset, TypeDirection);
*/
PM40DRIVERDLL_API UINT32 ResetDMAEngine(UINT32 board,    // Board number to target
    INT32 EngineOffset,      // DMA Engine number offset to use
    UINT32 TypeDirection     // DMA Type & Direction Flags
);

//PM40DRIVERDLL_API UINT32
 //setupUserInterruptSignal(
     //UINT32          board       // Board to target
 //);
PM40DRIVERDLL_API UINT32 UserIRQWait(UINT32 board,       // Board number to target
    DWORD TimeoutMilliSec       // Timeout if no User interrupts occur.
);

PM40DRIVERDLL_API UINT32 UserIRQCancel(UINT32 board      // Board number to target
);

PM40DRIVERDLL_API UINT32 UserIRQControl(UINT32 board, UINT32 userIrqEnable);

#endif                          // _DMADRIVERDLL_H
