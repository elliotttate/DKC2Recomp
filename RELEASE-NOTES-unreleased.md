# Unreleased features and fixes

Prepared: 2026-10-29
Scope: changes since `3b386cb` (Release v0.0.8), assembled for commit review.
Status: unreleased; no new version number assigned.

These changes extend simultaneous TEAM co-op. Classic TEAM and single-player
retain their native behavior unless a change explicitly applies to the host
application, controllers, or developer tools.

## New features

### Select catch-up teleport

- Hold Select to move the requesting player beside their living teammate.
  The current compiled hold duration is **2 seconds**.
- Works in either direction: P1 to P2 or P2 to P1.
- Supports the tested ground, rope, swimming, land/water transition,
  sticky-wall and mounted-teammate recovery cases.
- Preserves held-item ownership and restores normal movement or the validated
  destination wall attachment.
- Fires once per hold; releasing Select allows another activation. Pause and
  loading reset the charge. Simultaneous requests resolve once.
- Confirmed live players recover stale saved loss flags. Genuinely dead players
  cannot use teleport to revive; they require a DK barrel.
- Earlier development notes describe a three-second hold. The checked-in
  compiled tuning now selects two seconds; editing it requires a rebuild.

### Shared enemy-stomp bounce

- When one player earns a real enemy stomp, a nearby living teammate who is
  also airborne and descending near that enemy receives the same bounce.
- The current proximity window is **32 horizontal by 32 vertical game pixels**.
  It accommodates slightly staggered contacts.
- Copies the accepted native impulse, gravity and terminal-speed settings while
  preserving each player's position, animation and horizontal control.
- Includes descending barrel-shot flight and attached/detached coaster riders.
- Excludes grounded, rising, distant, dead/lost, mounted and incompatible
  scripted teammates. It does not teleport players or grant invincibility.

### Shared barrel operation

- Either living player entering a supported transport or manual rotating barrel
  brings both living players inside; dead players remain excluded.
- Either controller can fire both players together.
- Either controller can aim a shared manual barrel. Opposing directions cancel
  on each axis; matching directions retain the native rotation speed.
- After the native launch lock ends, both players can steer independently.

### Shared Squawks and animal-section control

- Both living players hang from Squawks together.
- Damage transfers flight control to the survivor while preserving the shared
  flight state.
- Animal-only transformation sections keep the inactive Kong hidden rather
  than spawning it on any button press.
- Each surviving damage hit in those sections transfers control to the other
  controller, preserving native health, healing and final-loss behavior.
- The animal-only policy covers all five native animal types. Ordinary riding
  sections continue to support an independent on-foot teammate.

### Special shared kart control

- In the special mode-02 kart, Jump makes the vehicle jump with both riders
  seated rather than letting either player jump out.
- The first player to mount controls its jump. Later boarding preserves that
  ownership, including when P2 boards first.
- Ordinary mode-01 rollercoasters retain individual rider jumps.

### Compiled gameplay and developer configuration

These files are compiled into the executable. Rebuild after editing them;
restarting the game alone does not apply changes. Invalid values fail compilation.

| File | Setting | Current value | Purpose |
| --- | --- | --- | --- |
| `config/coop_tuning.cfg` | `DKC2_COOP_SELECT_HOLD_MS` | `2000` | Teleport hold duration in milliseconds |
| `config/coop_tuning.cfg` | `DKC2_COOP_STOMP_HORIZONTAL_PX` | `32` | Shared-stomp horizontal distance |
| `config/coop_tuning.cfg` | `DKC2_COOP_STOMP_VERTICAL_PX` | `32` | Shared-stomp vertical distance |
| `config/developer.cfg` | `DKC2_DEVELOPER_ENABLED` | `1` | Enable developer commands |
| `config/developer.cfg` | `DKC2_DEVELOPER_BEAT_LEVEL_KEY` | `8` | Function-key shortcut: F8 |

- Added the `beat_level` developer command and F8 keyboard shortcut.
- Normal levels receive their native progress credit and exit sequence.
- Boss completion credits the boss reward/map node and returns to the map.
- Bonus completion credits its reward once and returns to the parent level
  without clearing that level, preserving the animal form where applicable.
- Menus, stale map gameplay fields and repeated completion requests are rejected.
- Developer commands can be compiled out; shortcut value `0` disables the key.

## Controller and launcher improvements

- Native Windows gameplay uses SDL GameController for USB and Bluetooth input
  instead of the previous XInput-only path.
- Added an upstream Windows mapping for the 8BitDo SN30 Pro Bluetooth DInput
  device missing from the pinned SDL mapping database.
