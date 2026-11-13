#ifndef RECIPES_H
#define RECIPES_H
#include <stdint.h>

typedef enum {
    PROG_LOW = 0,      /* full reprocess, low flux    (UFR < 8)     */
    PROG_MID,          /* full reprocess, middle flux (UFR 8-20)    */
    PROG_HIGH,         /* full reprocess, high flux   (UFR > 20)    */
    PROG_PRECLEAN,     /* stages 1-5 only                           */
    PROG_DISINFECT,    /* daily machine disinfection (SAN-01)       */
    PROG_RINSEOUT,     /* rinse-out after disinfection (SAN-03)     */
    PROG_USER_COUNT,   /* programs above are on the operator home screen */
    PROG_SVC_DRAIN = PROG_USER_COUNT,  /* service: empty the mix tank              */
    PROG_SVC_FILL,     /* service: fill the tank to 400 g (scale + fill rate check)    */
    PROG_SVC_PRIME,    /* service: purge air, flush all paths (dialyser or bypass)     */
    PROG_SVC_SEAL,     /* service: closed-valve seal test, four dialyser ports capped  */
    PROG_COUNT
} program_t;
#define PROG_IS_SERVICE(p) ((p) >= PROG_SVC_DRAIN && (p) < PROG_COUNT)

typedef enum { DT_LOW = 1, DT_MID = 2, DT_HIGH = 3 } dtype_t;

typedef struct {
    uint8_t  dtype;
    uint16_t prime_min, prime_max;      /* ml                                  */
    uint16_t clean_conc, clean_water;   /* g; 2.2 % cleaning solution          */
    uint16_t ster_conc, ster_water;     /* g; 3 % sterilant                    */
    uint16_t di_fill, bd_fill;          /* g drawn into dialysate / blood side */
} recipe_t;

const recipe_t *recipe_find(uint8_t dtype, uint16_t prime_ml);   /* NULL if out of range */
const recipe_t *recipe_disinfect(void);
const char *program_name(program_t p);     /* <= 12 chars */
const char *program_cli_name(program_t p); /* lower-case keyword used by the CLI and the log */
const char *dtype_name(uint8_t dtype);
uint8_t program_dtype(program_t p);        /* PROG_LOW..HIGH -> DT_*, else 0 */
uint16_t leak_limit_x10(uint8_t dtype);

#endif
