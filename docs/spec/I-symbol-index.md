# Appendix I Symbol and Method Index

Every runtime and library name an author can write, alphabetically: functions, methods, members,
objects, namespaces, types, constants and the symbols a library defines. *Kind* says what the name
is; a method or member names the type it belongs to. *Provided by* is `core` (loaded without an
include, §21), an extension (`<name>`, §22) or a binding (§23). *Section* is where the name is
specified. Overloads share one row; a family of names that differ only in a suffix is listed once
with the suffix spelled out. Names of the `bgl` namespace are listed under `bgl.…`.

`<array>` is loaded by the core, so its methods need no include; `length()` is built in, `setLength()`
and `clear()` come from `<array>`.

| Name | Kind | Provided by | Section |
| --- | --- | --- | --- |
| `+=`, `-=` | operator on `array<T>` | `<array>` | §22.4 |
| `=~` | operator on `char` (case-insensitive equality) | core | §21.7 |
| `a(obj)`, `cA(obj)` | function | core | §21.4 |
| `AfterLife()`, `NewRoom()`, `TimePasses()`, `InScope()`, … | function (`extern default` entry point; author-overridable) | binding (`i6StandardLibrary`) | §23.3.4 |
| `any(pred)`, `all(pred)` | method on `array<T>` | `<linq>` | §22.5 |
| `append(item)` | method on `array<T>` | `<array>` | §22.4 |
| `append(v)`, `prepend(v)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `array<T>` | type | core | §12 |
| `attribute` | type | core | §11.6, §21.5.1 |
| `attributeList` | type | core | §11.5.3, §21.5.1 |
| `attributes` | member on `object` | core | §11.5.3, §21.5.1 |
| `before`, `after`, `life`, `orders`, `describe`, `time_out`, `each_turn` | property (additive) | binding (`i6StandardLibrary`) | §23.3.5 |
| `bgl` | namespace | core | §21.3 |
| `bgl.asm` | namespace (opcode emitters; members differ per target) | core | §21.3 |
| `bgl.glulx` | namespace | core (Glulx) | §21.3 |
| `bgl.glulx.color` | object | `<glulxWindow>` | §22.7.8 |
| `bgl.glulx.eStyleType`, `eWinType`, `eImgAlign`, `eImgDimension`, `bWinBorder`, `bWinPlacement`, `bWinScale` | type (enum aliases) | core (Glulx) | §21.3, §22.7.10 |
| `bgl.glulx.window`, `bgl.glulx.textBufferWindow`, `bgl.glulx.textGridWindow`, `bgl.glulx.graphicsWindow` | type (path forms of the window types) | `<glulxWindow>` | §21.3, §22.7.1 |
| `bgl.printRules` | namespace | core | §21.11 |
| `bgl.printRules.bold`, `italics`, `underline`, `reverse`, `fixed`, `roman` | print rule | core | §21.11 |
| `bgl.printRules.img(image[, align[, width[, height]]])` | print rule | core (Glulx) | §21.11 |
| `bgl.story` | namespace | binding (`i6StandardLibrary`) | §23.3.9 |
| `bgl.story.printSerial()` | method on `bgl.story` | binding (`i6StandardLibrary`) | §23.3.9 |
| `bgl.story.release` | member on `bgl.story` | binding (`i6StandardLibrary`) | §23.3.9 |
| `bgl.story.serialChar(i)` | method on `bgl.story` | binding (`i6StandardLibrary`) | §23.3.9 |
| `bgl.ui` | namespace | core | §21.10 |
| `bgl.ui.hideCursor()`, `showCursor()` | function | `<ui>` | §22.6 |
| `bgl.ui.mainWin` | object (root text-buffer window) | core | §21.10, §22.7.2 |
| `bgl.ui.screen` | object (style target for both text kinds) | `<glulxWindow>` | §22.7.2 |
| `bgl.ui.statusBar` | object (root text-grid window) | core | §21.10, §22.7.2 |
| `bgl.ui.waitForKey([separator])` | function | `<ui>` | §22.6 |
| `bgl.util` | namespace | core | §21.3 |
| `bgl.util.buf` | namespace | `<buf>` | §22.2 |
| `bgl.util.buf.append(to, from)`, `prepend(to, from)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.capture(buf[, maxBytes])`, `release()`, `stackLen()` | function | `<buf>` | §22.2 |
| `bgl.util.buf.compare(a, b[, caseInsensitive])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.copy(to, from, n[, toPos[, fromPos]])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.delete(buf, pos, count)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.equals(a, b[, caseInsensitive])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.getChar(buf, pos)`, `setChar(buf, pos, c)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.indexOf(buf, search[, start])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.indexOfFirstTrue(buf, pred[, start])`, `indexOfFirstFalse(…)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.insert(to, from, pos[, count])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.isTracked(buf)`, `size(buf)`, `length(buf)`, `setLength(buf, n)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.mid(to, from, fromPos, n)`, `left(to, from, n)`, `right(to, from, n)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.print(buf[, len])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.replace(buf, search, repl)`, `replaceAll(buf, search, repl[, start])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.set(buf, value)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.startsWith(buf, prefix[, ci])`, `endsWith(buf, suffix[, ci])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.toUpper(buf)`, `toLower(buf)`, `reverse(buf)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.trim(buf)`, `trimLeft(buf)`, `trimRight(buf)` | function | `<buf>` | §22.2 |
| `bgl.util.math` | namespace | core | §21.6.2 |
| `bgl.util.math.abs(x)` | function | core | §21.6.2 |
| `bgl.util.math.clamp(v, lo, hi)` | function | core | §21.6.2 |
| `bgl.util.math.min(a, b)`, `max(a, b)` | function | core | §21.6.2 |
| `bgl.util.math.pow(base, exp)` | function | core | §21.6.2 |
| `bgl.util.math.shiftLeft(x, n)`, `shiftRight(x, n)` | function | core | §21.6.2 |
| `bgl.util.math.sign(v)` | function | core | §21.6.2 |
| `bgl.util.math.unsignedCompare(a, b)` | function | core | §21.6.2 |
| `bgl.util.math.unsignedDiv(a, b)`, `unsignedMod(a, b)` | function | core | §21.6.2 |
| `bgl.util.random` | namespace | core | §21.6.3 |
| `bgl.util.random.get(n)`, `get(a, b[, c …])` | function | core | §21.6.3 |
| `bgl.util.random.seed(s)` | function | core | §21.6.3 |
| `bgl.wordsize` | constant | core | §21.3 |
| `bgl.world` | namespace | core | §21.9 |
| `bgl.world.getAll([pred])` | function | core | §21.9 |
| `bgl.world.inParent(parent[, pred])` | function | core | §21.9 |
| `bgl.world.instances(cls[, pred])` | function | core | §21.9 |
| `bgl.zcode` | namespace | core (Z-machine) | §21.3 |
| `bglAllocated` | type (mixin class) | core | §21.8 |
| `bglAutoInitialize` | symbol (mirrors `autoInitialize`) | binding | §23.5 |
| `bglClass` | type | core | §21.5.6 |
| `_bglGlobalDeclaration()` | emitter hook on a class (one top-level I6 declaration per instance; `$selfsub`) | core | §15.8 |
| `bglInit()` | function | core | §21.2, §18.8 |
| `_bglObject` | type (emitter class) | core | §21.5.8 |
| `bglSize` | type (value class) | core | §21.12 |
| `bglStringDefaultSize` | constant (I6, set before the include) | `<buf>` | §22.2 |
| `bglStringPoolReserve` | constant (I6, set before the include) | `<string>` | §22.3 |
| `bGlulxWindowBorder` | type (bnum) | core (Glulx) | §22.7.10 |
| `bGlulxWindowPlacement` | type (bnum) | core (Glulx) | §22.7.10 |
| `bGlulxWindowScale` | type (bnum) | core (Glulx) | §22.7.10 |
| `bool` | type | core | §2.2 |
| `capture()`, `release()` | method on `stringObj` | `<string>` | §22.3 |
| `captureOutput(obj, prop)` | method on `stringObj` | `<string>` | §22.3 |
| `char` | type | core | §2.2, §21.7 |
| `children` | member on `object` | core | §11.5.2, §21.5.7 |
| `childrenProp` | type | core | §21.5.7 |
| `clear()` | method on `array<T>` | core; `<array>` | §12.3, §22.4 |
| `close()` | method on `window` | `<glulxWindow>` | §22.7.6 |
| `CLR_*` | constant (colors) | binding | §23.4 |
| `clr_on`, `clr_fg`, `clr_bg`, `clr_fgstatus` | variable (extern) | binding (`punyInform`) | §23.4 |
| `color.black`, `white`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `gray`, `lightGray`, `darkGray` | constant (on `bgl.glulx.color`) | `<glulxWindow>` | §22.7.8 |
| `color.rgb(r, g, b)` | method on `bgl.glulx.color` | `<glulxWindow>` | §22.7.8 |
| `compareTo(other[, caseInsensitive])` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `contains(item)` | method on `array<T>` | `<array>` | §22.4 |
| `copy(other)` | method on `bglAllocated` | core | §21.8 |
| `count()` | method on `array<T>` | `<linq>` | §22.5 |
| `create([p1[, p2[, p3]]])` | method on a pooled class (author-declared; run by `new`) | core | §8.2.6 |
| `DEBUG` | symbol (`#define`; enables `log()`) | core | §21.4 |
| `deinit()`, `static deinit(T v)` | lifecycle emitter / value-form method on a class | core | §8.5, §12.10 |
| `delete(pos, count)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `destroy()` | method on a pooled class (author-declared; run by `delete`) | core | §8.2.6 |
| `dictionaryWord` | type | core | §13.1, §21.5.3 |
| `Directions` | object (extern; the shared direction placeholder) | binding (`punyInform`) | §23.4 |
| `distinct()` | method on `array<T>` | `<linq>` | §22.5 |
| `door`, `absent`, `pluralname`, `male`, `female`, `neuter` | value (`attribute`) | binding (`i6StandardLibrary`) | §23.4 |
| `drawImage(img, x, y[, width[, height]])` | method on `graphicsWindow` | `<glulxWindow>` | §22.7.5 |
| `drawImage(img[, align[, width[, height]]])` | method on `textBufferWindow`, `bgl.ui.mainWin` | `<glulxWindow>` | §22.7.5 |
| `eAssets` | type (named union) | core | §21.12 |
| `eBool` | type (enum) | core | §2.7.4 |
| `eErrorFormat` | type (enum) | core | §17.3, Appendix E |
| `eGlulxImageAlign` | type (enum) | core (Glulx) | §22.7.10, §21.11 |
| `eGlulxJustify` | type (enum) | `<glulxWindow>` | §22.7.10 |
| `eGlulxStyleHint` | type (enum) | `<glulxWindow>` | §22.7.10 |
| `eGlulxStyleType` | type (enum) | core (Glulx) | §22.7.10 |
| `eGlulxWindowType` | type (enum) | core (Glulx) | §22.7.10 |
| `eImages` | type (enum) | core | §17.6.1, §21.12 |
| `enqueue(item)`, `dequeue()` | method on `array<T>` | `<array>` | §22.4 |
| `eSounds` | type (enum) | core | §17.6.1, §21.12 |
| `eTarget` | type (enum) | core | §17.3, Appendix E |
| `eType` | type (enum) | core | §2.8.1, §21.5.9 |
| `eUnknownAsset` | type (enum) | core | §17.6.1, §21.12 |
| `FAKE_*_OBJ` | constant (parser sentinels) | binding (`punyInform`) | §23.4 |
| `filter(pred)` | method on `array<T>` | `<linq>` | §22.5 |
| `first()`, `last()` | method on `array<T>` | `<linq>` | §22.5 |
| `float` | type | core (Glulx) | §2.3 |
| `format(pattern[, p1[, p2]])` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `func<…>` | type | core | §2.9 |
| `getLength()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `gg_mainwin`, `gg_statuswin`, … | variable (Glulx window globals) | binding (`i6StandardLibrary`) | §23.2 |
| `give(attr)`, `ungive(attr)` | method on `object`, `attributeList` | core | §21.5.1, §11.5.3 |
| `glulxImage` | type (veneer class) | `<glulxImage>` | §22.8 |
| `grammar` | member on `verb` | core | §13.2, §13.4.5 |
| `grammarRule` | type | core | §13.4.1, §21.5.5 |
| `grammarRuleList` | type | core | §13.4.1, §21.5.5 |
| `grammarToken` | type (extern enum) | binding | §13.4.1, §23.3.6 |
| `graphicsWindow` | type | `<glulxWindow>` | §22.7.1 |
| `handler()` | method on `verb` | core | §13.2.1 |
| `has(attr)`, `hasnt(attr)` | method on `object`, `attributeList` | core | §21.5.1, §11.5.3 |
| `height` | member on `bgl.ui.statusBar`, `window` | core; `<glulxWindow>` | §21.10, §22.7.4 |
| `I6_STANDARD_LIBRARY` | symbol (`#declare`) | binding (`i6StandardLibrary`) | §23.3.3 |
| `id` | member on `bgl.ui.mainWin`, `bgl.ui.statusBar`, `window` | core | §21.10, §22.7.1 |
| `indexOf(item)`, `find(item)` | method on `array<T>` | `<array>` | §22.4 |
| `indexOf(search)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `informVersion` | print rule | binding (`i6StandardLibrary`) | §23.3.9 |
| `INFORMV__TX`, `LIBRARYV__TX`, `LibRelease` | constant | binding (`i6StandardLibrary`) | §23.3.9 |
| `init()` | lifecycle emitter on a class (fires at a local's declaration) | core | §8.5 |
| `Initialise()` | function (entry point the library calls; author-defined) | binding | §23.3.1 |
| `insert(pos, item)` | method on `array<T>` | `<array>` | §22.4 |
| `insert(pos, src)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `int` | type | core | §2.2 |
| `intLiteral`, `negativeIntLiteral`, `stringLiteral`, `charLiteral`, `dictionaryWordLiteral`, `interpolatedStringLiteral` | type (literal pseudo-type) | core | §2.4 |
| `is(Class)` | method on `object` | core | §21.5.6 |
| `isEmpty()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `isLower()`, `isUpper()`, `isAlpha()`, `isNumeric()`, `isAlphaNumeric()`, `isVowel()`, `isConsonant()` | method on `char` | core | §21.7 |
| `isRoutine()` | method on `stringOrRoutine` | binding | §21.5.10 |
| `isTracked()` | method on `array<T>` | `<array>` | §22.4 |
| `itobj`, `himobj`, `herobj` | variable (pronoun objects) | binding | §23.4 |
| `length()` | method on `array<T>` | core; `<array>` | §12.3, §22.4 |
| `length()`, `size()` | method on `children` | core | §11.5.2, §21.5.7 |
| `light`, `container`, `scenery`, `static`, … (library attributes) | value (`attribute`) | binding | §23.3.4 |
| `location`, `player`, `actor`, `score`, `turns` | variable (extern) | binding | §23.3.4 |
| `log(v)` | function | core | §21.4 |
| `map(f)` | method on `array<T>` | `<linq>` | §22.5 |
| `measureStyle(styleType, hint)`, `styleHonored(styleType, hint)` | method on `window` | `<glulxWindow>` | §22.7.9 |
| `meta` | member on `verb` | core | §13.2.4 |
| `mid(start, count)`, `left(count)`, `right(count)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `moveCursor(col, line)` | method on `textGridWindow` | `<glulxWindow>` | §22.7.6 |
| `moveUp()`, `moveDown()`, `moveLeft()`, `moveRight()` | method on `window` | `<glulxWindow>` | §22.7.4 |
| `name` | property (additive, on `object`) | core | §11.7.2, §21.5.2 |
| `NO_ATTRIBUTE` | constant | core | §21.5.1 |
| `n_obj` … `d_obj` (compass direction objects) | object (extern) | binding (`i6StandardLibrary`) | §23.3.4, §23.4 |
| `null` | value | core | §2.5 |
| `noun`, `held`, `creature`, `topic`, `multi`, `multiheld`, `multiexcept`, `multiinside`, `special`, `anynumber`, `number`, `scope`, `reverse` | constant (`grammarToken` values) | binding | §13.4.2, §23.3.6 |
| `noun`, `second`, `action`, `verb_word` | variable (extern; `action` is a `verb`) | binding | §23.3.4, §13.3 |
| `object` | type | core | §2.2, §11 |
| `orderBy([compare])` | method on `array<T>` | `<linq>` | §22.5 |
| `parent` | member on `object` | core | §11.5.1, §21.5.7 |
| `parentProp` | type | core | §21.5.7 |
| `patternElement` | type | core | §13.4.1, §21.5.5 |
| `peekEnd()`, `popEnd()` | method on `array<T>` | `<array>` | §22.4 |
| `perform([noun[, second]])` | method on `verb` | core | §13.2.2 |
| `PlayerTo()`, `TestScope()`, `StartTimer()`, `StatusLineHeight()`, … | function (extern library routine) | binding | §23.3.4 |
| `prepend(item)` | method on `array<T>` | `<array>` | §22.4 |
| `print()` | method on `_bglObject` (author-defined override) | core | §21.4 |
| `print()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `print(v)` | function (overloaded by type) | core | §21.4 |
| `printName(obj)` | function | core | §21.4 |
| `priority` | member on `verb` | core | §13.2.5 |
| `property` | type | core | §11.7, §21.5.2 |
| `provides(prop)` | method on `object` | core | §11.7.1, §21.5.2 |
| `PUNYINFORM` | symbol (`#declare`) | binding (`punyInform`) | §23.3.3 |
| `push(item)`, `pop()`, `peek()` | method on `array<T>` | `<array>` | §22.4 |
| `rawArray<T>` | type | core | §12.8 |
| `remaining()` | method on `bglAllocated` | core | §21.8 |
| `remove(pos)` | method on `array<T>` | `<array>` | §22.4 |
| `removeValue(item)` | method on `array<T>` | `<array>` | §22.4 |
| `replace(search, repl)`, `replaceAll(search, repl)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `reverse()` | method on `array<T>` | `<array>` | §22.4 |
| `reverse()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `selected_direction`, `selected_direction_index` | variable (extern) | binding (`punyInform`) | §23.4 |
| `setBackgroundColor(color)` | method on `graphicsWindow` | `<glulxWindow>` | §22.7.5 |
| `setLength(n)` | method on `array<T>` | core; `<array>` | §12.3, §22.4 |
| `setStyle(styleType, style {…})`, `clearStyle(styleType)` | method on the text window types, the roots, `bgl.ui.screen` | `<glulxWindow>` | §22.7.7 |
| `short_name` | member on `object` | core | §21.4 |
| `size()` | method on `array<T>` | core | §12.3 |
| `size()` | method on `glulxImage`, `eImages` | `<glulxImage>` | §22.8 |
| `size()`, `length()`, `setLength(n)`, `isTracked()` | method on `array<char>` | `<buf>` | §22.2 |
| `sort([compare])` | method on `array<T>` | `<array>` | §22.4 |
| `splitUpGrid()` … `splitRightBuffer()` (12 combinations of direction and kind) | method on `window`, the roots | `<glulxWindow>` | §22.7.3 |
| `startsWith(prefix)`, `endsWith(suffix)`, `contains(search)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `story`, `headline` | constant (I6) and `extern string` | binding | §23.3.2 |
| `string` | type | core | §2.2, §22.3 |
| `stringObj` | type | `<string>` | §22.3 |
| `stringOrRoutine` | type (named union) | binding | §2.8.2, §21.5.10, §23.3.7 |
| `style` | emitter namespace (built-in; called as `style.⟨member⟩()`; the same word as the value class below) | core | §7.7 |
| `style` | type (value class; written `style { … }`; the same word as the emitter namespace above) | `<glulxWindow>` | §22.7.7 |
| `styleUnset` | constant | `<glulxWindow>` | §22.7.10 |
| `swap(pos1, pos2)` | method on `array<T>` | `<array>` | §22.4 |
| `switchable`, `on`, `workflag`, `reactive`, `scored` | value (`attribute`) | binding (`punyInform`) | §23.4 |
| `take(n)`, `skip(n)` | method on `array<T>` | `<linq>` | §22.5 |
| `Take`, `Look`, `Receive`, … (the library's actions) | object (`extern verb`) | binding | §23.3.6 |
| `takeWhile(pred)`, `skipWhile(pred)` | method on `array<T>` | `<linq>` | §22.5 |
| `textBufferWindow` | type | `<glulxWindow>` | §22.7.1 |
| `textBufferWindowHorz`, `textBufferWindowVert`, `textGridWindowHorz`, `textGridWindowVert`, `graphicsWindowHorz`, `graphicsWindowVert` | type (orientation views) | `<glulxWindow>` | §22.7.3 |
| `textGridWindow` | type | `<glulxWindow>` | §22.7.1 |
| `the(obj)`, `cThe(obj)` | function | core | §21.4 |
| `thedark`, `selfobj` | object (extern) | binding | §23.3.4 |
| `themobj` | variable (pronoun object) | binding (`punyInform`) | §23.4 |
| `toUpper()`, `toLower()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `toUpper()`, `toLower()` | method on `char` | core | §21.7 |
| `trim()`, `trimLeft()`, `trimRight()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `typeof(v)` | function | core | §2.8.1, §21.5.9 |
| `uint` | type | core | §21.6.1 |
| `var` | type | core | §2.6 |
| `verb` | type | core | §13.2, §21.5.4 |
| `void` | type | core | §2.2 |
| `width` | member on `window`, the roots | `<glulxWindow>` | §22.7.4 |
| `width()`, `height()` | method on `glulxImage`, `eImages` | `<glulxImage>` | §22.8 |
| `window` | type | `<glulxWindow>` | §22.7.1 |