- The launcher lists controllers and lets each player select their own device.
- Device choices, input sources, deadzones and bindings persist in `launcher.cfg`.
- Controllers connected after launch are detected automatically.
- Automatic assignment preserves connected-controller ownership when another
  controller disconnects. An explicitly selected missing device remains neutral
  until it reconnects.
- Each player's pause-menu Controls tab shows and rebinds its assigned device.
- Added controller-assignment tests and SDL mapping provenance/license files
  under `third_party/sdl_gamecontrollerdb/`.

## Gameplay fixes

### Camera and level flow

- The viewport follows the full-precision midpoint of both eligible living
  players on both axes, rather than continuing to track P1 alone.
- Centers that midpoint within the viewport while retaining native level
  bounds, smoothing and terrain streaming.
- Extends midpoint tracking through the tested swimming, pole, stomp-bounce,
  sticky-wall and cat-grab states.
- Either player's valid goal contact can start the native level ending.
- Bonus teleporter entry restores both living players on the arrival platform
  without requiring Select.
- Fixed transport-cannon entry repeatedly trapping the other player in a loop.
- Fixed throwing a carried player into the elevated Gangplank Galley barrel
  stranding both players instead of completing entry and arrival.
- Keeps the partner safe during Kleever's scripted intro instead of letting
  them sink into lava; both regain independent controls afterward.

### Item ownership and enemy reactions

- An empty-handed partner's Run/Pick or Jump no longer makes the other player
  drop or throw their item.
- One player's water entry no longer releases the other player's held beetle
  or other grabbable. The carrier's own native water-entry behavior remains.
- Ground and airborne item ownership survive save/load.
- Enemy knockback targets the actual contacted player rather than turning
  them into a player being carried. Intentional team pickup still works.
- Kudgel's ground shake independently stuns each eligible grounded player;
  jumping players avoid it, and a shared stun timer advances once per frame.
- Cat O' Nine Tails grabs, spins and launches only the player that touched it.
  A distant teammate is no longer recruited, repositioned, hidden or launched.
- The cat-grab camera includes both players, and the free teammate remains
  controllable throughout the captured player's spin and release.

### Hooks and hangers

- Either player's contact attaches that player without summoning the teammate.
- Both players can share a fixed hook or Kleever dropping hook in either grab
  order, preserving the relevant native camera framing.
- Hooks with camera-framing parameters also support sharing.
- Each player can jump off independently. The last occupant no longer needs
  two jump presses or immediately re-grabs after an expired earlier cooldown.
- Throwing a player into a hanger no longer freezes the thrower in team wait;
  affected waiting saves can recover.
- Horizontal team throws can grab a hook on real contact, as upward throws can.
  The direct independent attachment avoids a stalled AI-reunion flight.
- Hanging after a throw restores interaction eligibility, including from older
  saves, so the player can use subsequent hooks.
- A teammate hanging elsewhere no longer blocks another hook's grab.
- On-foot players can grab/release hangers while their teammate rides Rattly.

### Poles, ropes and honey terrain

- The independent swimming player now turns to face their movement.
- Both players can attach to, climb and leave actor poles independently,
  including sharing one pole in either entry order.
- The second player's climbing animation advances in both directions and
  settles into the native idle pose.
- Pole departure references expire after leaving contact, allowing later
  re-grabs and reused pole slots rather than permanently blocking attachment.
- Re-grabbing also works in the tested death/respawn and surviving-player cases.
- Honey-floor sticking affects the touching player; the dry teammate keeps
  control, and either player can jump free when both are on honey.
- Sticky-wall contact belongs to the touching player. Either player can attach,
  share the wall and jump away independently.

### Hurt, death and revival

- Either player's pit fall preserves an eligible living teammate rather than
  ending the level because the camera leader fell.
- Fallen players count as dead and remain lost until revived by a DK barrel;
  Select, ordinary input and loading do not revive them.
- Pole- and honey-attached survivors retain their own attachment and controls.
- Thorn contact keeps the native hurt animation, death arc and survivor handoff
  rather than immediately hiding the victim.
- Ceiling/side terrain damage belongs to the player who touched it, including
  when that player is inactive; it no longer kills the distant teammate.
- DK-barrel revival finishes at the living teammate's current position without
  arrival drift off a narrow platform.
- Restored live actors clear stale loss metadata without reviving truly dead
  actors. Relevant ownership and loss state persist across save/load.

### Animals and mounted transitions

- Rattly's high jumps no longer park a safe on-foot teammate solely because
  they are below the camera; tested offscreen rescue remains available.
- Rattly or Squitter rider pit loss restores palette ownership, clears orphaned
  mount state and hands control to the survivor correctly.
