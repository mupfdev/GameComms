/** @file GcConfig.h
 *
 *  Optional settings file.  Looked up in this order:
 *    E:\GameComms.ini   (memory card, easy to edit on a PC)
 *    C:\System\Data\GameComms.ini
 *
 *  [Config]
 *  DeviceName=Maxwell        ; name shown to other players (default: phone BT name)
 *  AutoJoin=0                ; 1 = clients join the first open session without asking
 *  Log=0                     ; 1 = write E:\GameComms.log
 *  [Bridge]
 *  Address=0002ee112233      ; Bluetooth address of the ESP32 bridge (saved automatically)
 *  Channel=0                 ; RFCOMM channel, 0 = SDP lookup
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GCCONFIG_H
#define GCCONFIG_H

#include <bttypes.h>
#include <e32base.h>

class TGcConfig
{
  public:
    TGcConfig();
    /** Reads the settings file; missing file/keys keep the defaults. */
    void Load();
    /** Persists the bridge address after the first successful selection. */
    void SaveBridgeAddress(const TBTDevAddr &aAddr);

  public:
    TBuf8<33> iDeviceName; /* UTF-8, empty = use Bluetooth name */
    TBool iAutoJoin;
    TBool iLog;
    TBool iHasBridge;
    TBTDevAddr iBridge;
    TInt iChannel;
    TFileName iPath; /* file that was read */
};

#endif /* GCCONFIG_H */
