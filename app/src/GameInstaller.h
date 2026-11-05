/** @file GameInstaller.h
 *
 *  Copies the replacement GAMECOMMS.DLL of a variant to E:\System\Libs.
 *  The DLLs ship with the application in <app dir>\dlls\gcvN.dll.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GAMEINSTALLER_H
#define GAMEINSTALLER_H

#include <e32std.h>

class GameInstaller
{
  public:
    /** Returns KErrNone or a system-wide error code (for example KErrInUse
     *  while the game is running, KErrNotFound without memory card). */
    static TInt Install(TInt aVariant, const TDesC &aAppFullName);
};

#endif /* GAMEINSTALLER_H */