- Older affected orphaned-mount saves can recover.
- Mounted bonus returns complete their native arrival without leaving the
  rider stuck in barrel flight, moving without input or inheriting the wrong
  animal state.

### Barrel flight and ordinary rollercoasters

- Restores independent passenger steering after the native barrel-launch lock.
- Both barrel passengers use native shot momentum, upward force, gravity and
  terminal speed. Landing restores normal movement instead of a stuck roll or
  barrel-spin pose; applicable older grounded saves recover normal fall speed.
- Ordinary rollercoaster riders can jump independently without ejecting the
  other seated player.
- Coaster stomps produce the native upward bounce, including after the rider
  has detached from the previous cart, and support the shared-bounce window.
- Rider hurt and pit loss preserve the living teammate; final loss retains
  native life accounting and Game Over.
- Lone survivors can land on the next cart using native foot contact.
- A bounded descending catch near an empty seat makes paired next-cart landings
  more forgiving when the other rider has already landed.
- Early cart-to-cannon entry releases stale cart ownership so the falling cart
  cannot reclaim the captured player or cause a false pit loss.
- Native scripted coaster exits keep carrying their riders to the exit fade
  and goal area rather than leaving a player frozen while the cart continues.

## Save compatibility and verification support

- Added synthetic behavior tests, generated-adapter validation and emitter
  contract coverage for the new ownership and transition rules.
- Added private input-driven regression scripts for the reported checkpoints,
  including exact reload checks through selected movement, hurt, capture,
  launch and exit transitions.
- Added optional CMake regression inputs for external ROM/save paths.
- Private integration checks use ROM hashes and external paths. Save files are
  copied before testing; private ROMs, saves, graphics, audio, screenshots and
  generated game binaries are excluded from repository content.
- This change set preserves the existing save-state format; selected older
  broken states are repaired when their native state proves the recovery safe.

## Recorded validation and remaining limitations

Fresh pre-commit validation was performed on 2026-10-29 against hash-verified
external checkpoint copies. Commands, checkpoint hashes, results and remaining
limitations are recorded in [the evidence manifest](docs/evidence/coop-validation-2026-10-29.md).

- Release unit, headless, native Windows desktop and SDL builds passed.
- The complete available CTest run reported **56/58 passing** before and after
  the latest gameplay milestone.
- The two original failures were `create_windows_version` and `make_release`:
  their spawned PowerShell environment cannot resolve `Get-FileHash`.
  These were subsequently fixed by explicit host-module imports; synthetic
  packaging and the complete public suite now pass **58/58**. See
  [follow-up evidence](docs/evidence/packaging-validation-2026-10-29.md).
- **44/45 targeted private checks passed**, including cat-grab ownership,
  successive hooks, coaster behavior, shared barrels, honey terrain, animal
  transitions and developer level completion.
- The original `check_coop_item_input.py` check failed its blank-SRAM P1 pickup
  setup before reaching the ownership assertions. This was already documented
  on 2026-10-22; it is not counted as a pass. Copied-save water/item ownership
  and synthetic ownership checks pass. The setup was subsequently repaired:
  both players now pass all item-input assertions, with native chest identity
  checked too. See [follow-up evidence](docs/evidence/item-input-validation-2026-10-29.md).
- Corrected outdated test approaches for staggered pole entry and water entry,
  and replaced an obsolete Rattly-deletion assertion with native unmounted-idle
  validation. Initial failed attempts and rechecks remain in the evidence.
- Hidden desktop replays completed for the latest fixes; gameplay-state
  assertions use the headless regression runner.
- Physical controller button behavior and rumble sensation still need owner
  acceptance; logical routing/device-assignment checks do not establish them.
- Some older optional private tests still assume a three-second Select hold.
  For example, the Rattly script reaches its rescue-timing assertion and fails
  against the current two-second compiled setting; its earlier hanger checks pass.
- Special-kart first-contact ownership has synthetic coverage and controlled
  P2-front-seat replay coverage; a fresh input-only boarding approach from the
  level entrance remains unverified.
- Full-game traversal, every enemy/terrain arrangement, concurrent capture by
  multiple cats and final ending cinematics remain unverified.

## Supporting documentation

- [Changelog](CHANGELOG.md)
- [Implementation journal](docs/IMPLEMENTATION_JOURNAL.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Hardware notes](docs/HARDWARE_NOTES.md)
- [Roadmap](docs/ROADMAP.md)
- [Co-op tuning](config/coop_tuning.cfg)
- [Developer configuration](config/developer.cfg)
- [Controller mapping provenance](third_party/sdl_gamecontrollerdb/PROVENANCE.md)
