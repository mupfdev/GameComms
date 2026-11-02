#!/usr/bin/env python3
"""Single source of truth for the GAMECOMMS.DLL export tables.

Every original variant is described ordinal by ordinal.  The table is
derived from the reverse engineering in docs/REVERSE_ENGINEERING.md:

  * function identity: behaviour of the exported code (Ghidra + objdump),
    cross-checked with the alphabetical order of GCC 2.9 mangled names
    that the original .def files follow (ordinal 1 aside);
  * cross-variant identity: tools/re/xmatch.py shows that ordinals 1..55
    are the same functions in v2..v11 and that v3..v11 only append.

Usage:
    variants.py defs  OUTDIR     write gamecomms_vN.def for every variant
    variants.py table            print the compatibility table (Markdown)
    variants.py check DLLDIR     compare export counts with original DLLs
"""
import os
import sys

# (symbol, prototype, confidence)  confidence: V = verified from code,
# S = verified behaviour + name from sort order, I = inferred
RSGE = {
    'ctor': ('rsge_ctor', 'RSGEDebugLog::RSGEDebugLog()', 'V'),
    'dtor': ('rsge_dtor', 'RSGEDebugLog::~RSGEDebugLog()', 'V'),
    'Close': ('rsge_void', 'void RSGEDebugLog::Close()', 'V'),
    'Open': ('rsge_ret0', 'TInt RSGEDebugLog::Open()', 'S'),
    'NewLine': ('rsge_ret0', 'TInt RSGEDebugLog::NewLine(TLogLevel)', 'S'),
    'SetLoggingLevel': ('rsge_SetLoggingLevel', 'void RSGEDebugLog::SetLoggingLevel(TLogLevel)', 'V'),
    'Write': ('rsge_ret0', 'TInt RSGEDebugLog::Write*/WriteLine*(...)  (no-op in all shipped builds)', 'V'),
}

V1 = {
    1: RSGE['ctor'],
    2: ('gc1_ConnectState', 'TConnectState CGameBTComms::ConnectState()', 'V'),
    3: ('gc1_ConnectionRole', 'TConnectionRole CGameBTComms::ConnectionRole()', 'V'),
    4: ('gc1_ContinueMultiPlayerGame', 'TInt CGameBTComms::ContinueMultiPlayerGame()', 'S'),
    5: ('gc1_DisconnectClient', 'TInt CGameBTComms::DisconnectClient(TUint16)', 'V'),
    6: ('gc1_Disconnect', 'void CGameBTComms::Disconnect()', 'V'),
    7: ('gc1_EndMultiPlayerGame', 'TInt CGameBTComms::EndMultiPlayerGame()', 'S'),
    8: ('gc1_GameState', 'TGameState CGameBTComms::GameState()', 'V'),
    9: ('gc1_GetLocalDeviceName', 'TInt CGameBTComms::GetLocalDeviceName(THostName&)', 'V'),
    10: ('gc1_IsShowingDeviceSelectDlg', 'TBool CGameBTComms::IsShowingDeviceSelectDlg()', 'S'),
    11: ('gc1_NewL', 'static CGameBTComms* CGameBTComms::NewL(MGameBTCommsNotify*, TUint32, RSGEDebugLog*)', 'V'),
    12: RSGE['NewLine'],
    13: RSGE['Open'],
    14: ('gc1_PauseMultiPlayerGame', 'TInt CGameBTComms::PauseMultiPlayerGame()', 'S'),
    15: ('gc1_ReconnectL', 'TInt CGameBTComms::ReconnectL(TBool)', 'S'),
    16: ('gc1_SendDataToAllClients', 'TInt CGameBTComms::SendDataToAllClients(TDesC8&)', 'V'),
    17: ('gc1_SendDataToClient', 'TInt CGameBTComms::SendDataToClient(TUint16, TDesC8&)', 'V'),
    18: ('gc1_SendDataToHost', 'TInt CGameBTComms::SendDataToHost(TDesC8&)', 'V'),
    19: RSGE['SetLoggingLevel'],
    20: ('gc1_StartClientL', 'void CGameBTComms::StartClientL()', 'V'),
    21: ('gc1_StartHostL', 'void CGameBTComms::StartHostL(TUint16, TUint16)', 'V'),
    **{o: RSGE['Write'] for o in range(22, 31)},
    31: ('gc1_dtor', 'CGameBTComms::~CGameBTComms()', 'V'),
    32: RSGE['dtor'],
    33: RSGE['Close'],
}

