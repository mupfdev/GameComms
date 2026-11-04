/** @file GcExports.cpp
 *
 *  Exported entry points.  Every original ordinal is bound (by the
 *  generated gamecomms_vN.def) to one of the extern "C" thunks below.
 *  The thunks reproduce the GCC 2.9 / ARM calling convention of the
 *  original C++ member functions: the object pointer arrives in r0
 *  ("this"), arguments follow in r1..r3 and on the stack, class-typed
 *  by-value arguments arrive as pointers to a copy.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "GameBTComms.h"
#include "GcHelpers.h"

#ifndef GC_VARIANT
#define GC_VARIANT 6
#endif

#define GC_EXPORT extern "C" EXPORT_C

/* ================================================================== */
/* RSGEDebugLog (all variants).  The shipped libraries were built      */
/* without SGE_DEV_PHASE: every method is a no-op returning KErrNone.  */
/* ================================================================== */

RSGEDebugLog::RSGEDebugLog() : iReserved0(0), iReserved1(0), iReserved2(0), iLevel(0)
{
}

RSGEDebugLog::~RSGEDebugLog()
{
}

GC_EXPORT RSGEDebugLog *rsge_ctor(RSGEDebugLog *aThis)
{
    return new (aThis) RSGEDebugLog;
}

GC_EXPORT void rsge_dtor(RSGEDebugLog *aThis, TInt aFlags)
{
    aThis->~RSGEDebugLog();
    if (aFlags & 1) {
        User::Free(aThis);
    }
}

GC_EXPORT TInt rsge_ret0(RSGEDebugLog * /*aThis*/)
{
    return KErrNone; /* Open, NewLine, WriteLine(...), Write(...) */
}

GC_EXPORT void rsge_void(RSGEDebugLog * /*aThis*/)
{
    /* Close */
}

GC_EXPORT void rsge_SetLoggingLevel(RSGEDebugLog *aThis, TInt aLevel)
{
    aThis->iLevel = aLevel;
}

/* ================================================================== */
/* GameComms 1.x (variant v1)                                          */
/* ================================================================== */

GC_EXPORT CGameBTComms1 *gc1_NewL(MGameBTCommsNotify *aNotify, TUint32 aGameUid, RSGEDebugLog * /*aLog*/)
{
    return CGameBTComms1::NewL(aNotify, aGameUid);
}

GC_EXPORT void gc1_dtor(CGameBTComms1 *aThis, TInt aFlags)
{
    aThis->CGameBTComms1::~CGameBTComms1();
    if (aFlags & 1) {
        User::Free(aThis);
    }
}

GC_EXPORT void gc1_StartHostL(CGameBTComms1 *aThis, TUint16 aStart, TUint16 aMin) { aThis->StartHostL(aStart, aMin); }
GC_EXPORT void gc1_StartClientL(CGameBTComms1 *aThis) { aThis->StartClientL(); }
GC_EXPORT TInt gc1_ConnectionRole(CGameBTComms1 *aThis) { return aThis->ConnectionRole(); }
GC_EXPORT TInt gc1_GameState(CGameBTComms1 *aThis) { return aThis->GameState(); }
GC_EXPORT TInt gc1_ConnectState(CGameBTComms1 *aThis) { return aThis->ConnectState(); }
GC_EXPORT TInt gc1_GetLocalDeviceName(CGameBTComms1 *aThis, THostName &aName) { return aThis->GetLocalDeviceName(aName); }
GC_EXPORT void gc1_Disconnect(CGameBTComms1 *aThis) { aThis->Disconnect(); }
GC_EXPORT TInt gc1_DisconnectClient(CGameBTComms1 *aThis, TUint16 aId) { return aThis->DisconnectClient(aId); }
GC_EXPORT TInt gc1_SendDataToClient(CGameBTComms1 *aThis, TUint16 aId, TDesC8 &aData) { return aThis->SendDataToClient(aId, aData); }
GC_EXPORT TInt gc1_SendDataToAllClients(CGameBTComms1 *aThis, TDesC8 &aData) { return aThis->SendDataToAllClients(aData); }
GC_EXPORT TInt gc1_SendDataToHost(CGameBTComms1 *aThis, TDesC8 &aData) { return aThis->SendDataToHost(aData); }
GC_EXPORT TInt gc1_ContinueMultiPlayerGame(CGameBTComms1 *aThis) { return aThis->ContinueMultiPlayerGame(); }
GC_EXPORT TInt gc1_ReconnectL(CGameBTComms1 *aThis, TBool aAll) { return aThis->ReconnectL(aAll); }
GC_EXPORT TInt gc1_PauseMultiPlayerGame(CGameBTComms1 *aThis) { return aThis->PauseMultiPlayerGame(); }
GC_EXPORT TInt gc1_EndMultiPlayerGame(CGameBTComms1 *aThis) { return aThis->EndMultiPlayerGame(); }
GC_EXPORT TBool gc1_IsShowingDeviceSelectDlg(CGameBTComms1 *aThis) { return aThis->IsShowingDeviceSelectDlg(); }

