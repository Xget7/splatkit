# 0015. Tiles with offline levels replace the in-memory tree for tiled worlds

Status: accepted. Date: 2026-09-10.

A world of 15M splats and up does not fit a single cloud with a global sort, and the in-memory tree of ADR 0010 costs 1.5 times the splats in memory, seconds to build at load and 90 to 220 ms per selection on the phone.
We decided that a tiled world carries its levels of detail precomputed offline, one spz file per tile and level, indexed by a tileset, and that the engine streams tiles by screen error against a residency budget: a tile is drawn whole or not at all, and the sort runs per tile.
The tree stays for worlds that are one file and opt in through `splatBudget`; a tiled world never builds one, so there is exactly one level of detail mechanism per kind of world and no selection walk on the phone.

Considered and rejected: a tree inside every tile (finer selection, but two mechanisms to keep and the selection cost we measured), and building levels on the phone at first load (30 s and 1.5 times the memory for 15M, every install).
