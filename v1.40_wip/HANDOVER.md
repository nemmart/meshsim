# Hand-over to the next design session

Written 2026-11-19 by the design session that took v1.37 from design notes to its freeze. The owner: "We've had
tons of compaction events, so it's time for new sessions." Everything ruled is in the files this page names;
nothing here is new, it is where to look and what is still owed. The prompt for the new session is at the end.

## 1. Where things stand

- **v1.37 is frozen** (2026-11-19) and on `main`: `v1.37/`. The final pass on the runner was green (Quest 269).
  It is v1.36 with the full system round it: the host link, Ingress/Egress, the Updater, the cache and the AXI
  arbiter, the master tile's real firmware, the reply at a segment's end, a chain of ASICs.
- **Tony has been told it is frozen** and to read `v1.37/RELEASE-NOTE-v1.37.md` first: what is asked of him,
  what is a question, what is his to settle, what is for his information.
- **The version numbers** (owner): **Tony has reserved v1.38 and v1.39. This side's next version is v1.40,** and
  its work folder is this one, `v1.40_wip/`.
- **The design documents are still in `v1.4_wip/planning/`** (v1.4 and v1.40 are the same version by two
  spellings). v1.37's frozen pages cite them there by name, and its document check reads that folder, so they
  were not moved. Whether they move here, and how v1.37's check is then kept honest, is for the owner.

## 2. What to read, in order

| paper | what it gives |
|---|---|
| `v1.37/RELEASE-NOTE-v1.37.md` | one page: v1.37 as Tony sees it |
| `v1.4_wip/planning/TO-ADD.md` | what the owner wants next |
| `v1.4_wip/planning/RTL-CHANGE-REQUESTS.md` | every request and comment for Tony, with the owner's words on each |
| `v1.4_wip/planning/DESIGN-NOTES-2026-11-16.md`, sections 34 to 41 | how v1.37 was made and every ruling on the way; section 39 has the rulings before the freeze, 41 the list for Tony |
| `v1.37/README.md`, `v1.37/OPEN-v1.37.md` | the frozen folder's guide, and what the simulator owes |
| `v1.37/spec/` | the specification: `machine-spec.md` (chapter 26 is the open items), `host-link.md`, `host-frames.md`, `master-firmware.md`, `CHANGES-v1.36-to-v1.37.md` |
| `v1.4_wip/planning/CONTROLLER-SIZING.md` | the controllers' sizes, and why fixed logic |
| `v1.37/planning/updater/` | the project's papers: the milestone, every session's brief, notes and report |

## 3. What is owed

Nothing here held the freeze. The owner (2026-11-19): "we can work on the soak campaigns in the next day or two
and it will be fine."

| owed | where it stands |
|---|---|
| **a soak campaign on the cache's controller** | a large random campaign, of the kind the link had, before Tony is told the cache is ready for RTL. The owner's worry: "I don't want to tell Tony the cache is ready for RTL, and then say haha, just kidding a week later." No plan written yet: the next session brings one |
| **WORKER8's long runs of the link** | Quests 238 to 242, parked in Quest's `tasks/hold/`; the owner's to release, for an idle night (spec item 192) |
| **a test for the firmware's wait on the block's counter** | the wait is in the firmware and untested (design notes 39; `v1.37/OPEN-v1.37.md`). The owner: "The firmware should wait. That eliminates the race" |

## 4. What is not ruled

- **Whether the watchdog keeps its reload on a `RELEASE`** (`TO-ADD.md`, entry 2). Asked twice, not answered.
- **Whether the list of twelve counters for each link is final** (`TO-ADD.md`, entry 1, marked †).
- **Whether the design documents move from `v1.4_wip/` to this folder** (section 1).

## 5. What is next

`TO-ADD.md` is the list: the link's statistics (words 96 to 107 and 112 to 123) with the names `lvds_parent`,
`lvds_child` and `chain_end`; the fix to the watchdog; and **the next project, the RW peripheral,** with two
regimes for a full router queue on a compute tile: an error, or flow control. Nothing of the RW peripheral is
designed. Tony expects to redesign the RW engine in full.

