# Field trials

Hardening tests for the Beguile compiler. Each trial is a small program written against the Inform 6
standard library, the way an author's code is written, and played with a fixed list of commands. A
trial that fails to compile, or prints anything other than what its `.trial` file expects, is a
compiler (or documentation) defect to fix.

**These are not games and not examples.** They exist only to test the compiler. They are not released,
packaged or meant to be played, read as fiction or reused.

## Rules for every trial

- File names say what is exercised: `lock_and_key.bgl`, never a story title.
- The program opens with this block, unchanged:

  ```
  /*  Field trial — exercises the Beguile compiler against the Inform 6 standard library.
      A hardening test: not a game, not an example, not for reuse.  */
  ```

- No game trappings: no story name or headline settings, no IFID, no release or serial number, no cover
  art. Text is flat and functional (`Room A`, `brass key`, `The door opens.`).
- Each `<name>.bgl` has a `<name>.trial` beside it: the commands, and what each prints.
- Files a build generates here (`_blorbAssets.bgl`, from blorb packaging) are not trials and are not
  committed.

Run them with `python3 tests/tools/field_trials.py` (`--only NAME`, `--show` to print transcripts). They
also run at the end of `tests/run_tests.sh`.
