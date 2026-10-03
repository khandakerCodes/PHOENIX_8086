# Lab Solutions

One worked solution per lab, as a patch against the kernel. Try the lab first; these are for checking your work or for instructors.

| Lab | Patch | What it does |
| --- | --- | --- |
| 1. Add a system call | `01-system-call.patch` | Adds `gettid` (13h), the `px_gettid` wrapper, a `whoami` program, a self-test and the documentation row |
| 2. Change the scheduler | `02-scheduler-round-robin.patch` | Option A: pure round-robin, with priorities and aging removed |
| 3. Add a keyboard layout | `03-keyboard-layout.patch` | A Spanish layout with its self-tests |

To try one:

```sh
git apply docs/labs/solutions/01-system-call.patch
make test
git apply -R docs/labs/solutions/01-system-call.patch     # undo
```

Each patch was applied, built and run before it was committed, and CI checks that every patch still applies to the current source. A patch that stops applying after a kernel change needs regenerating.

Notes on the results:

* **Lab 1:** `run whoami.bin` prints the same thread ID the shell reports when it starts the program.
* **Lab 2:** with pure round-robin the whole integration test still passes, including the sleep-accuracy checks, because few threads are runnable at once. The difference shows in the measurements the lab asks for: the three demo threads get equal shares whatever their priority, and the shell answers a keystroke only when its turn comes round.
* **Lab 3:** scan code `27h`, the key right of L, gives `ñ`; AltGr with the 2 key gives `@`.
