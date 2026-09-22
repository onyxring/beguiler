# Appendix J Limits

Every numeric limit stated in this specification, in one table. *Target* is the target the limit
applies to: `both`, `Z-machine` or `Glulx`; a paired value is written `Z-machine / Glulx`. Each limit
is specified in the section named in the last column; this table does not add limits of its own.

| Limit | Value | Target | Section |
| --- | --- | --- | --- |
| Local slots per routine, parameters and locals together; one slot is reserved for the frame pointer, so a routine needing more than 14 spills to the frame pool | 15 | Z-machine | §16.10 |
| Arguments passed natively on a routine call; the rest travel through compiler-emitted globals | 5 | Z-machine | §16.10 |
| Frame pool size, in words (`framePoolSize`) | 64 by default; at least 1 | both | §15.4, §16.10 |
| Words per object property, counting the length slot; a larger member array uses separate storage | 32 | Z-machine | §10.7 |
| Distinct values in one `bnum` | 16 / 32 | Z-machine / Glulx | §2.7.2 |
| Parameters of a pooled class's `create()` | 3 | both | §8.2.6 |
| `create()` and `destroy()` declarations per pooled class | one of each | both | §8.2.6 |
| Type parameters per class; only the first binds | 1 | both | §8.1.1 |
| `operator auto()` declarations per class | 1 | both | §7.8 |
| Ternary operators per statement | 1 | both | §4.9 |
| Member types in a named union | at least 2 | both | §2.8.2 |
| Integer literal in an `array<char>` initializer or element write | 0..255 | both | §10.4 |
| `setLength(n)` range | 0..32767 / 0..2^31−1 | Z-machine / Glulx | §10.3 |
| Include nesting depth | 255 | both | §12.1.6, §16.4 |
| Compile-time errors reported per build; the first ends the build | 1 | both | §17.1, §14.7 |
| Elements in a literal-list `for (x in {…})` (`forInScratchSize`) | 31 by default; at least 1 | both | §15.4, §5.9.1 |
| Elements per `<linq>` scratch buffer (`linqScratchSize`) | 32 by default; at least 1 | both | §15.4, §20.5 |
| `<linq>` chain nesting depth (a chain inside a chain's predicate or mapper) | 2 | both | §20.5 |
| Arguments to `stringObj.format()` after the pattern (`$1`, `$2`) | 2 | both | §20.3 |
| String objects in the `<string>` pool (`bglStringPoolReserve`) | 10 by default | both | §20.3 |
| Characters per `<buf>` / `<string>` buffer (`bglStringDefaultSize`) | 500 by default | both | §20.2 |
| `<buf>` capture nesting depth | 16 | both | §20.2 |
| Objects per `bgl.world` result buffer; a longer walk stops at the limit | 128 | both | §19.9 |
| `bgl.world` result buffers, rotating; a result is valid until the fourth subsequent query | 4 | both | §19.9 |
| Values listed in `bgl.util.random.get(a, b, …)` | 2 to 8 | both | §19.6.3 |
| Child windows per kind (`<glulxWindow>`): text-buffer, text-grid, graphics | 8 of each | Glulx | §20.7.1 |
| Word size (`bgl.wordsize`), in bytes | 2 / 4 | Z-machine / Glulx | §19.3 |
| Width of `int` and `uint`, in bits | 16 / 32 | Z-machine / Glulx | §19.6.1 |
| Digits in the `serial` setting | exactly 6 | both | §15.3 |
| Major version representable in the `beguiler` symbol | 32 | Z-machine | Appendix F |
| ZSCII codes of the extended characters that may be typed directly | 155..224 | Z-machine | Appendix D.5 |
