# Lab 3 — Add a Keyboard Layout

## Goal

Add a keyboard layout to the kernel and test it.

You will learn how a keyboard driver turns scan codes into characters, and what a device driver's data tables look like.

## Background

A PC keyboard does not send characters. It sends a *scan code* that identifies which key moved: a make code when the key goes down and the same code with bit 7 set when it comes up. Scan codes describe key *positions*, so the key to the right of Tab sends `10h` whether it is labelled Q (US), A (French) or anything else. The layout is entirely the driver's business.

Read `kernel/keyboard.c`:

* `us_normal` and `us_shift` map each scan code to a character for the US layout.
* Every other layout is a list of `key_override_t` entries: one line per key that differs from US, giving the character without Shift, with Shift, and with AltGr.
* `kb_translate` looks a scan code up in the current layout's list and falls back to the US tables.
* Characters above 7Fh are in code page 437, the character set of the PC's text mode.

## Steps

1. **Pick a layout** that is not there yet: Spanish, Italian, Swedish, Dvorak, or your own.
2. **Find its key positions.** Use a diagram of the layout and a table of scan code set 1. Note only the keys that differ from the US layout.
3. **Look up non-ASCII characters** in a code page 437 table. Add `#define CP_...` constants beside the existing ones. If a character is not in code page 437, leave that level as 0.
4. **Write the table.** Add a `static const key_override_t xx_keys[]` array.
5. **Register it.** Add a line to the `keymaps` array with a two-letter name and a description.
6. **Test it.** In `test_keymaps` in `kernel/selftest.c`, add `expect` lines for at least three keys that differ from US, including one that uses Shift.

## Check

```sh
make test
make run
```

At the prompt:

```
phoenix> keymap
```

Your layout must be listed. Select it, type a few of the keys you changed, then run `selftest`; it must report `0 failed`.

QEMU sends scan codes according to your *host* keyboard's key positions, so if your real keyboard is a US one, pressing the key labelled Y should now produce whatever your layout puts at that position.

## Questions

1. Why is a layout stored as differences from US and not as a complete table? What does that cost at run time?
2. Press and hold Shift, press A, release Shift, release A. Write down the four scan codes the driver receives and what it does with each.
3. Some keys send two bytes, starting with `E0h`. Find where the driver handles that. Why must right Alt be told apart from left Alt?
4. The dashboard can type into the shell over the serial port. Do those characters go through your layout? Find the code that answers this.

## Going further

Many European layouts have *dead keys*: pressing `^` and then `e` produces `ê`. Add dead-key support: a key that produces no character on its own but changes the next one. Decide what should happen when the next key has no accented form.
