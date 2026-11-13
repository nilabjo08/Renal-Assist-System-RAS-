/* Automated kit / board test (v1.2). `kittest [1-5|all] [quick|long]` over USB runs the checks
 * for one build stage (or all of them), decides PASS / FAIL against fixed limits, asks for the few
 * manual actions on the LCD and USB (START or ENTER = yes / done, STOP = no; USB: ok, no, skip),
 * logs every result as an event, keeps progress and results in FRAM (so the power-cycle check can
 * survive the power cycle), and prints a report: one line per item, then a summary line.
 * Valves are only opened with RO water off (S3 below 5 psig or not connected). */
#ifndef KITTEST_H
#define KITTEST_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { KS_NONE = 0, KS_PASS, KS_FAIL, KS_SKIP, KS_INFO } kt_status_t;
typedef enum { KA_NONE = 0, KA_OK, KA_NO, KA_SKIP } kt_answer_t;

typedef void (*kt_print_t)(const char *line);

void kittest_init(kt_print_t print);       /* at boot, after the CLI: resumes a test across the power-cycle step */
bool kittest_start(uint8_t stage, bool quick, bool longer, char *why, uint32_t why_len);   /* stage 0 = all */
void kittest_abort(void);
void kittest_answer(kt_answer_t a);
void kittest_key(uint8_t key);             /* keypad while a test runs */
void kittest_tick(uint32_t now);           /* every 10 ms */
bool kittest_active(void);
bool kittest_leds(bool *green, bool *red); /* true = the test drives the LEDs now */
void kittest_lcd(char rows[4][20]);
const char *kittest_prompt_code(void);     /* "" when not waiting for the technician */

/* report access (CLI) */
uint32_t kittest_items(void);
bool kittest_report_line(uint32_t i, char *buf, uint32_t len);   /* false = item not part of the last run */
void kittest_summary(char *buf, uint32_t len);

#endif