V2 = {
    1: RSGE['ctor'],
    2: ('gc2_BluetoothPowerState', 'TInt CGameBTComms::BluetoothPowerState(TBool&)', 'V'),
    3: ('gcx_Obj_Ret0', 'TInt CGameBTDiscoverer::Cancel()  [internal helper]', 'V'),
    4: ('gcx_List_Reset', 'void CGameBTDeviceList::Reset()  [internal helper]', 'V'),
    5: RSGE['Close'],
    6: ('gc2_ConnectState', 'TConnectState CGameBTComms::ConnectState()', 'V'),
    7: ('gc2_Connect', 'TInt CGameBTComms::Connect()', 'V'),
    8: ('gc2_ConnectClient', 'TInt CGameBTComms::Connect(TUint16)  [host only]', 'V'),
    9: ('gc2_ConnectionRole', 'TConnectionRole CGameBTComms::ConnectionRole()', 'V'),
    10: ('gcx_List_Count', 'TInt CGameBTDeviceList::Count()  [internal helper]', 'V'),
    11: ('gc2_DeclareRemoteDevice', 'TInt CGameBTComms::DeclareRemoteDevice(const TDeviceDetails&)', 'V'),
    12: ('gcx_List_Delete', 'void CGameBTDeviceList::Delete(TInt)  [internal helper]', 'V'),
    13: ('gcx_Obj_Member', 'T& CGameBTDiscoverer::Devices()  [internal helper]', 'V'),
    14: ('gc2_Disconnect', 'void CGameBTComms::Disconnect()', 'V'),
    15: ('gc2_DisconnectClient', 'TInt CGameBTComms::Disconnect(TUint16, TBool)  [host only]', 'V'),
    16: ('gcx_Obj_StartWith', 'TInt CGameBTResolver::Start(const TDeviceDetails&)  [internal helper]', 'V'),
    17: ('gcx_List_CurrentItem', 'TInt CGameBTDeviceList::CurrentItem()  [internal helper]', 'V'),
    18: ('gcx_List_ListBox', 'CEikListBox* CGameBTDeviceList::ListBox()  [internal helper]', 'V'),
    19: ('gcx_List_At', 'TDeviceDetails& CGameBTDeviceList::At(TInt)  [internal helper]', 'V'),
    20: ('gc2_GetLocalDeviceName', 'TInt CGameBTComms::GetLocalDeviceName(THostName&)', 'V'),
    21: ('gcx_List_Hide', 'void CGameBTDeviceList::Hide()  [internal helper]', 'V'),
    22: ('gc2_HostAcceptMode', 'TAcceptMode CGameBTComms::HostAcceptMode()  [host only]', 'V'),
    23: ('gcx_List_InsertL', 'void CGameBTDeviceList::InsertL(TInt, const TDeviceDetails&)  [internal helper]', 'V'),
    24: ('gcx_Obj_RetTrue', 'TBool CGameBTDiscoverer::IsIdle()  [internal helper]', 'V'),
    25: ('gcx_Obj_NewL', 'static CGameBTResolver* CGameBTResolver::NewL(M*, RSGEDebugLog*)  [internal helper]', 'V'),
    26: ('gcx_List_NewL', 'static CGameBTDeviceList* CGameBTDeviceList::NewL(const TRect&, M*, TBool, RSGEDebugLog*)  [internal helper]', 'V'),
    27: ('gc2_NewL', 'static CGameBTComms* CGameBTComms::NewL(MGameBTCommsNotify*, TUint32, RSGEDebugLog*)', 'V'),
    28: ('gcx_Obj_NewL', 'static CGameBTDiscoverer* CGameBTDiscoverer::NewL(M*, RSGEDebugLog*)  [internal helper]', 'V'),
    29: RSGE['NewLine'],
    30: RSGE['Open'],
    31: ('gcx_Obj_Ret0', 'TInt CGameBTDiscoverer::StartL()  [internal helper]', 'V'),
    32: ('gc2_SendData', 'TInt CGameBTComms::SendData(const TDesC8&)', 'V'),
    33: ('gc2_SendDataTo', 'TInt CGameBTComms::SendData(TUint16, const TDesC8&)  [host only]', 'V'),
    34: ('gc2_SetBluetoothPowerState', 'TInt CGameBTComms::SetBluetoothPowerState(TBool)', 'V'),
    35: ('gcx_Obj_SetWord', 'void CGameBTDiscoverer::Set...(const TUint32&)  [internal helper]', 'V'),
    36: ('gc2_SetHostAcceptMode', 'TInt CGameBTComms::SetHostAcceptMode(TAcceptMode)  [host only]', 'V'),
    37: RSGE['SetLoggingLevel'],
    38: ('gcx_List_Show', 'void CGameBTDeviceList::Show()  [internal helper]', 'V'),
    39: ('gc2_StartClientL', 'void CGameBTComms::StartClientL(TInt aShowList, CEikAppUi*)', 'V'),
    40: ('gc2_StartHostL', 'void CGameBTComms::StartHostL(TInt aShowList, CEikAppUi*)', 'V'),
    **{o: RSGE['Write'] for o in range(41, 50)},
    50: ('gcx_Obj_dtor', 'CGameBTResolver::~CGameBTResolver()  [internal helper]', 'V'),
    51: ('gcx_List_dtor', 'CGameBTDeviceList::~CGameBTDeviceList()  [internal helper]', 'V'),
    52: ('gc2_dtor', 'CGameBTComms::~CGameBTComms()', 'V'),
    53: RSGE['dtor'],
    54: ('gcx_Obj_dtor', 'CGameBTDiscoverer::~CGameBTDiscoverer()  [internal helper]', 'V'),
    55: ('gcx_List_AppendL', 'void CGameBTDeviceList::AppendL(const TDeviceDetails&)  [internal helper]', 'V'),
}

