# Translating Phoenix-8086

English is the reference language. There are three things to translate, each independent of the others.

## Dashboard language

Each language is one file in `dashboard/locales/`, for example `de.js`. To add one:

1. Copy `dashboard/locales/en.js` to `dashboard/locales/<code>.js`, where `<code>` is the two-letter language code.
2. Change the code in `i18n.register('<code>', ...)`, the `name` (the language's own name for itself), and `dir` (`'ltr'`, or `'rtl'` for right-to-left scripts).
3. Translate the values. Leave the keys alone, and keep every `{placeholder}` exactly as it is; you may move it within the sentence.
4. Add a `<script src="locales/<code>.js"></script>` line to `dashboard/index.html`, and the code to the lists in `dashboard/test/fakedom.js`, `dashboard/test/i18n.test.js` and `dashboard/test/render.test.js`.
5. Run `node --test dashboard/test/*.test.js`. The tests fail if a key is missing, extra, empty, or has different placeholders.

Guidance:

* Register names, hexadecimal values, thread names and console text come from the kernel and are never translated.
* Kernel terms such as IRET, IRQ, TID and INT 80h stay as they are.
* Thread states (READY, RUNNING, ...) may be translated; keep them short, they sit in a narrow badge.

### Status

| Language | File | Status |
| --- | --- | --- |
| English | `en.js` | Reference |
| German | `de.js` | Initial translation, not yet reviewed by a native speaker |
| French | `fr.js` | Initial translation, not yet reviewed by a native speaker |
| Spanish | `es.js` | Initial translation, not yet reviewed by a native speaker |
| Arabic | `ar.js` | Initial translation, not yet reviewed by a native speaker; right-to-left layout not yet checked in a browser |

Reviews by native speakers are very welcome: open a pull request that corrects the file and updates this table.

## Keyboard layout

A layout is a list of the keys that differ from the US layout, in `kernel/keyboard.c`. [Lab 3](labs/03-keyboard-layout.md) walks through adding one. Characters outside ASCII use code page 437, the PC's text-mode character set, so only characters in that set can be typed.

## Documentation

Translated documents go in `docs/<code>/` with the same file names as the English ones, for example `docs/de/architecture.md`. Start each translated file with a line saying which English version (commit or date) it was translated from, so readers can tell when it has fallen behind. Translate documents one at a time; a partly translated set is fine.

The kernel's own messages (shell output, panic screen) are English only for now.
