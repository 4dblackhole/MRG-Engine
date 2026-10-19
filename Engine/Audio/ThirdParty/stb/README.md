# stb_vorbis

Upstream: https://github.com/nothings/stb
Revision: `2c980bb59875b0d32144a71867fbdebb2f77cd20`
File: `stb_vorbis.c`, v1.22 (unmodified)
SHA-256: `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`
License: MIT, as provided in LICENSE.

Moved from the Client editor into the engine's shared sequential PCM decoder.
The existing external-header/static-analysis policy and the documented upstream
C4701 loop-invariant workaround remain limited to the vendor implementation.
This decoder supports Vorbis, not Opus. Chained and floor-0 Vorbis streams retain
the upstream documented limitations.
