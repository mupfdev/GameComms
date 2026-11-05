/** @file GameList.h
 *
 *  Games supported by PhantomLink and the replacement GAMECOMMS.DLL
 *  variant each one needs (see the table in README.md).
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GAMELIST_H
#define GAMELIST_H

#include <e32std.h>

struct TGameEntry
{
    const char *iName;
    TInt iVariant; /* 1..11 */
};

TInt GameCount();
const TGameEntry &GameAt(TInt aIndex);

#endif /* GAMELIST_H */
