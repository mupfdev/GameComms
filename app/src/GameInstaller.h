/** @file GameInstaller.h
 *
 *  Copies the replacement GAMECOMMS.DLL to a game-specific directory, or
 *  E:\System\Libs by default.
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
    static TInt Install(TInt aVariant, const TDesC &aAppFullName, const char *aTargetDir = NULL);
};

#endif /* GAMEINSTALLER_H */
