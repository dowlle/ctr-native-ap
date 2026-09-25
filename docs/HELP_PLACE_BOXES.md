# Help place item boxes

Thanks for helping. This guide is for players who want to suggest new spots for
Archipelago item boxes. You drive to a spot, press a key, and the game saves that
spot to a file. You send us the file, we check every spot, and the good ones go
into a later release.

You need a controller, or a keyboard with a number pad.

## 1. Install the box authoring download

The release page has a separate download for this:
`ctr-archipelago-vX.Y.Z-box-authoring-windows-x86.zip` (Windows) or
`ctr-archipelago-vX.Y.Z-box-authoring-linux-x86.tar.gz` (Linux and Steam Deck).

- Unzip it into a **new, empty folder**. Do not unzip it into your normal
  CTR Archipelago folder.
- Add your disc image to its `assets` folder, the same way as for the normal
  client (see `SETUP.md`, step 2).
- Start `ctr_native_ap_authoring.exe` (Linux: `ctr_native_ap_authoring`). The
  window title says `BOX AUTHORING BUILD`.

This build does not connect to Archipelago, so do not play seeds with it. It
starts with its own settings, so your normal client and its saves are not
touched.

## 2. Turn on Box Author Mode

Main menu: `OPTIONS -> Authoring -> Box Author Mode`, set it to on.

Start an Arcade single race on one of the 18 race tracks (custom tracks:
see section 5). Skip Time Trial and
relic races: their version of the track has no crate model, so the boxes do not
show as crates there. The top left of the screen shows
`BOX AUTHOR  <TRACK>  <N> HERE`: the track name and how many boxes it has now.
You see every box that is already planned, including the ones in the current
release, so you can tell where the gaps are.

Author mode pauses in boss races. That is on purpose: use the normal race on
the same track instead.

## 3. Keys

| Controller | Keyboard | What it does |
|---|---|---|
| Select | Numpad 9 | Put a box where your kart is right now |
| Hold Select, press L1 | Numpad 0 | Remove the newest box on this track |
| Hold Select, press R1 | Numpad . | Save now and write this track's list to the log |

The top left shows the controller keys as `SELECT DROP  +L1 UNDO  +R1 SAVE`.

On the controller, the box is dropped when you let go of Select, so holding
Select for L1 or R1 never drops one as well. While you hold Select, L1 and R1
do not make the kart hop. The controller keys work in a race only, not in the
pause menu. With the default keyboard layout the space bar is Select, so it
drops a box too.

The box goes on the ground under your kart, facing the way you face. The file is
saved after every change, so a crash or closing the game does not lose your
work.

Numpad 0 removes the newest box on the track you are on, even if that box came
with the release. Use it only right after a drop you want to take back.

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

The authoring download can also race community tracks from Project Saphi, so
you can place boxes on them before they are in a release. One custom track at
a time.

1. Main menu: `OPTIONS -> Custom Content`. This is the Track Manager.
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
   HERE`. The keys are the same as in step 3.

Every installed track shows on the Custom pages. A track that cannot race in
the mode you picked is dimmed and says why when you highlight it. Most Saphi
tracks are made for Time Trial only: they have no paths for the computer
racers, so they cannot run an Arcade race with seven opponents. The Arcade page
then says `No AI paths. Use Time Trial.` Other reasons are `Grid: 3 of 8` (fewer
than eight start positions), `No lap checkpoints` and `Files failed checks`. In
the Track Manager, the Installed tab shows `Arcade`, `Time Trial` (Time Trial
only) or `No races` for each track, with the reason under it.

In Time Trial on a custom track, your best times and your best ghost are saved
in the `custom-records` folder next to the authoring exe, never on the memory
card. The next Time Trial on that track shows the best times and races your
saved ghost. A new version of the track's files, or a different lap count,
starts fresh. The track has no N. Tropy or Oxide ghost, and there is no Save
Ghost option: the ghost of a run that beats your saved one is saved by itself.

If the top left says `CUSTOM TRACK NOT LOADED`, the
track did not load. Go back to the menu and start it again.

Boxes on custom tracks go to their own file,
`ap-box-placements-custom-authoring.json`, next to the authoring exe. Each box
records which track and which version of its files it belongs to, so boxes
never mix between tracks or between versions of a track. The file for the
normal tracks is not changed by this.

## 6. Send us the file

The file is `ap-box-placements-authoring.json`, in the same folder as the
authoring exe. It holds every box, the planned ones plus yours, which is what we
need. If you placed boxes on custom tracks, also send
`ap-box-placements-custom-authoring.json` from the same folder.

Open a
[Box placement submission](https://github.com/dowlle/ctr-native-ap/issues/new?template=box_placement.yaml)
issue, drag the file (or both files) into the form, and say which tracks you
worked on. If
GitHub refuses the file, put it in a zip first. You need a free GitHub account
for this.

If the game crashes, attach `ctr-ap-crash.txt` and `ctr-ap.log` from the same
folder too.
