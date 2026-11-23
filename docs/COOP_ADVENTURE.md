# Experimental Adventure co-op

Two local players share one Archipelago slot. Both can drive around the Adventure hubs, race together in retail Trophy Races and collect crystals together in Crystal Challenges.

1. In the Adventure hub, press Start on a connected gamepad other than Player 1's. Player 2 joins as their last eligible racer for the slot, or the first eligible racer. The hub briefly reloads at Player 1's position and puts both karts side by side.
2. Press Start again on Player 2's pad to open their menu. Choose Change Racer, Drop Out or Back. In Change Racer, use Left and Right to browse, Cross to confirm, and Triangle to go back. Both players see the portraits and can pick the same racer. Older seeds without the character phase offer the same roster to both players. Changing racer and dropping out reload the hub at Player 1's position.
3. Player 1's Start opens the usual pause menu. Choose Gameplay Options and look for Co-op Rules. Lenient is the default. Switch presets in a loaded hub, outside a cup.

| Rule | Lenient | Hard |
|---|---|---|
| Win | Either player finishes first | The players finish first and second, in either order |
| Podium checks | Better finishing place | Worse finishing place |
| Crystal Challenge | Shared crystal count, full solo time limit | Shared crystal count, 60% of the solo time limit |
| Local DeathLink to the partner | Mask reset | The seed's DeathLink mode and send conditions |

The slot remembers the racer and rules in its `ctr_coop_<slot>` Archipelago Data Storage key. Values from older co-op builds still restore the racer and default to Lenient. Press Start to join in each new Adventure session. If a two-player Trophy entry reports that the rules are still syncing, leave the pad and try again after sync. Solo entries can proceed while it finishes. A queued edit shows that the save is pending. If the client reconnects and the room still has a different value, the hub reports the unsaved edit and restores the room's value, or the first eligible racer if the room had no saved racer. Change the setting again to retry.

Retail Trophy podium checks require the observed race result. The client retries earned checks when it reconnects to the same room. If an older client left a Trophy checked without sending its podium checks, finish the race again to earn the missing podium checks.

Join, drop out and racer changes are available when the hub is free of scenes and loads. A replacement controller can press Start to take an unplugged player's seat in the hub. The active seats use SDL instance identities. If Player 2 unplugs during a race or while Retry is loading, Retry can reload the track as solo and retain the original hub as the return destination. Builds using the retail memory budget refuse hub co-op with a notice. The full memory build with custom-track support enables it.

Either player can drive onto a warp pad. The pad prompt and race type menu use that player's view and controller. A racer-locked pad seats Player 1 as its required racer. When either player drives into a neighboring hub, the other kart moves alongside. Player 1 operates doors, boss garages, Wumpa Shop, the save box, mask hints and the tracker. Player 2 waits in place while those scenes run.

Retail Trophy Races use split screen and load up to six AI opponents. The loader uses the stock four-opponent pack when the two additional models cannot load. Races with a special AP encounter roster use the solo loader to keep the advertised opponents. Both humans must finish before the race ends. Either human can earn item-box, Itemsanity, letter, Hit Character, Wumpa and lap checks. Partner hits are excluded from Hit Character checks. Weapon ownership, progressive stats, shortcut and surface restrictions apply to both karts, with each racer's own stat row. Received and starting Wumpa reaches both karts. Turbo and useful grants prefer Player 1, then Player 2 when the first slot is busy.

For Crystal Challenges, drive either kart into the arena's pad. Both views show the team's count and the same timer. Either kart can earn individual crystal checks. Collect all the crystals in time and confirm the result to earn the shared completion check. Either controller can confirm, retry or choose Exit to Map from the results. If Player 2 disconnects during play, the arena reloads as a solo attempt with the standard time limit. Crystal checks earned before that reload stay checked.

Custom tracks, Cortex Vortex, cups, bosses, CTR Challenges and Relic Races run solo. The game shows a sit-out notice and returns to the split-screen hub afterwards.

## Remaining work before broader use

Ordinary weapon-crate, Wumpa-crate and loose-fruit checks also accept either human and share their checked set. Their instance-count verification remains in place; a 2P level whose pickup counts differ from the room's verified record refuses those checks instead of assigning another object's code. Per-track 2P pickup compatibility still needs inspection.

Physics-based trap restrictions recognize both humans. In Crystal Challenges, direct hazard and weapon-slot traps use each kart's own protection and readiness. If the pair retries, those traps carry over only for a kart that still owes the effect. The tracker, box numbers, blue-fire palette and podium presentation need a split-screen pass.

The two-player Adventure race design is based on Coding Bob's [CTR Native Expanded](https://github.com/thecodingbob/ctr-native).
