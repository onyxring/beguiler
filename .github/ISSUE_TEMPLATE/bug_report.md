---
name: Bug report
about: Report a problem with the Beguile compiler (Beguiler)
title: "[bug] "
labels: bug
---

<!--
Thanks for helping improve Beguile. This is an early preview, so rough edges are
expected. The more of the below you can fill in, the faster the problem can be
diagnosed. The single most useful item is the generated .inf (see "Generated Inform 6").
-->

## What happened

<!-- A clear description of the problem. -->

## Minimal Beguile source that reproduces it

<!-- The smallest .bgl that still triggers the problem. Paste it here or attach the file. -->

```bgl

```

## Build settings

- **Target:** <!-- Z3 / Z5 / Z8 / Glulx -->
- **Library:** <!-- PunyInform / I6 Standard Library / orLibrary / none -->
- **How you built it:** <!-- the Beguilex extension, or the command line (paste the exact command) -->

## Generated Inform 6

<!--
Beguiler writes a `<name>.transpiled.inf` next to your source (or in the configured
output directory). Attach it, or paste the relevant part. For transpiler bugs this is
the most useful thing you can provide.
-->

## Expected vs. actual

- **Expected:**
- **Actual:** <!-- include the exact error or warning text, if any -->

## Environment

- **Beguiler version:** <!-- the banner line printed on every run, e.g. "Beguiler 0.1b ... (date)", or the release you downloaded -->
- **OS:** <!-- macOS (arm64 / x86_64), Linux x64, or Windows x64 -->
- **Inform 6 version:** <!-- only if the failure is at the Inform 6 stage -->
