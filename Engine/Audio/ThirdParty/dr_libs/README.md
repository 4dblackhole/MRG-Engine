# dr_mp3

Upstream: https://github.com/mackron/dr_libs
Revision: `dfe8377631000664666519fdb83da193fd8037f4`
File: `dr_mp3.h` (unmodified)
SHA-256: `997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f`
License: MIT No Attribution (MIT-0), selected from the upstream dual license.

The adapter uses the high-level float PCM API, including Xing/Info and LAME
priming/end-padding handling. Intentional silence remains part of the source.
The vendor implementation is compiled once in ThirdParty/DrMp3.cpp. Vendor
headers are MSVC external includes and vendor implementations are excluded
from owned-code static analysis. Decoder adapters remain analyzed at /W4.
