# Experimental Adventure co-op

This draft implements the first retail Trophy Race slice of two local players on one Archipelago slot. It is not the complete co-op milestone and has not been accepted in a two-controller play test.

In the hub, open Options, Gameplay, Adventure Players and select 2P LENIENT with two controller slots connected. Player 2 Racer cycles through unlocked racers. Both choices are stored together in the slot's `ctr_coop_<slot>` data-storage key: engine racer ID 0 to 15, plus 16 when co-op is selected. Missing or invalid storage starts solo. Global configuration does not carry a choice into another room.

Mute When Unfocused is on Video so Gameplay keeps both co-op rows and Update Check within its eleven visible rows.

On older seeds without a character phase, this first slice uses the conservative unlock predicate for P2: the eight vanilla Adventure racers plus P1's current racer. P1's existing wider legacy browse picker stays unchanged. Modern seeds with character unlocks disabled still offer all sixteen racers to P2.

The hub stays under player 1's control. Standalone retail Trophy Races seat two humans and four bots using the retail split-screen loader. Both humans must finish before the race ends. Either human finishing first wins the Trophy; podium checks use the better position. Either human can earn item-box, Itemsanity, letter, Hit Character, Wumpa and lap checks. Partner hits are excluded. Weapon ownership, progressive stats, shortcut and surface restrictions apply to both karts, with each racer's own stat row. Received and starting Wumpa reaches both karts; Turbo and useful grants prefer player 1, then player 2 when the first slot is busy.

Changing the player count or P2 racer is refused during a race, cup or load. Enabling needs connected controller slots 1 and 2. Switching back to solo in the hub remains possible after the second controller disconnects. A remembered co-op preference falls back to solo at race entry without a second controller or an unlocked P2 racer.

Custom tracks, Cortex Vortex, cups, bosses, CTR Challenges, Relic Races and Crystal Challenges remain solo in this slice. Selecting one preserves the co-op preference for the next supported Trophy Race. Proper custom-track co-op remains part of the later milestone.

## Remaining work before broader use

Ordinary weapon-crate, Wumpa-crate and loose-fruit checks also accept either human and share their checked set. Their instance-count verification remains in place; a 2P level whose pickup counts differ from the room's verified record refuses those checks instead of assigning another object's code. Per-track 2P pickup compatibility still needs inspection.

The special AP encounter roster still uses the solo loader. Two-player races currently use the retail four-bot pool, without the guest racer swaps or held-boss exclusions. P2 fruit is not yet included in the shared Wumpa wallet's finish and exit accounting. Trap scheduling and DeathLink targeting still need the full pair treatment, including local DeathLink without another AP player. Physics-based trap restrictions recognize both humans, but instant traps and some camera effects still target P1. The tracker, box numbers, blue-fire palette and podium presentation need a split-screen pass. Hard difficulty and the remaining event types are not implemented.

The two-player Adventure design is based on Coding Bob's [CTR Native Expanded](https://github.com/thecodingbob/ctr-native), used as a reference for the single-player hub and multiplayer race boundary. The AP result policy here is Lenient: either human wins.
