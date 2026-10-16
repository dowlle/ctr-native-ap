# Clocked Out

Clocked Out is item 35010206, with stable trap key `clocked_out`. Its default
weight is 3. Set `trap_weights.clocked_out` to 0 to remove it from filler draws.
The existing Trap Fill Percentage option controls the total number of traps.

After the ordinary one-second warning, the receiving player spins and receives
the retail clock slowdown, wobble, weapon block and blur for 7680 ms. Wumpa
count does not change this duration. The retail rank-based slowdown is retained.
Other drivers are unaffected. Time Trial and Relic Race are eligible; the
Adventure hub is excluded.

Shield, mask, invincibility, existing clock and incompatible kart state make a
copy wait. A failed hazard call retries without spending it. Copies serialize
until the native clock timer expires. Pausing freezes the engine timer and
blocks retries. Pending unapplied copies survive map boundaries; an applied
copy is not replayed. Self-inflicted trap hits do not send DeathLink.

Forced Use and Item Reroll wait for a clocked held slot to become usable. Use
`debug_trap=clocked_out` with developer keys enabled for the ordinary receive
and scheduler path. Steam acceptance remains pending.

Refs #455. The schema-18 identity map appends this effect without changing its
version or any previously registered trap ID, key or native enum value.
