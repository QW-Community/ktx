/* Minimal stub of g_local.h for antilag unit tests.
 * Provides only the types, macros, and declarations used by antilag.c.
 */
#ifndef G_LOCAL_TEST_STUB_H
#define G_LOCAL_TEST_STUB_H

#include "q_shared.h"   /* qbool, string_t, func_t, vec_t, vec3_t, byte */
#include "mathlib.h"    /* VectorCopy/Add/etc macros, VectorLength/VectorMA decls */
#include "progs.h"      /* entvars_t, edict_t, gedict_t, antilag_t, globalvars_t,
                           ANTILAG_* constants (progs.h includes progdefs.h) */
#include "g_consts.h"   /* FL_GODMODE, FL_ONGROUND, SOLID_TRIGGER */

#undef max
#undef min

float min(float a, float b);
float max(float a, float b);
float bound(float a, float b, float c);

#define EDICT_TO_PROG(e) ((byte *)(e) - (byte *)g_edicts)
#define PROG_TO_EDICT(e) ((gedict_t *)((byte *)g_edicts + (e)))
#define PASSVEC3(x)      (x[0]),(x[1]),(x[2])
#define FOFS(x)          ((intptr_t)&(((gedict_t *)0)->x))

int NUM_FOR_EDICT(gedict_t *e);

extern globalvars_t g_globalvars;
extern gedict_t     g_edicts[];
extern gedict_t    *world;
extern gedict_t    *self, *other;
extern gedict_t    *newmis;
extern float        time_corrected;

#define PRDFL_MIDAIR   1
#define PRDFL_COILGUN  2
#define PRDFL_FORCEOFF 255

float  cvar(const char *var);
char  *ezinfokey(gedict_t *ed, char *key);
void   traceline(float v1_x, float v1_y, float v1_z,
                 float v2_x, float v2_y, float v2_z,
                 int nomonst, gedict_t *ed);
void   trap_setorigin(intptr_t edn, float x, float y, float z);

#define G_SETLASTRUNTIME 0
#define HAVEEXTENSION(x) (0)
static inline void trap_SetLastRuntime(intptr_t edn) { (void)edn; }
static inline void SetLastRuntime(gedict_t *ed) { (void)ed; }

void antilag_lagmove_all_proj(gedict_t *owner, gedict_t *e);
void antilag_lagmove_all_proj_bounce(gedict_t *owner, gedict_t *e);
void antilag_unmove_all(void);

#endif /* G_LOCAL_TEST_STUB_H */
