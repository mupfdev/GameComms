/** Game list selection navigation. SPDX-License-Identifier: MIT */
#ifndef GAMENAVIGATION_H
#define GAMENAVIGATION_H

enum TGameNavigationDirection
{
    EGameNavUp,
    EGameNavDown,
    EGameNavLeft,
    EGameNavRight
};

static int GameNavigationIndex(int aIndex, int aCount, enum TGameNavigationDirection aDirection)
{
    const int pageSize = 6;
    if (aCount <= 0 || aIndex < 0 || aIndex >= aCount) {
        return aIndex;
    }
    switch (aDirection) {
    case EGameNavUp:
        return aIndex == 0 ? aCount - 1 : aIndex - 1;
    case EGameNavDown:
        return aIndex == aCount - 1 ? 0 : aIndex + 1;
    case EGameNavLeft:
        return aIndex < pageSize ? 0 : aIndex - pageSize;
    case EGameNavRight:
        return aCount - 1 - aIndex < pageSize ? aCount - 1 : aIndex + pageSize;
    }
    return aIndex;
}

#endif /* GAMENAVIGATION_H */