ORD56 = {56: ('gc2_SetLocalDeviceName', 'TInt CGameBTComms::SetLocalDeviceName(THostName)', 'V')}
ORD57 = {57: ('gcx_Obj_Ret0', 'TInt CGameBTDiscoverer::StartFullL()  [internal helper]', 'V')}
ORD58_63 = {
    58: ('gc2_EndSession', 'void CGameBTComms::EndSession()', 'V'),
    59: ('gc2_DiscoverabilityOp', 'TInt CGameBTComms::<discoverability helper 3>()', 'I'),
    60: ('gc2_DiscoverabilityOp', 'TInt CGameBTComms::<discoverability helper 2>()', 'I'),
    61: ('gc2_SetDiscoverabilityModeLimited', 'TInt CGameBTComms::SetDiscoverabilityModeLimited(TBool)', 'V'),
    62: ('gcx_Obj_SetArg', 'TInt CGameBTDiscoverer::SetSearchModeLimited(...)  [internal helper]', 'I'),
    63: ('gc2_DiscoverabilityOp', 'TInt CGameBTComms::<discoverability helper 1>()', 'I'),
}

VARIANTS = {
    1: ('79c25cdd1972c287712475ee544c7bf3', 1, V1),
    2: ('8863fdea897f1c63807ad69252a3566f', 2, V2),
    3: ('f6fbc3c745ae0ecf584b3ef7ec4371ef', 2, {**V2, **ORD56}),
    4: ('c2f184012cd0635f3065b7d5042702ce', 2, {**V2, **ORD56}),
    5: ('e8d317204a763d71215680d567eb71e0', 2, {**V2, **ORD56, **ORD57}),
    6: ('bb02377c5361597f8d8503f5de584845', 2, {**V2, **ORD56, **ORD57}),
    7: ('f07f48ee44cca60046f40ce41858a160', 2, {**V2, **ORD56, **ORD57}),
    8: ('d4c10fcebfb6da8da6b757aa8392b6d7', 2, {**V2, **ORD56, **ORD57}),
    9: ('6ac8b1a1e490b367a1d5e46bb720fe70', 2, {**V2, **ORD56, **ORD57}),
    10: ('827df54717bc210daa96aef9e12de4a3', 2, {**V2, **ORD56, **ORD57, **ORD58_63}),
    11: ('75536306ef48b1928b4562890d01c9c6', 2, {**V2, **ORD56, **ORD57, **ORD58_63}),
}


def write_defs(outdir):
    os.makedirs(outdir, exist_ok=True)
    for v, (_, _, table) in VARIANTS.items():
        with open(os.path.join(outdir, f'gamecomms_v{v}.def'), 'w', newline='\n') as f:
            f.write(f'; GAMECOMMS.DLL variant v{v} - generated by dll/variants/variants.py, do not edit\n')
            f.write('EXPORTS\n')
            seen = {}
            for o in sorted(table):
                sym, _, _ = table[o]
                seen[sym] = seen.get(sym, 0) + 1
            for o in sorted(table):
                sym, proto, _ = table[o]
                # NONAME exports only need unique external names; shared
                # implementations are aliased (dlltool "external=internal")
                name = f'{sym}_{o}={sym}' if seen[sym] > 1 else sym
                f.write(f'\t{name} @ {o} NONAME ; {proto}\n')


def print_table():
    print('| Ordinal | ' + ' | '.join(f'v{v}' for v in VARIANTS) + ' |')
    print('|---:|' + '---|' * len(VARIANTS))
    for o in range(1, 64):
        cells = []
        for v, (_, _, t) in VARIANTS.items():
            cells.append(t[o][1].split('(')[0].replace('static ', '').split()[-1] + f' ({t[o][2]})' if o in t else '')
        print(f'| {o} | ' + ' | '.join(cells) + ' |')


def check(dlldir):
    sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..', 'tools', 're'))
    from e32 import E32
    ok = True
    for v, (_, _, t) in VARIANTS.items():
        e = E32(os.path.join(dlldir, f'gamecomms.v{v}.dll'))
        good = e.export_count == len(t) and sorted(t) == list(range(1, len(t) + 1))
        ok &= good
        print(f'v{v}: original {e.export_count} exports, table {len(t)} -> {"OK" if good else "MISMATCH"}')
    return ok


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'defs':
        write_defs(sys.argv[2])
    elif len(sys.argv) >= 2 and sys.argv[1] == 'table':
        print_table()
    elif len(sys.argv) >= 3 and sys.argv[1] == 'check':
        sys.exit(0 if check(sys.argv[2]) else 1)
    else:
        print(__doc__)
