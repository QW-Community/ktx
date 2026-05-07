/*
 * Unit tests for antilag_lagmove_all_proj().
 *
 * Ping adjustment: subtract min(frametime, 1/77 s) from the raw ping.
 * If frametime is zero (unavailable), fall back to 1/77 s.
 *
 * Stepping: use sv_mintic as the step size with no forced minimum floor.
 * The final step may be sub-mintic to land exactly on g_globalvars.time.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Stub headers override the real g_local.h / fb_globals.h.
 * Compile with -I ktx/tests/include -I ktx/include so the stubs take
 * precedence. */
#include "g_local.h"

/* -------------------------------------------------------------------------
 * Globals required by antilag.c (declared extern in the stub g_local.h).
 * -------------------------------------------------------------------------*/
globalvars_t  g_globalvars;
gedict_t      g_edicts[32];   /* small pool – tests only use indices 1 and 2 */
gedict_t     *world = &g_edicts[0];
gedict_t     *self;
gedict_t     *other;
gedict_t     *newmis;

/* vec3_origin and nanmask are declared extern in mathlib.h */
vec3_t vec3_origin = {0, 0, 0};
int    nanmask     = 0;

/* strlcpy is declared in q_shared.h for Windows/Linux builds */
size_t strlcpy(char *dst, const char *src, size_t siz)
{
    size_t len = strlen(src);
    if (siz > 0) {
        size_t n = len < siz ? len : siz - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

/* -------------------------------------------------------------------------
 * Configurable test state – set before each test case.
 * -------------------------------------------------------------------------*/
static float       stub_sv_antilag = 1.0f;
static float       stub_sv_mintic  = 0.013f;
static const char *stub_ping       = "13";

/* -------------------------------------------------------------------------
 * Stub implementations of engine syscalls used by antilag.c.
 * -------------------------------------------------------------------------*/
float min(float a, float b)           { return a < b ? a : b; }
float max(float a, float b)           { return a > b ? a : b; }
float bound(float a, float b, float c){ return b < a ? a : b > c ? c : b; }

int NUM_FOR_EDICT(gedict_t *e)
{
    return (int)((byte *)e - (byte *)g_edicts);
}

float cvar(const char *var)
{
    if (!strcmp(var, "sv_antilag")) return stub_sv_antilag;
    if (!strcmp(var, "sv_mintic"))  return stub_sv_mintic;
    return 0.0f;
}

char *ezinfokey(gedict_t *ed, char *key)
{
    (void)ed;
    if (!strcmp(key, "ping")) return (char *)stub_ping;
    return "";
}

/* No collision: set endpos = the supplied endpoint, fraction = 1. */
void traceline(float v1_x, float v1_y, float v1_z,
               float v2_x, float v2_y, float v2_z,
               int nomonst, gedict_t *ed)
{
    (void)v1_x; (void)v1_y; (void)v1_z;
    (void)nomonst; (void)ed;
    g_globalvars.trace_fraction   = 1.0f;
    g_globalvars.trace_startsolid = 0;
    g_globalvars.trace_endpos[0]  = v2_x;
    g_globalvars.trace_endpos[1]  = v2_y;
    g_globalvars.trace_endpos[2]  = v2_z;
    g_globalvars.trace_ent        = 0;
}

void trap_setorigin(intptr_t edn, float x, float y, float z)
{
    gedict_t *e          = PROG_TO_EDICT(edn);
    e->s.v.origin[0]     = x;
    e->s.v.origin[1]     = y;
    e->s.v.origin[2]     = z;
}

vec_t VectorLength(vec3_t v)
{
    return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

void VectorMA(vec3_t veca, float scale, vec3_t vecb, vec3_t vecc)
{
    vecc[0] = veca[0] + scale * vecb[0];
    vecc[1] = veca[1] + scale * vecb[1];
    vecc[2] = veca[2] + scale * vecb[2];
}

void VectorScale(vec3_t in, vec_t scale, vec3_t out)
{
    out[0] = in[0] * scale;
    out[1] = in[1] * scale;
    out[2] = in[2] * scale;
}

/* -------------------------------------------------------------------------
 * Test helpers.
 * -------------------------------------------------------------------------*/

/* Set up owner at the given position with a valid antilag_data block. */
static void setup_owner(gedict_t *owner, antilag_t *adata,
                        float x, float y, float z)
{
    memset(owner, 0, sizeof(*owner));
    memset(adata, 0, sizeof(*adata));
    adata->owner         = owner;
    owner->antilag_data  = adata;
    owner->s.v.origin[0] = x;
    owner->s.v.origin[1] = y;
    owner->s.v.origin[2] = z;
    owner->s.v.health    = 100.0f;  /* alive */
}

/* Set up a rocket in g_edicts[idx] so NUM_FOR_EDICT / PROG_TO_EDICT work. */
static gedict_t *setup_rocket(int idx, float vx)
{
    gedict_t *r = &g_edicts[idx];
    memset(r, 0, sizeof(*r));
    r->s.v.origin[0]   = 0.0f;
    r->s.v.velocity[0] = vx;
    return r;
}

static void reset_globals(void)
{
    memset(&g_globalvars, 0, sizeof(g_globalvars));
    g_globalvars.time = 1.0f;
    memset(g_edicts, 0, sizeof(g_edicts));
    self  = NULL;
    other = NULL;
    newmis = NULL;
    stub_sv_antilag = 1.0f;
    stub_sv_mintic  = 0.013f;
    stub_ping       = "13";
}

/* -------------------------------------------------------------------------
 * Test: sv_antilag != 1 → function is a no-op.
 * -------------------------------------------------------------------------*/
static void test_disabled_when_antilag_not_1(void)
{
    printf("test_disabled_when_antilag_not_1 ... \n");

    reset_globals();
    stub_sv_antilag = 2.0f;

    antilag_t   adata;
    gedict_t   *owner  = &g_edicts[1];
    gedict_t   *rocket = setup_rocket(2, 1000.0f);

    setup_owner(owner, &adata, 0, 0, 0);
    newmis = rocket;

    antilag_lagmove_all_proj(owner, rocket);

    /* Rocket must not have moved at all. */
    assert(rocket->s.v.origin[0] == 0.0f);

    printf("passed\n");
}

/* -------------------------------------------------------------------------
 * Test: 13 ms ping – after subtracting 1/77 s (~13 ms) the rewind window
 * goes to zero, so the while-loop does not run and the rocket only travels
 * the 50 ms newmis fastforward = 50 units.
 * -------------------------------------------------------------------------*/
static void test_13ms_ping_no_loop_travel(void)
{
    printf("test_13ms_ping_no_loop_travel ... \n");

    reset_globals();
    stub_ping = "13";
    /* frametime = 0 → fallback to 1/77 s; 13 ms - 13 ms = 0 */

    antilag_t   adata;
    gedict_t   *owner  = &g_edicts[1];
    gedict_t   *rocket = setup_rocket(2, 1000.0f);

    setup_owner(owner, &adata, 0, 0, 0);
    newmis = rocket;

    antilag_lagmove_all_proj(owner, rocket);

    assert(rocket->s.v.origin[0] == 50.0f);

    printf("passed\n");
}

/* -------------------------------------------------------------------------
 * Test: 0 ms ping → ms goes negative → clamped to 0 → same result.
 * -------------------------------------------------------------------------*/
static void test_0ms_ping_no_loop_travel(void)
{
    printf("test_0ms_ping_no_loop_travel ... \n");

    reset_globals();
    stub_ping = "0";

    antilag_t   adata;
    gedict_t   *owner  = &g_edicts[1];
    gedict_t   *rocket = setup_rocket(2, 1000.0f);

    setup_owner(owner, &adata, 0, 0, 0);
    newmis = rocket;

    antilag_lagmove_all_proj(owner, rocket);

    assert(rocket->s.v.origin[0] == 50.0f);

    printf("passed\n");
}

/* -------------------------------------------------------------------------
 * Test: frametime zero falls back to 1/77 s, and any frametime below 1/77 s
 * is floored to 1/77 s — preventing a fake high-fps client from gaining
 * extra rewind time.
 *
 * ping=26 ms, frametime=0     → floor to 1/77 ≈ 13 ms → ms ≈ 13 ms rewind.
 * ping=26 ms, frametime=0.010 → below floor, treated same as 1/77 → same ms.
 *
 * Both produce identical rocket positions.
 * -------------------------------------------------------------------------*/
static void test_frametime_zero_falls_back_to_1_over_77(void)
{
    printf("test_frametime_zero_falls_back_to_1_over_77 ... \n");

    antilag_t  adata;
    gedict_t  *owner;
    gedict_t  *rocket;

    /* Run with frametime = 0 (fallback to floor). */
    reset_globals();
    stub_ping = "26";
    g_globalvars.frametime = 0.0f;
    owner  = &g_edicts[1];
    rocket = setup_rocket(2, 1000.0f);
    setup_owner(owner, &adata, 0, 0, 0);
    newmis = rocket;
    antilag_lagmove_all_proj(owner, rocket);
    float pos_fallback = rocket->s.v.origin[0];

    /* Run with frametime = 0.010 (below 1/77 — floored to 1/77, same result). */
    reset_globals();
    stub_ping = "26";
    g_globalvars.frametime = 0.010f;
    owner  = &g_edicts[1];
    rocket = setup_rocket(2, 1000.0f);
    setup_owner(owner, &adata, 0, 0, 0);
    newmis = rocket;
    antilag_lagmove_all_proj(owner, rocket);
    float pos_explicit = rocket->s.v.origin[0];

    /* Sub-floor frametime is clamped — both cases subtract 1/77, same travel. */
    assert(pos_explicit == pos_fallback);

    printf("passed (fallback=%.2f explicit=%.2f)\n", pos_fallback, pos_explicit);
}

/* -------------------------------------------------------------------------
 * Test: frametime above 1/77 s is used in the ping subtraction.
 *
 * ping=26 ms, frametime=0.020 s (50 fps), sv_mintic=0.013 s, velocity=1000 u/s.
 * ms = 0.026 - 0.020 = 0.006 s rewind window.
 * Loop: one sub-mintic step of 0.006 s = 6 u.
 * With newmis fastforward: 50 + 6 = 56 u.
 * -------------------------------------------------------------------------*/
static void test_frametime_used_in_ping_subtraction(void)
{
    printf("test_frametime_used_in_ping_subtraction ... \n");

    reset_globals();
    stub_ping              = "26";
    stub_sv_mintic         = 0.013f;
    g_globalvars.frametime = 0.020f;

    antilag_t   adata;
    gedict_t   *owner  = &g_edicts[1];
    gedict_t   *rocket = setup_rocket(2, 1000.0f);

    setup_owner(owner, &adata, 0, 0, 0);
    newmis = rocket;

    antilag_lagmove_all_proj(owner, rocket);

    /* 50 (newmis) + 6 (loop: 0.006 s × 1000 u/s) = 56 units. */
    assert(fabsf(rocket->s.v.origin[0] - 56.0f) < 0.01f);

    printf("passed\n");
}

/* -------------------------------------------------------------------------
 * Test: no forced minimum step size.
 *
 * ping=26 ms, frametime=0.020 s, sv_mintic=0.010 s, velocity=1000 u/s.
 * ms = 0.026 - 0.020 = 0.006 s rewind window.
 * The 0.006 s window is smaller than sv_mintic (0.010 s), so the loop takes
 * one sub-mintic step of 0.006 s = 6 u.  The old bound(0.01,…) code would
 * have forced a 10 ms step, overshooting by 4 ms.
 *
 * Total travel = 50 (newmis) + 6 = 56 u (not the buggy 50+10=60 u).
 * -------------------------------------------------------------------------*/
static void test_no_minimum_step_size(void)
{
    printf("test_no_minimum_step_size ... \n");

    reset_globals();
    stub_ping              = "26";
    stub_sv_mintic         = 0.010f;
    g_globalvars.frametime = 0.020f;

    antilag_t   adata;
    gedict_t   *owner  = &g_edicts[1];
    gedict_t   *rocket = setup_rocket(2, 1000.0f);

    setup_owner(owner, &adata, 0, 0, 0);
    newmis = rocket;

    antilag_lagmove_all_proj(owner, rocket);

    /* Must be 56 u (correct), not 60 u (forced-minimum bug). */
    assert(fabsf(rocket->s.v.origin[0] - 56.0f) < 0.01f);

    printf("passed\n");
}

/* -------------------------------------------------------------------------
 * Test: large ping (100 ms, clamped to ANTILAG_REWIND_MAXPROJECTILE = 80 ms).
 * The rocket must travel further than the 50 ms newmis fastforward.
 * We don't pin the exact value — just verify it exceeds 50 units so the
 * loop ran, and doesn't exceed the physical maximum (80 ms × 1000 u/s = 80
 * loop units + 50 newmis = 130 u).
 * -------------------------------------------------------------------------*/
static void test_large_ping_steps_beyond_50ms(void)
{
    printf("test_large_ping_steps_beyond_50ms ... \n");

    reset_globals();
    stub_ping = "100";  /* 100 ms → ms clamped to 0.080 */

    antilag_t   adata;
    gedict_t   *owner  = &g_edicts[1];
    gedict_t   *rocket = setup_rocket(2, 1000.0f);

    setup_owner(owner, &adata, 0, 0, 0);
    newmis = rocket;

    antilag_lagmove_all_proj(owner, rocket);

    assert(rocket->s.v.origin[0] > 50.0f);
    assert(rocket->s.v.origin[0] <= 130.0f);

    printf("passed\n");
}

/* -------------------------------------------------------------------------
 * Test: non-newmis rocket (newmis points at a different entity) – the
 * newmis fastforward block is skipped; the while-loop drives the advance.
 * For 0 ms ping the while-loop does not execute → rocket stays at origin.
 * -------------------------------------------------------------------------*/
static void test_non_newmis_rocket_zero_ping(void)
{
    printf("test_non_newmis_rocket_zero_ping ... \n");

    reset_globals();
    stub_ping = "0";

    antilag_t   adata;
    gedict_t   *owner   = &g_edicts[1];
    gedict_t   *rocket  = setup_rocket(2, 1000.0f);
    gedict_t   *other_e = &g_edicts[3];
    memset(other_e, 0, sizeof(*other_e));

    setup_owner(owner, &adata, 0, 0, 0);
    newmis = other_e;   /* rocket is NOT the newmis */

    antilag_lagmove_all_proj(owner, rocket);

    /* ms = 0, while-loop does not run → rocket unmoved. */
    assert(rocket->s.v.origin[0] == 0.0f);

    printf("passed\n");
}

/* -------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/
int main(void)
{
    test_disabled_when_antilag_not_1();
    test_13ms_ping_no_loop_travel();
    test_0ms_ping_no_loop_travel();
    test_frametime_zero_falls_back_to_1_over_77();
    test_frametime_used_in_ping_subtraction();
    test_no_minimum_step_size();
    test_large_ping_steps_beyond_50ms();
    test_non_newmis_rocket_zero_ping();

    printf("All antilag tests passed.\n");
    return 0;
}
