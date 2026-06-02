# Implement Beguile buffers 
Goal: Implement beguile string buffers.
##Terminology##
`size` is the allocated space in an array.
`length` is the amount of allocated space actually used.

## Technical Details
- string buffers are declared in beguile as array<char>.  array<other type> was implemented in a previous session.
- The I6 construct we use for beguile buffers is are called strings.  The storage format used by I6 strings is:

| ZCode byte pos | Description |
| --- | --- | 
| 0 | string length (WORD) |
| 2 | first character |
|...N | rest of string |

| ZCode byte pos | Description |
| --- | --- | 
| 0 | string length (WORD) |
| 4 | first character |
|...N | rest of string |

So getting the length of a string is done with:
``` 
    buf-->0 !note I6's WORD accessor -->
```
and accessing the first character with:
``` 
    buf->(WORDSIZE) !note I6's BYTE accessor ->
```

The tension here is that I6 strings do not contain size.

## the orLibrary 
We addressed the size issue in the orLibrary by moving the pointer to any given string forward by WORDSIZE + 2, thereby allocating space "prior" to the string to contain the size, plus 2 magic bytes to allow code to programmatically detect if any given string is a "sized buffer".  If it is, then the size is used by the various manipulation routines to safely edit the character array without overflowing the allocated space.

Getting the allocated space can be accomplished with:

``` 
    buf-->(-1)
```
Accessing the length and string content is done the same with sized buffers as normal strings:
``` 
    buf-->0 !length
    buf->(WORDSIZE) !first char
```
## I6 table arrays & Beguile "Tracked" arrays
I6 table arrays do the opposite as strings. The allocated `size` is in the first WORD, the `length` is not maintained by I6.  

Because we know the size in advance, we are able to utilize the *last* WORD, plus 2 additional bytes, in an array to store the length and the magic bytes.  For z-machine, we also employed the highest bit of the length WORD to expand our magic byte collision space.  We could do this because I6's max array size is signed, meaning the highest bit must always be zero. Ensuring this is true doubles the collision-protection space introduced by our two magic bytes.

Glulx needn't worry about the extra bit, since glulx WORD size is 4 bytes and the chance of collision is effectively zero.

We've been referring to these arrays which maintain length as `tracked` arrays.

## There was a misguided attempt...
...to adopt the same extra WORD storage pattern for sized buffers that are used with tracked arrays. Obviously, this is not possible, since we can't easily find the size of the array to determine the length.  

## Glulx research findings
Researched Glulx and Inform 6 docs (Glulx Specification, the Inform 6 Reference Addendum, the Glulx Inform Tech Reference, and the Game Author's Guide). Findings:

- **Glulx has no native sized-buffer format.** The three native string types `E0` (C-style), `E1` (Huffman-compressed), and `E2` (Unicode) are output/encoding formats, not in-memory length-prefixed buffers.
- **Glulx allows unaligned word access in memory.** Per the spec: "Multibyte values are not necessarily aligned in memory." Only stack values require natural alignment. This means orBuf's `buf-->(-1)` size read works on Glulx even though the resulting byte offset is not 4-byte aligned — the I6/Glulx interpreter handles it.
- **I6's `Array X buffer N` (since 6.30)** is a hybrid array: first WORD = length, then N data bytes. Same convention on both Z and Glulx (WORDSIZE adjusts). This is what `print_to_array` writes into, and what the parser library expects.
- **No direction change**: orLibrary's SizedBuffer model is correct and works as-is on Glulx. We can prefix the user pointer with `WORDSIZE + 2` bytes of metadata (magic + size) and the user-visible pointer still behaves as a standard I6 hybrid buffer.

## Implementation notes
- For the underlying raw allocation, use `Array X_raw -> (2 + 2*WORDSIZE + N)` (a raw byte array — no I6-managed length header at byte 0). We control every byte ourselves, including the leading magic. Using `buffer` would conflict with orBuf's magic placement at bytes 0-1.
- I6 syntax `(addr + offset)-->0` does a word read/write at any byte address. This handles the alignment-free reads/writes that the orBuf model needs on Glulx (where the size word at user_addr - WORDSIZE may not be 4-byte aligned).
- We can't reassign a global array's address (the array symbol IS its address constant), so each `array<char> buf[N]` declaration emits two things: a fixed-address `Array buf_raw -> ...;` for storage AND a `Global buf = 0;` pointer variable that bglInit sets to `buf_raw + WORDSIZE + 2`. Code refers to `buf`; the underlying `buf_raw` is internal.

## Coupled bug: byteArray `[]` uses hardcoded `+2` instead of `+WORDSIZE`
[_array.bgl](beguiler/beguiLib/core/_array.bgl)'s `byteArray` emitter today reads `$val->($i+2)` for `buf[i]`. The `+2` is hardcoded — it matches Z3's WORDSIZE but is wrong on Glulx where data in an I6 hybrid buffer starts at byte 4. On Glulx, `buf[0]` today reads into the middle of the length WORD instead of the first data byte.

This is a pre-existing bug, but it's tightly coupled with the sized-buffer work: any tests we write to exercise sized buffers on Glulx will fail until `[]` uses `WORDSIZE` correctly. Fix bundled with the SizedBuffer implementation:

```bgl
extern class byteArray : array {
    replace emitter char operator[](int i)          { $val->($i + bgl.wordsize) }
    replace emitter char operator[]=(int i, char v) { $val->($i + bgl.wordsize) = $v }
    replace emitter int  size()                     { $val-->0 }   // length, per hybrid buffer convention
}
```

The `size()` semantics also shifts: for a hybrid buffer, the length WORD lives at `$val-->0` (Beguile's existing `size()` reads byte 0, which is the high byte of the length WORD on Glulx — also broken today). Renaming and re-routing the byteArray surface is part of this change.

## The goal of this exercise...
is to have orLibrary sizedBuffers implemented by default in Beguile.  There are things we may need to work around, since I don't think we can change the value of a global array, we will need to work around this by declaring it as a variable which points to the global, and adjusting the variable forward.  This would make interop with I6 code easier.
