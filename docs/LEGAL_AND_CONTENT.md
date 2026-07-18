# Legal and content boundary

This is an engineering policy, not legal advice.

- Contributors must use a lawfully obtained copy of the game.
- No ROM, ROM fragment, extracted asset, music, BRR sample, level data, text,
  save file, or copyrighted screenshot belongs in the public source tree.
- Tests should use synthetic fixtures or hashes. Private integration tests may
  reference an external ROM path.
- Research repositories without an explicit license are references only. Do
  not copy their source, comments, generated data, or bundled assets. This
  private repository currently includes mechanically derived function labels
  and structural CFG contracts from the H4v0c21 address map; it excludes the
  referenced assembly, comments, ROM bytes, and assets. Review provenance and
  legal implications before any public redistribution of that metadata.
- Reused third-party code must include its license, exact source revision,
  provenance, and a record of local changes. The LakeSnes APU subset follows
  this rule under `third_party/lakesnes_apu`.
- A license attached to a reverse-engineering tool does not license Nintendo or
  Rare content processed by that tool.
- Any public release of translated or reconstructed game logic should receive
  appropriate legal review for the intended jurisdiction.