/* ================================================================== */
/* GameComms 2.x (variants v2..v11)                                    */
/* ================================================================== */

GC_EXPORT CGameBTComms2 *gc2_NewL(MGameBTCommsNotify2 *aNotify, TUint32 aGameUid, RSGEDebugLog * /*aLog*/)
{
    return CGameBTComms2::NewL(aNotify, aGameUid, GC_VARIANT);
}

GC_EXPORT void gc2_dtor(CGameBTComms2 *aThis, TInt aFlags)
{
    aThis->CGameBTComms2::~CGameBTComms2();
    if (aFlags & 1) {
        User::Free(aThis);
    }
}

GC_EXPORT void gc2_StartHostL(CGameBTComms2 *aThis, TInt aList, CEikAppUi *aUi) { aThis->StartHostL(aList, aUi); }
GC_EXPORT void gc2_StartClientL(CGameBTComms2 *aThis, TInt aList, CEikAppUi *aUi) { aThis->StartClientL(aList, aUi); }
GC_EXPORT TInt gc2_ConnectionRole(CGameBTComms2 *aThis) { return aThis->ConnectionRole(); }
GC_EXPORT TInt gc2_ConnectState(CGameBTComms2 *aThis) { return aThis->ConnectState(); }
GC_EXPORT TInt gc2_GetLocalDeviceName(CGameBTComms2 *aThis, THostName &aName) { return aThis->GetLocalDeviceName(aName); }
GC_EXPORT TInt gc2_BluetoothPowerState(CGameBTComms2 *aThis, TBool &aState) { return aThis->BluetoothPowerState(aState); }
GC_EXPORT TInt gc2_SetBluetoothPowerState(CGameBTComms2 *aThis, TBool aState) { return aThis->SetBluetoothPowerState(aState); }
GC_EXPORT TInt gc2_SetLocalDeviceName(CGameBTComms2 *aThis, const TDesC &aName) { return aThis->SetLocalDeviceName(aName); }
GC_EXPORT TInt gc2_DeclareRemoteDevice(CGameBTComms2 *aThis, const TDeviceDetails &aDev) { return aThis->DeclareRemoteDevice(aDev); }
GC_EXPORT TInt gc2_Connect(CGameBTComms2 *aThis) { return aThis->Connect(); }
GC_EXPORT TInt gc2_ConnectClient(CGameBTComms2 *aThis, TUint16 aId) { return aThis->Connect(aId); }
GC_EXPORT void gc2_Disconnect(CGameBTComms2 *aThis) { aThis->Disconnect(); }
GC_EXPORT TInt gc2_DisconnectClient(CGameBTComms2 *aThis, TUint16 aId, TBool aPerm) { return aThis->Disconnect(aId, aPerm); }
GC_EXPORT TInt gc2_SendData(CGameBTComms2 *aThis, const TDesC8 &aData) { return aThis->SendData(aData); }
GC_EXPORT TInt gc2_SendDataTo(CGameBTComms2 *aThis, TUint16 aId, const TDesC8 &aData) { return aThis->SendData(aId, aData); }
GC_EXPORT TInt gc2_SetHostAcceptMode(CGameBTComms2 *aThis, TInt aMode) { return aThis->SetHostAcceptMode(aMode); }
GC_EXPORT TInt gc2_HostAcceptMode(CGameBTComms2 *aThis) { return aThis->HostAcceptMode(); }
GC_EXPORT void gc2_EndSession(CGameBTComms2 *aThis) { aThis->EndSession(); }
GC_EXPORT TInt gc2_SetDiscoverabilityModeLimited(CGameBTComms2 *aThis, TBool aLimited) { return aThis->SetDiscoverabilityModeLimited(aLimited); }
GC_EXPORT TInt gc2_DiscoverabilityOp(CGameBTComms2 * /*aThis*/) { return KErrNone; }

/* ================================================================== */
/* GameComms 2.x helper classes that the original exported as well:   */
/* device list control, device discoverer and SDP resolver.  The       */
/* discoverer and resolver work (Ashen uses them for its host search); */
/* the list control is an inert, memory-safe stand-in so that a game   */
/* that references it keeps working; see docs/COMPATIBILITY.md.        */
/* ================================================================== */