The way of working that held for v1.37, and the owner's choice ("follow the tried and true"): **the
specification first,** in one long session; then the simulator, the firmware and the tests; reviews of the
specification by the sessions that wrote the code it mirrors.

## 6. How the owner works

His standing instructions, in his words where they are his:

- "For all sessions, please do not start writing code or changing docs unless we agree on the plan and you have
  a 'go ahead' from me." A question is not a go-ahead: "ok, what's the command...?" asked for the command, not
  for it to be run.
- Short exchanges. No paragraphs of agreement: "Most of the time there isn't much new content in what you're
  sending, which just slows down the conversation."
- Questions and issues **one at a time.**
- "If there are things we have discussed about the design... let's get it into the files." A ruling is recorded
  the day it is made, with (owner) beside it; what was proposed and not ruled carries a dagger (†).
- **A change to hardware Tony has built is written down and asked of Tony** (`RTL-CHANGE-REQUESTS.md`), not made
  on this side. Units Tony only pencilled in are this side's to specify.
- **A frozen folder is never edited** but for an errata file. v1.37's release note was added as a new file on
  the same footing.
- Runner tasks short and to the point: "These big runs seem really pointless."

## 7. Things this session got wrong, so the next does not

- **Flow control.** "There are regimes on the mesh. Some need flow control, some don't." The mesh's links have
  none; nothing in v1.37 needs it on the line between the special tiles. Do not add it unasked.
- **Ports.** The special tiles are 64, 65, 66: the line is on **east and west,** id ± 1. A peripheral's DMA is on
  **south, the +8 port.**
- **Names.** 128 bits is "a 128-bit word", not a "beat". The links are parent and child.
- **Precedence.** For the cache's controller, the Updater's controller and the link end, the code is the
  authority and the specification mirrors it. For everything else the design documents lead.
- **Error recovery is not designed** and is not to be improvised: one reset net, and open item 181.
- **Check before proposing a register's place or name:** words 64 to 67 were taken when 64 was proposed for the
  counters, and `WATCHDOG` already existed when a new one was proposed.

## 8. The mechanics

- **This session is the design session and the integrator.** The owner starts other sessions and relays their
  messages. This session writes their briefs, reviews their plans and reports, merges their branches into `main`
  (`git merge --no-ff`, then pull and push), and gives the owner messages ready to paste.
- **A brief for another session always says:** never write a token into a file; never `pkill -f`, use `pgrep`
  and kill by PID.
- **The runner** takes tasks from the Quest repository (its `PIPELINE.md`): 16 CPUs, 62 GB, a limit of 3 hours
  30 minutes a task, no RISC-V toolchain. Pull and read `NUMBERS.md` just before claiming a number, and push the
  claim before the task. Never push to Quest while the runner has result commits it has not pushed. A killed task
  is retried up to three times unless it is first moved to `tasks/hold/`.
- **v1.37's task generator for the runner** is `v1.37/tools/v137-check/`.
- **Checks before a push that touches v1.37:** `make doccheck` and `sh tools/check_copies.sh` in `v1.37/`.

## 9. The prompt for the new session

> You are the design session and integrator for meshsim, a simulator and specification for a 64-tile mesh
> processor ASIC that proves the Jolt zkVM. The repository is `nemmart/meshsim`; the runner's is `nemmart/quest`.
>
> Start by reading `v1.40_wip/HANDOVER.md` in meshsim, then the papers its section 2 lists, in order. v1.37 is
> frozen; the next version on this side is v1.40.
>
> Rules, all of them standing: do not write code or change a document until we have agreed a plan and I have
> said go ahead, and a question from me is not a go-ahead. Keep replies short; do not write paragraphs agreeing
> with me. Ask questions one at a time. Record every design ruling in the files the day it is made. Changes to
> hardware Tony has built are written in `RTL-CHANGE-REQUESTS.md` and asked of him, never made on our side. A
> frozen folder is never edited. Never write a token into a file or repeat one in a reply.
>
> When you have read in, tell me in a few lines what you understand to be owed and what is next, and wait.
