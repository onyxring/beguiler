# Appendix J Limits

Here is every numeric limit stated in this specification.  *Target* is the target the limit applies to:  `both`, `Z-machine`, or `Glulx`; a paired value is written `Z-machine / Glulx`.  Each limit is covered in the section named in the last column.

| Limit | Value | Target | Section |
| --- | --- | --- | --- |
| Local slots per routine, parameters and locals together | 15 | Z-machine | §18.10 |
| Arguments passed natively on a routine call | 5 | Z-machine | §6.3 |
| Frame pool size, in words (`framePoolSize`) | sized from the program, and at least 64, by default; at least 1 when set | both | §15.4, §18.10 |
| Words per object property (a `rawArray<T>` member's capacity) | 32 | Z-machine | §12.7, §12.8.3 |
| Distinct values in one `bnum` | 16 / 32 | Z-machine / Glulx | §2.7.2 |
| Global variables, shared with the library and the runtime | 233 | Z-machine | §18.12 |
| Attributes, shared with the library | 48 | Z-machine | §18.12 |
| Common properties, shared with the library | 61 | Z-machine | §18.12 |
| Readable memory | 64K | Z-machine | §18.12 |
| Entries in one array | 32,767 | Z-machine | §18.12 |
| Parameters of a pooled class's `create()` | 3 | Z-machine | §8.2.6 |
| `create()` and `destroy()` declarations per pooled class | one of each | both | §8.2.6 |
| Type parameters of `array<T>` and `rawArray<T>`, the only types which take one | 1 | both | §8.1.1 |
| `operator auto()` declarations per class | 1 | both | §9.5.1 |
| Member types in a named union | at least 2 | both | §2.8.2 |
| Integer literal in an `array<char>` initializer or element write | 0..255 | both | §12.4 |
| Assigned array `length` range | 0..32767 / 0..2^31-1 | Z-machine / Glulx | §12.3 |
| Include nesting depth | 255 | both | §18.4 |
| Compile-time errors reported per build | 1 | both | §19.1 |
| Words in the heap for local arrays and `<linq>` results (`heapSize`) | 1536 on the Z-machine, 16384 on Glulx by default (also for 0); otherwise at least 64 | both | §12.6, §15.4 |
| Arguments to `stringObj.format()` after the pattern (`$1`, `$2`) | 2 | both | §22.3 |
| String objects in the `<string>` pool (`bglStringPoolReserve`); running out stops the program | 10 by default | both | §22.3 |
| Tracked `<buf>` buffers beyond the `<string>` pool's (`bglTrackedBufferReserve`) | 116 by default | both | §22.2 |
| The room for a capture into an untracked buffer (`bglStringDefaultSize`) | 500 by default | both | §22.2 |
| Bytes in the `<string>` heap (`stringHeapSize`) | 5120 by default (also for 0) | both | §22.3 |
| Characters captured into a `stringObj` at once, and characters of a literal which other `<string>` operations read (`stringCaptureSize`) | 512 by default (also for 0) | both | §22.3 |
| `<buf>` capture nesting depth | 16 | both | §22.2 |
| Objects per `bgl.world` result buffer (`worldBufSize`); a longer walk stops at the limit | 128 by default; at least 1 | both | §15.4, §21.9 |
| `bgl.world` result buffers, rotating; a result is valid until the fourth subsequent query | 4 | both | §21.9 |
| Values listed in `bgl.util.random.get(a, b, …)` | 2 to 8 | both | §21.6.3 |
| Child windows per kind (`<glulxWindow>`):  text-buffer, text-grid, graphics | 8 of each | Glulx | §22.7.1 |
| Word size (`bgl.wordsize`), in bytes | 2 / 4 | Z-machine / Glulx | §21.3 |
| Width of `int` and `uint`, in bits | 16 / 32 | Z-machine / Glulx | §21.6.1 |
| Digits in the `serial` setting | exactly 6 | both | §15.3 |
| Major version representable in the `beguiler` symbol | 32 | Z-machine | Appendix F |
| ZSCII codes of the extended characters which can be typed directly | 155..224 | Z-machine | Appendix D.5 |
