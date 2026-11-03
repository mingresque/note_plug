# NotePlug — greenfield CapitalizeNext

A compact, **native Windows x64 Notepad++ plugin** rebuilt from the user-facing behavior of `mingresque/note_plug` (main branch, reviewed October 31, 2026). This is **not** a database application and requires **no database**, background service, runtime package, or external dependency beyond Notepad++ and the Windows libraries it already uses.

The repository is intentionally small: one plugin implementation, one small pure-rules header, one Windows build script, and one focused set of rule tests. No large fixture matrix, probes, release evidence, agents, orchestration scripts, or archived specifications are included.

**Validation status:** Pure rules tests passed with GCC on Linux. The C++/Win32 adapter passed a syntax-only check using local Win32 API stubs. The DLL **has not been compiled with MSVC or tested in a real Notepad++ process**. This is a greenfield implementation, **not a certified drop-in parity replacement**; test in a disposable Notepad++ installation before using it with important documents.

## Features

| Input | Enhanced behavior | Native fallback |
| --- | --- | --- |
| Lowercase typing | Capitalizes at logical line start, immediately after `. `, `? `, `: `, `! `, or `- ` at column zero | Everything outside eligible contexts; selected text; unsupported keyboard state |
| Uppercase typing | If the new uppercase letter precedes an uppercase letter directly or across exactly one ASCII space, lowercase that successor | Unsupported editing context |
| Delete and Backspace | Keep native deletion/selection semantics; adjust only the immediate surviving ASCII letter (possibly across one ASCII space) when its capitalization eligibility changes | Ineligible and unsupported cases |
| Paste | Use native paste route; capitalize only first inserted lowercase ASCII character when the original caret prefix qualifies | Selected text, non-single-insertion native modifications, or other unsupported cases |
| Space | Normalize up to two adjacent ASCII spaces on each side of the caret into one; leave outer spaces intact; update the immediately following letter only when eligibility changes | No adjacent spaces and no case transition |
| Enter | Remove up to three literal spaces immediately to the left of the original caret, retain native line-ending/autoindent and promote the originally adjacent lowercase letter only if it reaches logical column zero | Unsupported contexts; modified Enter |

The transformations are byte-oriented for ASCII letters and literal ASCII spaces. Non-ASCII content and Unicode whitespace are not rewritten. Logical line starts, not soft wraps, determine eligibility.

## Safety and behavior

- Only the foreground attached Scintilla editor on its owning UI thread is eligible.
- Requires one normal selection (Delete/Backspace permit one contiguous selection), supported code page (UTF-8 or Scintilla code page 0), insert mode, writable document, no virtual spaces, and active Undo collection.
- Manual uppercase typing permits Shift, Caps Lock, and other keyboard layouts; lowercase enhancement requires the US layout, Caps Lock off, and no held modifiers. Paste allows Ctrl/Shift. Space permits Shift. Modified Enter and deletion are not enhanced.
- Native typing, deletion, paste, and Enter are forwarded once. A Space collapse/no-op is an intentional replacement/suppression rather than forwarding a new space.
- When a compound edit is planned, it uses Scintilla's `SCI_BEGINUNDOACTION` / `SCI_ENDUNDOACTION` to group the native operation and correction. It checks document identity, caret/selection and nearby bytes before secondary edits. If a check fails after native dispatch, it leaves the native result in place instead of attempting a risky rollback.
- Paste capitalization requires a synchronous `SCN_MODIFIED` witness of one native user insertion. The plugin never reads the clipboard.
- Both editor views and the standard main-window Paste command are supported.

### Intentional simplifications and limitations

- Uses a direct, editor-bound Scintilla Undo action. The original used a more elaborate hidden-view, document-retention scheme; this simplified approach does not provide identical protection against unusual synchronous document switches or reentrant host/plugin behavior during Undo scopes.
- Does not implement the original's timed, per-query 25 ms budget, detailed event audits, diagnostic instrumentation, or exhaustive failure-injection coverage. Normal Scintilla operations execute synchronously on the UI thread.
- This implementation is cautious about compound edits: a changed document, unsupported selection, unexpected native paste pattern, or unexpected Enter result may suppress *secondary formatting* while leaving the native edit intact.
- Native host behavior and plugin interoperability may vary with Notepad++/Scintilla versions, EOL/indent settings, input methods, and other plugins. No Windows integration matrix has been executed.
- Does not migrate historical datasets or previous user settings (the original plugin is not a database-backed application and stores no rules database).

## Prerequisites

- Windows 10/11 x64 and Notepad++ x64.
- Microsoft Visual Studio 2022 C++ x64 build tools (MSVC and Windows SDK) installed. `build.cmd` uses an active `VSINSTALLDIR` or discovers the Visual Studio installation with `vswhere`.

## Build

From a Windows Command Prompt or PowerShell opened in this directory:

```cmd
build.cmd
```

Produces `build\CapitalizeNext.dll`. To run only the focused C++ rules tests:

```cmd
build.cmd test
```

No tests are required to compile the DLL. There are no third-party dependencies or generated source files.

## Install (test in an isolated copy first)

1. Close Notepad++ completely. **Back up or disable the existing `CapitalizeNext` plugin first**; never load both plugins with the same name.
2. Place the compiled DLL at `plugins\CapitalizeNext\CapitalizeNext.dll` under your **portable Notepad++ directory**, or use the equivalent plugin subdirectory for your installed Notepad++ instance. Notepad++ needs to load plugins from its configured plugin location.
3. Restart Notepad++. The plugin uses the name **CapitalizeNext**, with no menu commands beyond the required Notepad++ API separator.
4. First verify input in a **disposable text file**. Check both primary and secondary views, Undo/Redo after a compound edit, paste variants, EOL modes, and Basic/none autoindent behavior.
5. If any native editing behavior changes unexpectedly, close Notepad++, remove this DLL and restore the original plugin.

## Minimal manual smoke check

- Type `hello` on an empty line: `Hello`.
- Type `a. b`: `A. B`. Typing after `a.  ` (two spaces) does not capitalize.
- Type `- x` at column zero: `- X`; after `a- x`, the `x` stays lowercase.
- Place the caret before `XYZ` and type `A`: `AxYZ`; the successor changes in the same Undo action.
- Paste `hello world` at the beginning of an empty line: only the inserted first `h` becomes uppercase.
- With caret amid adjacent ASCII spaces, type Space: the bounded local window collapses to one, with the caret just after it.
- With caret after three trailing spaces on a line, press Enter: those spaces are removed, while Notepad++ supplies the configured newline and indentation.
- Test Delete/Backspace with a collapsed caret and a normal selection immediately before a surviving ASCII letter; verify change only when prefix eligibility changes. Repeat Undo/Redo.

**Important:** These are manual instructions, not recorded test results. Do not treat successful Linux rule tests as proof of Windows plugin runtime correctness.

## Layout

```
noteplug/
  README.md
  build.cmd
  .gitignore
  src/
    plugin.cpp       Windows/Notepad++ ABI and all six input handlers
    rules.hpp        Pure ASCII decisions
  tests/
    rules_test.cpp   Small C++ rule checks
```

## Origin and repository safety

The new source was written separately, not patched into the original. The original GitHub repository was **read only** for reference and remains unchanged. This archive includes source files and instructions, not a prebuilt or validated Windows binary.
