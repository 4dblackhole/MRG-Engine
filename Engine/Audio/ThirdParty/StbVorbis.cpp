// Compile the unmodified vendored implementation once. Its directory is an
// MSVC external include, outside Client-owned diagnostics/static analysis.
#pragma warning(push)
// In upstream seek_to_sample_coarse, probe is incremented only after
// get_seek_page_info has initialized mid. The probe==1 branch cannot read mid
// before that successful first iteration; MSVC misses this loop invariant.
#pragma warning(disable : 4701)
#include <stb_vorbis.c>
#pragma warning(pop)
