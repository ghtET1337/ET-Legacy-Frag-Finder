# Protocol-84 cut fixture

`cut-protocol84.dm_84` is a synthetic 10-second demo generated for regression tests
using the official ET: Legacy MSG_WriteDeltaPlayerstate, MSG_WriteDeltaEntity and
Huffman implementation. It contains no map assets or real player recording.

It exercises 100ms snapshots, deltas referencing three packets back, periodic full
snapshots, signed/floating-point player fields, all ammunition-array groups,
entity appearance/removal/reappearance, three obituaries, area masks, baselines,
changed names, repeated reliable commands and bcs fragments crossing a cut.

Reference sources retrieved 2026-09-07:
- https://github.com/etlegacy/etlegacy/blob/master/src/qcommon/msg.c
- https://github.com/etlegacy/etlegacy/blob/master/src/qcommon/huffman.c
- https://github.com/etlegacy/etlegacy/blob/master/src/qcommon/q_shared.h

SHA-256 of reference files used for generation/independent decoding:

- msg.c: `9135faaff2fa5f6bb65cbc6a1134aa8d93e6b40f824bf204df0193d7279bb65d`
- huffman.c: `7653662ae32492344f69a0ca880e6c150a6ee4f7a613f418899d7062a046cc9f`
- q_shared.h: `16b688bd75ca999e75761aab5475292e9182dca0fdf16f857ead80f8a418a0d3`

Fixture SHA-256: `33186d71f0e132c7ce18407442605f1943242fc620f5f74f4721f7b690de75d6`

The generator/checker source is `../reference_engine_check.c`. It links against
upstream `msg.c` and `huffman.c` (unmodified), with local stubs for engine logging
and safe string functions. Its ASCII synthetic strings do not exercise string
sanitization. The cut writer itself is not used to generate the input fixture.
The `inspect` mode hashes the complete decoded player state, entity states and
area masks at every snapshot. It rejects missing delta bases and a delta-only
first snapshot. Expected cut hashes are compared to source hashes by serverTime.

Example build with ET: Legacy headers available in ETL_SRC/src/qcommon:

```sh
cc -std=c99 -O1 -ffunction-sections -fdata-sections -I"$ETL_SRC/src/qcommon" \
  -Wl,--gc-sections tests/reference_engine_check.c \
  "$ETL_SRC/src/qcommon/msg.c" "$ETL_SRC/src/qcommon/huffman.c" \
  -lm -o engine-check
./engine-check generate fixture.dm_84
./engine-check inspect fixture.dm_84
```

Upstream headers require `version.h`; a blank generated `version.h` suffices
for this message-codec-only harness. Normal Frag Finder tests use the committed
fixture and require no downloaded engine source.
