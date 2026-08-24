# DKC2 disassembly reference provenance

- Project: `H4v0c21/DKC2-disassembly`
- URL: <https://github.com/H4v0c21/DKC2-disassembly>
- Validated revision: `59a3a8aef88d074f488d25d0d0623fcb37fa3791`
- License at that revision: no explicit license found

The project is used as an address and behavior reference only. No assembly
source, comments, ROM-derived assets, or game data are copied here.

The widescreen diagnostic decoder independently reads these factual North
American v1.0 WRAM boundaries:

- sprite table `$0D84`, 25 records of `$5E` bytes;
- sprite render table `$16FE`;
- camera X/Y `$17BA/$17C0`; and
- the documented sprite-record field offsets needed to label diagnostic
  values.

For widescreen screen classification it also reads the factual level-config
field boundaries at `$0515-$0539`, the gameplay sub-mode index at `$0529`,
the decompressed metatile and terrain-VRAM pointers at `$17B4/$17B6`, and the
horizontal-versus-vertical map-address behavior selected by the gameplay
dispatch. The local implementation uses independently named enums and address
calculations; no reference tables, prose, or assembly are reproduced.

The Bramble Scramble widescreen calibration also uses the factual association
of game sub-mode `$10` with the square scroll family. The local `$C0`-byte row
calculation and tests are independently expressed and validated against private
WRAM/VRAM snapshots; no reference routine body or level data is retained.

Hornet Hole diagnosis used the factual `$B5:B322` source-pointer advance to
distinguish ordinary hive sub-mode `$03` from Bramble. The independently
implemented `$A0`-byte/80-metatile calculation was validated against the
owner's private state and movement recording; neither is retained in Git.

It was also consulted to identify the factual separation between the common
object renderer and the dedicated collectible-banana list/OAM path. Local
runtime traces supplied the coordinate and OAM evidence used by the
independently implemented adapter; no reference routine body or comments are
included.

Local adaptation consists only of reading those numeric fields from a private
WRAM snapshot and serializing original JSON field names. It does not assemble
or redistribute the reference project. Because no compatible license was
identified, its source and prose must not be copied into this repository.
