#include "recipes.h"
#include "config.h"
#include <stddef.h>

/* PRD section 3 "Default recipes (grams on the load cell; client deck slide 31)". */
static const recipe_t table[] = {
    /* type     prime     clean     sterilant  DI   BD */
    { DT_LOW,  25,  80,   9, 391,  17, 483,  250, 250 },
    { DT_LOW,  81, 250,  11, 489,  17, 483,  250, 250 },
    { DT_MID,  25,  70,  11, 489,  17, 483,  250, 250 },
    { DT_MID,  71,  90,  16, 684,  17, 483,  250, 250 },
    { DT_MID,  91, 250,  20, 880,  17, 483,  250, 250 },
    { DT_HIGH, 25,  90,  16, 684,  22, 678,  350, 350 },
    { DT_HIGH, 91, 250,  20, 880,  22, 678,  350, 350 },
};

static const recipe_t disinfect = { 0, 0, 0, 0, 0, 32, 968, 0, 0 };   /* 3 % daily disinfection */

const recipe_t *recipe_find(uint8_t dtype, uint16_t prime_ml)
{
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (table[i].dtype == dtype && prime_ml >= table[i].prime_min && prime_ml <= table[i].prime_max)
            return &table[i];
    return NULL;
}

const recipe_t *recipe_disinfect(void) { return &disinfect; }

const char *program_name(program_t p)
{
    static const char *const n[PROG_COUNT] = { "LOW FLUX", "MIDDLE FLUX", "HIGH FLUX", "PRE-CLEAN", "DISINFECT", "RINSE-OUT",
                                               "DRAIN TANK", "FILL TANK", "PRIME LINES", "SEAL TEST" };
    return p < PROG_COUNT ? n[p] : "?";
}

const char *program_cli_name(program_t p)
{
    static const char *const n[PROG_COUNT] = { "low", "mid", "high", "preclean", "disinfect", "rinseout",
                                               "drain", "fill", "prime", "seal" };
    return p < PROG_COUNT ? n[p] : "?";
}

const char *dtype_name(uint8_t dtype)
{
    switch (dtype) { case DT_LOW: return "LOW"; case DT_MID: return "MIDDLE"; case DT_HIGH: return "HIGH"; default: return "-"; }
}

uint8_t program_dtype(program_t p)
{
    return p == PROG_LOW ? DT_LOW : p == PROG_MID ? DT_MID : p == PROG_HIGH ? DT_HIGH : 0u;
}

uint16_t leak_limit_x10(uint8_t dtype)
{
    return dtype == DT_HIGH ? LEAK_LIMIT_HIGH_X10 : LEAK_LIMIT_LOWMID_X10;
}
