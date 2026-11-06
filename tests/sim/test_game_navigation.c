/* Game list navigation tests. SPDX-License-Identifier: MIT */
#include "GameNavigation.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(GameNavigationIndex(0, 44, EGameNavUp) == 43);
    assert(GameNavigationIndex(43, 44, EGameNavDown) == 0);
    assert(GameNavigationIndex(12, 44, EGameNavUp) == 11);
    assert(GameNavigationIndex(12, 44, EGameNavDown) == 13);
    assert(GameNavigationIndex(12, 44, EGameNavLeft) == 6);
    assert(GameNavigationIndex(12, 44, EGameNavRight) == 18);
    assert(GameNavigationIndex(4, 44, EGameNavLeft) == 0);
    assert(GameNavigationIndex(40, 44, EGameNavRight) == 43);
    assert(GameNavigationIndex(0, 44, EGameNavLeft) == 0);
    assert(GameNavigationIndex(43, 44, EGameNavRight) == 43);
    assert(GameNavigationIndex(0, 1, EGameNavUp) == 0);
    assert(GameNavigationIndex(0, 1, EGameNavDown) == 0);
    assert(GameNavigationIndex(0, 1, EGameNavRight) == 0);
    assert(GameNavigationIndex(3, 5, EGameNavLeft) == 0);
    assert(GameNavigationIndex(1, 5, EGameNavRight) == 4);
    assert(GameNavigationIndex(-1, 0, EGameNavDown) == -1);
    puts("Game navigation tests passed");
    return 0;
}
