#pragma once

class CDmaDriverDll {
    UINT32 _PacketReceive(INT32 EngineOffset, PUINT64 UserStatus, PUINT32 BufferToken, PVOID Buffer, PUINT32 Length, BOOLEAN Blocking);
public:
    CDmaDriverDll(VOID);
    CDmaDriverDll(UINT32 BoardNum);
    ~CDmaDriverDll(VOID);

    UINT32 ConnectToBoard(PDMA_INFO_STRUCT pDmaInfo);

    UINT32 DisconnectFromBoard();

    UINT32 GetBoardCfg(PBOARD_CONFIG_STRUCT Board);

    UINT32 GetDMAEngineCap(INT32 EngineNum, PDMA_CAP_STRUCT DMACap);

    UINT32 DoMem(UINT32 Rd_Wr_n, UINT32 BarNum, PUINT8 Buffer, UINT64 Offset, UINT64 CardOffset, UINT64 Length, PSTAT_STRUCT Status);

    UINT32 WritePCIConfig(PUINT8 Buffer, UINT32 Offset, UINT32 Length, PSTAT_STRUCT Status);

    UINT32 ReadPCIConfig(PUINT8 Buffer, UINT32 Offset, UINT32 Length, PSTAT_STRUCT Status);

    UINT32 GetDmaPerf(INT32 EngineNum, UINT32 TypeDirection, PDMA_STAT_STRUCT Status);

    UINT32 PacketReceiveEx(INT32 EngineOffset, PUINT64 UserStatus, PUINT32 BufferToken, PVOID Buffer, PUINT32 Length);

    UINT32 PacketReturnReceive(INT32 EngineOffset, PUINT32 BufferToken);

    UINT32 PacketReceives(INT32 EngineOffset, PPACKET_RECVS_STRUCT pPacketRecvs);

    UINT32 PacketSendEx(INT32 EngineOffset, UINT64 UserControl, UINT64 CardOffset, PUINT8 Buffer, UINT32 Length);

    UINT32 PacketReceiveNB(INT32 EngineOffset, PUINT64 UserStatus, PUINT32 BufferToken, PVOID Buffer, PUINT32 Length);

    UINT32 PacketWriteEx(INT32 EngineOffset, UINT64 UserControl, UINT64 CardOffset, UINT32 Mode, PUINT8 Buffer, UINT32 Length);

    UINT32 PacketReadEx(INT32 EngineOffset, PUINT64 UserStatus, UINT64 CardOffset, UINT32 Mode, PUINT8 Buffer, PUINT32 Length);

    UINT32 SetupPacket(INT32 EngineOffset, PUINT8 Buffer, PUINT32 BufferSize, PUINT32 MaxPacketSize, INT32 PacketModeSetting, INT32 NumberDescriptors);

    UINT32 ReleasePacketBuffers(INT32 EngineOffset);

    UINT32 ResetDMAEngine(INT32 EngineOffset, UINT32 TypeDirection);

    UINT32 UserIRQWait(DWORD dwTimeoutMilliSec);

    UINT32 UserIRQCancel(VOID);

    UINT32 UserIRQControl(UINT32 userIrqEnable);

private:
    UINT32 BufferSize;
    UINT32 MaxPacketSize;
    UINT32 BoardNumber;
    BOOLEAN AttachedToDriver;
    HANDLE hDevice;
    HDEVINFO hDevInfo;
    DMA_INFO_STRUCT DmaInfo;
    PSP_DEVICE_INTERFACE_DETAIL_DATA pDeviceInterfaceDetailData;
    UINT64 pRxPacketBufferHandle;
};
