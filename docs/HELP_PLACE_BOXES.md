# Help place item boxes

Thanks for helping. This guide is for players who want to suggest new spots for
Archipelago item boxes. You drive to a spot, press a button, and the game saves
that spot to a file. You export the tracks you worked on, send us the files, we
check every spot, and the good ones go into a later release.

You need a controller (a Steam Deck works), or a keyboard with a number pad.

## 1. Pick a client

**Your normal CTR Archipelago client** has Box Author Mode built in. Use it.
Nothing changes for your seeds: the mode is off every time the game starts, and
your rooms connect as before.

**The box authoring download** still exists for players who want a separate
folder. The release page has it as
`ctr-archipelago-vX.Y.Z-box-authoring-windows-x86.zip` (Windows) or
`ctr-archipelago-vX.Y.Z-box-authoring-linux-x86.tar.gz` (Linux and Steam Deck).
Unzip it into a **new, empty folder**, add your disc image to its `assets`
folder (see `SETUP.md`, step 2) and start `ctr_native_ap_authoring`. The window
title says `BOX AUTHORING BUILD`. It never connects to Archipelago and keeps
Box Author Mode on between starts. Everything below works the same in it.

## 2. Turn on Box Author Mode

Main menu: `OPTIONS -> Authoring -> Box Author Mode`, set it to ON.

In the normal client, Box Author Mode and a room never share a session:

- While you are connected to a room (or a Play Solo game runs), turning it on is
  refused, and the page says why. Press again to leave the room and turn it on.
- While it is on, Connect, room links and Play Solo are refused. The Connection
  page's status says `Box Author Mode is on`. Turn the mode off first.
- It never sends checks and never changes your seed, saves or memory cards.
  It only writes the box files named in section 6.

Start an Arcade single race on one of the 18 race tracks (custom tracks:
see section 5). Skip Time Trial and
relic races: their version of the track has no crate model, so the boxes do not
show as crates there. The top left of the screen shows
`BOX AUTHOR  <TRACK>  <N> HERE`: the track name and how many boxes it has now.
You see every box that is already planned, including the ones in the current
release, so you can tell where the gaps are.

Author mode pauses in boss races. That is on purpose: use the normal race on
the same track instead.

## 3. Buttons

Every action is on the controller. Hold Select (View on a Steam Deck) and press
the second button.

| Controller | Keyboard | What it does |
|---|---|---|
| Select | Numpad 9 | Put a box where your kart is right now |
| Hold Select, press Square or Circle | | Select the previous or next box on this track |
| Hold Select, press L1 | Numpad 0 | Delete the selected box (the newest one when none is selected) |
| Hold Select, press Cross | | Move the selected box (see below) |
| Hold Select, press R1 | Numpad . | Save now and write this track's list to the log |
| Hold Select, press Triangle | | Export submission files (section 6) |

The top left shows the buttons as `SELECT DROP  +L1 DELETE  +R1 SAVE` and
`+SQ +O PICK  +X MOVE  +TRI EXPORT`. The selected box shows as
`BOX <K> OF <N>` with its position.

**Moving a box.** Hold Select and press Cross. The top left says `MOVING`. Now
the D-pad slides the box (left and right along one axis of the track, up and
down along the other), L1 lowers it and R1 raises it. Each press moves it a
little and saves. Press Select when you are done. While moving, the D-pad and
L1 and R1 do not steer or hop, so stop the kart first.

On the controller, the box is dropped when you let go of Select, so holding
Select for a second button never drops one as well. While you hold Select, the
second button does not reach the kart (no hop, no item, no brake). The buttons
work in a race only, not in the pause menu. With the default keyboard layout the
space bar is Select, so it drops a box too.

The box goes on the ground under your kart, facing the way you face. The file is
saved after every change, so a crash or closing the game does not lose your
work.

Delete removes the newest box on the track you are on when no box is selected,
even if that box came with the release. Select a box first if you want to
delete a different one.

## 4. What makes a good spot

- On the driving line, or at the landing of a jump a normal player can make.
- Worth a small detour at most. A box should never need a wall clip, a glitch or
  a secret route.
