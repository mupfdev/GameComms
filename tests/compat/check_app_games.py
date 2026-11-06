#!/usr/bin/env python3
"""Check that the game list of the Symbian app matches the README table.

Every game of "Which DLL for which game" in README.md that has a replacement
variant (vN) must be listed in app/src/GameList.cpp with the same variant, and
the other way round.
"""

import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")


def readme_games():
    games = {}
    with open(os.path.join(ROOT, "README.md"), encoding="utf-8") as f:
        for line in f:
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) >= 2 and re.fullmatch(r"v\d+", cells[1]):
                games[cells[0]] = int(cells[1][1:])
    return games


def app_games():
    games = {}
    with open(os.path.join(ROOT, "app", "src", "GameList.cpp"), encoding="utf-8") as f:
        for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*(\d+)\s*(?:,\s*(?:"[^"]*"|NULL)\s*)?\}', f.read()):
            games[m.group(1)] = int(m.group(2))
    return games


def main():
    readme, app = readme_games(), app_games()
    errors = []
    for name, variant in sorted(readme.items()):
        if name not in app:
            errors.append("missing in app: %s (v%d)" % (name, variant))
        elif app[name] != variant:
            errors.append("%s: README v%d, app v%d" % (name, variant, app[name]))
    for name in sorted(set(app) - set(readme)):
        errors.append("not in README: %s" % name)
    for e in errors:
        print("ERROR:", e)
    print("%d games checked, %d problems" % (len(readme), len(errors)))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
