# Experimental Adventure co-op

Two local players share one Archipelago slot in retail Trophy Races. The Adventure hub currently stays on Player 1's screen.

1. In the Adventure hub, press Start on a connected gamepad other than Player 1's. Player 2 joins immediately as their last racer for the slot, or the first eligible racer. A short notice names the racer.
2. Press Start again on Player 2's pad to open their menu. Choose Change Racer, Drop Out or Back. In Change Racer, use Left and Right to browse, Cross to confirm, and Triangle to go back. Both players see the portraits and can pick the same racer. Older seeds without the character phase offer the same roster to both players.
3. Player 1's Start opens the usual pause menu. Choose Gameplay Options and look for Co-op Rules. Lenient is the default. Switch presets in a loaded hub, outside a cup.

| Trophy Race | Lenient | Hard |
|---|---|---|
| Win | Either player finishes first | The players finish first and second, in either order |
| Podium checks | Better finishing place | Worse finishing place |
| Local DeathLink to the partner | Mask reset | The seed's DeathLink mode and send conditions |

The slot remembers the racer and rules in its `ctr_coop_<slot>` Archipelago Data Storage key. Values from older co-op builds still restore the racer and default to Lenient. Press Start to join in each new Adventure session. When disconnected, joining and dropping out still work. Saving a racer change or rule change requires a connection.

Retail Trophy podium checks require the observed race result. The client retries earned checks when it reconnects to the same room. If an older client left a Trophy checked without sending its podium checks, finish the race again to earn the missing podium checks.

Join, drop out and racer changes are hub actions. A replacement controller can press Start to take an unplugged player's seat in the hub. The active seats use SDL instance identities. If Player 2 unplugs during a race or while Retry is loading, Retry can reload the track as solo and retain the original hub as the return destination.

Retail Trophy Races use split screen and load up to six AI opponents. The loader uses the stock four-opponent pack when the two additional models cannot load. Both humans must finish before the race ends. Either human can earn item-box, Itemsanity, letter, Hit Character, Wumpa and lap checks. Partner hits are excluded. Weapon ownership, progressive stats, shortcut and surface restrictions apply to both karts, with each racer's own stat row. Received and starting Wumpa reaches both karts. Turbo and useful grants prefer Player 1, then Player 2 when the first slot is busy.

Custom tracks, Cortex Vortex, cups, bosses, CTR Challenges, Relic Races and Crystal Challenges run solo. Player 2 can race again at the next supported Trophy Race.

## Remaining work before broader use

Ordinary weapon-crate, Wumpa-crate and loose-fruit checks also accept either human and share their checked set. Their instance-count verification remains in place; a 2P level whose pickup counts differ from the room's verified record refuses those checks instead of assigning another object's code. Per-track 2P pickup compatibility still needs inspection.

The special AP encounter roster, including guest racer swaps and held-boss exclusions, uses the solo loader. Physics-based trap restrictions recognize both humans, but instant traps and some camera effects still target Player 1. The tracker, box numbers, blue-fire palette and podium presentation need a split-screen pass.

The two-player Adventure design is based on Coding Bob's [CTR Native Expanded](https://github.com/thecodingbob/ctr-native), used as a reference for the single-player hub and multiplayer race boundary.
