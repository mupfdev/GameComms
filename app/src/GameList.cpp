/** @file GameList.cpp
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "GameList.h"

/* Keep in sync with "Which DLL for which game" in README.md
   (tests/compat/check_app_games.py verifies it). */
static const TGameEntry KGames[] = {
    { "Ashen", 8 },
    { "Asphalt Urban GT", 3 },
    { "Asphalt: Urban GT 2", 11 },
    { "Bomberman", 8 },
    { "Call of Duty", 6 },
    { "Catan", 5 },
    { "Colin McRae Rally 2005", 8 },
    { "Crash Nitro Kart", 8 },
    { "FIFA Football 2004", 3 },
    { "FIFA Football 2005", 6 },
    { "Glimmerati", 10 },
    { "Habbo Islands", 11 },
    { "High Seize", 6 },
    { "Marcel Desailly Pro Soccer", 1 },
    { "Mile High Pinball", 5 },
    { "MotoGP", 2 },
    { "NCAA Football 2004", 3 },
    { "ONE", 10 },
    { "Operation Shadow", 8 },
    { "Pandemonium!", 2 },
    { "Pathway to Glory", 5 },
    { "Pathway to Glory: Ikusa Islands", 6 },
    { "Puyo Pop", 2 },
    { "Puzzle Bobble VS", 2 },
    { "Rayman 3", 3 },
    { "Requiem of Hell", 5 },
    { "Rifts: Promise of Power", 10 },
    { "Snakes", 7 },
    { "SSX Out of Bounds", 6 },
    { "System Rush", 10 },
    { "The King of Fighters Extreme", 5 },
    { "The Roots: Gates of Chaos", 10 },
    { "The Sims Bustin' Out", 4 },
    { "Tiger Woods PGA Tour 2004", 9 },
    { "Tom Clancy's Ghost Recon: Jungle Storm", 3 },
    { "Tom Clancy's Splinter Cell: Chaos Theory", 5 },
    { "Tomb Raider", 2 },
    { "Tony Hawk's Pro Skater", 2 },
    { "Warhammer 40,000: Glory in Death", 11 },
    { "Worms World Party", 6 },
    { "WWE Aftershock", 6 },
    { "X-Men Legends", 6 },
    { "X-Men Legends II: Rise of Apocalypse", 6 },
    { "Xanadu Next", 6 }
};

TInt GameCount()
{
    return sizeof(KGames) / sizeof(KGames[0]);
}

const TGameEntry &GameAt(TInt aIndex)
{
    return KGames[aIndex];
}
