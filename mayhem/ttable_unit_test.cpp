// Known-answer unit test for noxim's traffic-table helper functions
// (other/ttable_from_hub.cpp). These are the exact routing/region routines exercised
// by the `ttable-from-hub` file-input target and the `ttable-parse` libFuzzer harness,
// so this suite is the functional oracle: it asserts concrete computed VALUES, so a
// no-op / `return`-only patch to any of these functions fails it (anti-reward-hacking).
//
// Hand-derived expected values (mesh is row-major: x = id % dim_x, y = id / dim_x):
//   id2Coord, getWiredDistanceC/I, getClosestNodeAttachedToRadioHubC,
//   getWirelessDistance, prefersWirelessPath, GetNodesInRegion.
#include <ctime>     // ttable_from_hub.cpp's (renamed) main uses time(); make it visible
#include <cstdio>
#include <cstdlib>

#define main ttable_from_hub_main
#include "ttable_from_hub.cpp"
#undef main

static int n = 0, passed = 0, failed = 0;

static void check(const char *desc, long got, long want) {
    n++;
    if (got == want) { passed++; printf("ok %d - %s (= %ld)\n", n, desc, want); }
    else { failed++; printf("not ok %d - %s: got %ld want %ld\n", n, desc, got, want); }
}

int main() {
    // id2Coord: row-major decode + (active) assert bounds.
    check("id2Coord(5,4,4).x",  id2Coord(5, 4, 4).x, 1);
    check("id2Coord(5,4,4).y",  id2Coord(5, 4, 4).y, 1);
    check("id2Coord(0,8,8).x",  id2Coord(0, 8, 8).x, 0);
    check("id2Coord(0,8,8).y",  id2Coord(0, 8, 8).y, 0);
    check("id2Coord(15,4,4).x", id2Coord(15, 4, 4).x, 3);
    check("id2Coord(15,4,4).y", id2Coord(15, 4, 4).y, 3);

    // getWiredDistanceC: Manhattan distance + frct_threshold.
    Coord a{0, 0}, b{3, 4}, c{1, 1};
    check("wiredC((0,0),(3,4),0)", getWiredDistanceC(a, b, 0), 7);
    check("wiredC((1,1),(1,1),5)", getWiredDistanceC(c, c, 5), 5);

    // getWiredDistanceI: by node id.
    check("wiredI(0,15,4,4,0)", getWiredDistanceI(0, 15, 4, 4, 0), 6);
    check("wiredI(0,15,4,4,2)", getWiredDistanceI(0, 15, 4, 4, 2), 8);

    // getClosestNodeAttachedToRadioHubC: closest of the 4 hub routers.
    Coord origin{0, 0};
    Coord rh = getClosestNodeAttachedToRadioHubC(origin, 4, 4, 0);
    check("closestRH((0,0),4,4).x", rh.x, 1);
    check("closestRH((0,0),4,4).y", rh.y, 1);

    // getWirelessDistance: wired(n1->rh1) + wired(n2->rh2) + 1.
    check("wireless(0,0,8,8,4,4,0)",   getWirelessDistance(0, 0, 8, 8, 4, 4, 0), 5);

    // prefersWirelessPath: wired >= wireless ?
    check("prefersWireless(0,0,8,8,4,4,0)",  prefersWirelessPath(0, 0, 8, 8, 4, 4, 0) ? 1 : 0, 0);
    check("prefersWireless(0,63,8,8,4,4,0)", prefersWirelessPath(0, 63, 8, 8, 4, 4, 0) ? 1 : 0, 1);

    // GetNodesInRegion: enumerate the (tl..br) rectangle in a dim_x-wide mesh.
    {
        int *r = nullptr; int len = 0;
        GetNodesInRegion(4, /*br*/5, /*tl*/0, &r, &len);
        check("region(4,br=5,tl=0).len", len, 4);
        check("region[0]", r ? r[0] : -1, 0);
        check("region[1]", r ? r[1] : -1, 1);
        check("region[2]", r ? r[2] : -1, 4);
        check("region[3]", r ? r[3] : -1, 5);
        free(r);
    }
    {
        int *r = nullptr; int len = 0;
        GetNodesInRegion(8, /*br*/0, /*tl*/0, &r, &len);
        check("region(8,br=0,tl=0).len", len, 1);
        check("region[0]", r ? r[0] : -1, 0);
        free(r);
    }

    printf("1..%d\n", n);
    printf("TESTS total=%d passed=%d failed=%d\n", n, passed, failed);
    return failed ? 1 : 0;
}