class CGcCompatList : public CBase
{
  public:
    CGcCompatList() : iArray(4) {}
    CArrayFixFlat<TDeviceDetails> iArray;
};

GC_EXPORT CGcCompatList *gcx_List_NewL(const TRect *, TAny *, TInt, RSGEDebugLog *)
{
    return new (ELeave) CGcCompatList;
}

GC_EXPORT void gcx_List_dtor(CGcCompatList *aThis, TInt aFlags)
{
    aThis->CGcCompatList::~CGcCompatList();
    if (aFlags & 1) {
        User::Free(aThis);
    }
}

GC_EXPORT void gcx_List_AppendL(CGcCompatList *aThis, const TDeviceDetails &aDev) { aThis->iArray.AppendL(aDev); }

GC_EXPORT void gcx_List_InsertL(CGcCompatList *aThis, TInt aIndex, const TDeviceDetails &aDev)
{
    if (aIndex >= 0 && aIndex <= aThis->iArray.Count()) {
        aThis->iArray.InsertL(aIndex, aDev);
    }
}

GC_EXPORT void gcx_List_Delete(CGcCompatList *aThis, TInt aIndex)
{
    if (aIndex >= 0 && aIndex < aThis->iArray.Count()) {
        aThis->iArray.Delete(aIndex);
    }
}

GC_EXPORT TDeviceDetails *gcx_List_At(CGcCompatList *aThis, TInt aIndex)
{
    if (aIndex < 0 || aIndex >= aThis->iArray.Count()) {
        User::Panic(_L("GC-LIST"), 0);
    }
    return &aThis->iArray[aIndex];
}

GC_EXPORT TInt gcx_List_Count(CGcCompatList *aThis) { return aThis->iArray.Count(); }
GC_EXPORT void gcx_List_Reset(CGcCompatList *aThis) { aThis->iArray.Reset(); }
GC_EXPORT TInt gcx_List_CurrentItem(CGcCompatList *) { return -1; }
GC_EXPORT TAny *gcx_List_ListBox(CGcCompatList *) { return NULL; }
GC_EXPORT void gcx_List_Show(CGcCompatList *) {}
GC_EXPORT void gcx_List_Hide(CGcCompatList *) {}

/* ---- device discoverer and service resolver: working versions, see
 *      GcHelpers.h (games like Ashen search for hosts with them) ---- */

GC_EXPORT CGcDiscoverer *gcx_Disc_NewL(MGameBTDiscovererObserver *aObserver, TAny * /*aLog*/)
{
    return CGcDiscoverer::NewL(aObserver);
}

GC_EXPORT void gcx_Disc_dtor(CGcDiscoverer *aThis, TInt aFlags)
{
    aThis->CGcDiscoverer::~CGcDiscoverer();
    if (aFlags & 1) {
        User::Free(aThis);
    }
}

GC_EXPORT TInt gcx_Disc_StartL(CGcDiscoverer *aThis) { return aThis->StartL(); }
GC_EXPORT TInt gcx_Disc_Cancel(CGcDiscoverer *aThis) { return aThis->Cancel(); }
GC_EXPORT TBool gcx_Disc_IsIdle(CGcDiscoverer *aThis) { return aThis->IsIdle(); }
GC_EXPORT TUint32 *gcx_Disc_DeviceClass(CGcDiscoverer *aThis) { return &aThis->DeviceClass(); }
GC_EXPORT void gcx_Disc_SetDeviceClass(CGcDiscoverer *aThis, const TUint32 &aClass) { aThis->SetDeviceClass(aClass); }
GC_EXPORT TInt gcx_Disc_SetSearchMode(CGcDiscoverer *aThis, TUint32 aMode) { return aThis->SetSearchMode(aMode); }

GC_EXPORT CGcResolver *gcx_Res_NewL(MGameBTResolverObserver *aObserver, TAny * /*aGameUid*/)
{
    return CGcResolver::NewL(aObserver);
}

GC_EXPORT void gcx_Res_dtor(CGcResolver *aThis, TInt aFlags)
{
    aThis->CGcResolver::~CGcResolver();
    if (aFlags & 1) {
        User::Free(aThis);
    }
}

GC_EXPORT TInt gcx_Res_Start(CGcResolver *aThis, const TDeviceDetails &aDevice) { return aThis->Start(aDevice); }

/* ================================================================== */

GLDEF_C TInt E32Dll(TDllReason /*aReason*/)
{
    return KErrNone;
}
