# The v1.40 milestone

Started 2026-11-19, from what the owner said to the design session that evening. (owner) marks what the owner
said; a dagger (†) marks what the design session proposes and the owner has not ruled on.

**The scope is not fixed** (owner): "things have been pretty fluid in this project. So scope might grow or
shrink." This page is kept to what is agreed on the day.

## What v1.40 is (owner)

"The big features for v1.40 are underlying hardware improvements (specifically the RW unit and changes to the
Ingress/Egress unit). On the software side, we want to get the file system and a small shell working."

| part | in v1.40 | where it stands today |
|---|---|---|
| **the RW unit** | hardware, improved | v1.36's RW engine, unchanged but for its place; nothing in v1.37's proof runs uses it. Not designed: `v1.4_wip/planning/TO-ADD.md`, entry 3, with the compute tile's two regimes for a full router queue. Tony expects to redesign it in full |
| **Ingress/Egress** | hardware, changed | asked for so far: the statistics for each link, with the names `lvds_parent`, `lvds_child` and `chain_end` (`TO-ADD.md`, entry 1), and the watchdog's fix (entry 2) |
| **the file system** | software, working | deferred since v1.35 (the specification's item 119). v1.37 fixes what the hardware sees of a file (23.5); everything above that is not designed |
| **a small shell** | software, working | new on 2026-11-19; nothing written |

## How the work is done (owner)

**The design session manages the other sessions; it does not, in the main, build.** "Your job is not primarily
to implement the changes, rather it's to manage other sessions." It writes their briefs, reviews their plans and
reports, merges their branches, and gives the owner messages ready to paste (`HANDOVER.md`, section 8).

## Carried in from v1.37

Owed, and not part of the four features (`HANDOVER.md`, sections 3 and 4):

- a soak campaign on the cache's controller, before Tony is told the cache is ready for RTL;
- WORKER8's long runs of the link (Quests 238 to 242, parked), the owner's to release;
- a test for the firmware's wait on the block's counter;
- not ruled: the watchdog's reload on a `RELEASE`; whether the twelve counters for each link are final; whether
  the design documents move from `v1.4_wip/` to this folder.

## The sessions

None yet.
