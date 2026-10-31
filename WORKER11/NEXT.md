# Pending for WORKER11's next prompt (integrator)

> Quest task numbers: claim yours in the Quest repository's NUMBERS.md before writing a task (pull, append the next number
> with your session and task, commit, push; if the push is rejected, pull and take the next). The next free number was 081
> on 2026-11-01.

> v1.2 is frozen (v1.2/, tag v1.2), and main now contains v1.2-meshsim and WORKER10's v1.25 spec (wip/spec/ is v1.25).
> Merge main into v1.25-meshsim (merge, don't rebase), so your branch carries the final v1.2 simulator and the v1.25 spec;
> when your work is done, the integrator merges your branch into main.

> v1.25's definition of done now spells out "the software" (DECISIONS-v1.25): besides replay_ctl.c, the replayer recipe
> (wip/sw/recipes/rcp_replay.c, recipe 26 in RECIPES.md) and its test-side template_lookup need the normalized key; that's in
> your scope, since you own the replayer's software. Regenerate the book with wip/sw/recipes/gen_book.py and keep
> check_book.py passing.
