// libFuzzer harness for noxim's traffic-table region/distance helpers.
//
// This REPLACES the original mayhem/fuzz.cpp, which was dead copy-paste junk: it
// declared an unrelated `Node2Coords` symbol and a commented-out `chafa_*` call,
// used a FuzzedDataProvider whose values were never consumed, and was never compiled
// or referenced by any Mayhemfile.
//
// Here we drive the REAL code from other/ttable_from_hub.cpp in-process: the wired /
// wireless distance routing logic (id2Coord, getWiredDistanceI, getWirelessDistance,
// prefersWirelessPath) and the region enumerator (GetNodesInRegion). These are the
// pure helpers the `ttable-from-hub` file-input target also exercises; fuzzing them
// directly is faster (no process restart) and leak-clean (GetNodesInRegion's malloc
// is freed here — the file-input target covers the leakier ParseTrafficTable path).
#include <ctime>      // ttable_from_hub.cpp's (renamed) main uses time(); make it visible
#include <cstdint>
#include <cstddef>
#include <cstdlib>

#include <fuzzer/FuzzedDataProvider.h>

// Pull in the real implementation; rename its main() so it doesn't clash with the
// fuzzing engine's / standalone driver's main.
#define main ttable_from_hub_main
#include "ttable_from_hub.cpp"
#undef main

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider fdp(data, size);

    // Mesh / region geometry. Mins are kept >=1 so an empty input (all-min) is a valid,
    // non-degenerate config — the fuzzer mutates from there into the interesting space
    // (large node ids, lopsided regions) that stresses id2Coord's asserts and the
    // GetNodesInRegion size arithmetic.
    int dim_x        = fdp.ConsumeIntegralInRange<int>(1, 64);
    int dim_y        = fdp.ConsumeIntegralInRange<int>(1, 64);
    int dim_region_x = fdp.ConsumeIntegralInRange<int>(1, 16);
    int dim_region_y = fdp.ConsumeIntegralInRange<int>(1, 16);
    int frct         = fdp.ConsumeIntegralInRange<int>(0, 64);

    int nodes = dim_x * dim_y;
    int n1 = fdp.ConsumeIntegralInRange<int>(0, nodes - 1);
    int n2 = fdp.ConsumeIntegralInRange<int>(0, nodes - 1);

    // Routing-distance helpers (wired + wireless paths).
    (void) getWiredDistanceI(n1, n2, dim_x, dim_y, frct);
    (void) getWirelessDistance(n1, n2, dim_x, dim_y, dim_region_x, dim_region_y, frct);
    (void) prefersWirelessPath(n1, n2, dim_x, dim_y, dim_region_x, dim_region_y, frct);

    // Region enumeration: GetNodesInRegion malloc-sizes from (br-tl) deltas; feed it
    // arbitrary corners and free the result so the harness itself stays leak-clean.
    int tl = fdp.ConsumeIntegralInRange<int>(0, nodes - 1);
    int br = fdp.ConsumeIntegralInRange<int>(0, nodes - 1);
    int *region = nullptr;
    int region_len = 0;
    GetNodesInRegion(dim_x, br, tl, &region, &region_len);
    free(region);

    return 0;
}