- Not on top of a normal weapon crate or Wumpa crate, and not inside a wall or
  under the floor.
- A track can hold at most 15 boxes. If the counter already says 15, pick a
  different track, or tell us which box your spot should replace.

We test every spot for reachability before it goes in, so a spot that turns out
to be too hard is simply left out. Suggest freely.

## 5. Custom tracks

Both clients can race community tracks from Project Saphi, so you can place
boxes on them before they are in a release. One custom track at a time.

1. Download the track. In the box authoring download: main menu
   `OPTIONS -> Custom Content`, the Track Manager. In the normal client: the
   custom track screen under `OPTIONS`.
   - **Browse** lists the Saphi catalogue. Pick a track and press Cross to
     download it. Square shows its other versions. R2 searches, L2 refreshes.
   - **Installed** lists what you have. L1 and R1 switch between the two.
   - Downloaded tracks are checked and stored under `assets/tracks/packages`.
2. Start an Arcade single race for **one player**, or a Time Trial. Pick your
   racer (and in Arcade the difficulty), and at the track list press Right to go
   past the normal tracks to the **Custom** pages.
3. Pick the track, press Cross, then Cross again to start. The race uses the
   lap count Saphi lists for the track, or 3 laps when it lists none. It plays
   the track's own music when Saphi has it for the track. Otherwise, or when the
   music is too large to fit next to the eight racers' sounds, you hear Crash
   Cove's music. A track downloaded before Saphi added its music shows
   `* Update` in the Track Manager.
4. With Box Author Mode on, the top left shows `BOX AUTHOR  <TRACK NAME>  <N>
   HERE`. The buttons are the same as in section 3.

Every installed track shows on the Custom pages. A track that cannot race in
the mode you picked is dimmed and says why when you highlight it. Most Saphi
tracks are made for Time Trial only: they have no paths for the computer
racers, so they cannot run an Arcade race with seven opponents. The Arcade page
then says `No AI paths. Use Time Trial.` Other reasons are `Grid: 3 of 8` (fewer
than eight start positions), `No lap checkpoints` and `Files failed checks`. In
the Track Manager, the Installed tab shows `Arcade`, `Time Trial` (Time Trial
only) or `No races` for each track, with the reason under it.

In Time Trial on a custom track, your best times and your best ghost are saved
in the `custom-records` folder next to the game, never on the memory
card. The next Time Trial on that track shows the best times and races your
saved ghost. A new version of the track's files, or a different lap count,
starts fresh. The track has no N. Tropy or Oxide ghost, and there is no Save
Ghost option: the ghost of a run that beats your saved one is saved by itself.

If the top left says `CUSTOM TRACK NOT LOADED`, the
track did not load. Go back to the menu and start it again.

Boxes on custom tracks go to their own file,
`ap-box-placements-custom-authoring.json`, next to the game. Each box
records which track and which version of its files it belongs to, so boxes
never mix between tracks or between versions of a track. The file for the
normal tracks is not changed by this.

## 6. Export and send us the files

In a race, hold Select and press Triangle. The game writes one file per track
into the `box-submissions` folder next to the game (the folder with
`ctr_native_ap` in it), and the top left says `EXPORTED <N> TRACKS`. Every
custom track with a box gets a file, and every normal track whose boxes differ
from the release. A file holds the track's name, which version of the track it
is (for a custom track: its package id, version and file checksums), and its
boxes. Export again after more changes: the files are written again.

Nothing is uploaded. To send them, open a
[Box placement submission](https://github.com/dowlle/ctr-native-ap/issues/new?template=box_placement.yaml)
issue, drag the files from `box-submissions` into the form, and say which
tracks you worked on. If GitHub refuses a file, put it in a zip first. You need
a free GitHub account for this.

The full tables are kept as well, also next to the game:
`ap-box-placements-authoring.json` (normal tracks, every box including the
planned ones) and `ap-box-placements-custom-authoring.json` (custom tracks). The
normal client never reads them for a seed, and it never writes your
`ap-box-placements.json`, if you have one.

If the game crashes, attach `ctr-ap-crash.txt` and `ctr-ap.log` from the same
folder too.
