/* Power-on self-test (v1.1). Runs once after every reset while the splash screen is shown,
 * without blocking the main loop: FRAM write/read-back, supply rails, load cell, S2 and S3
 * transducers, then the valve rail is switched on with every output off to check the rail
 * voltage and the DRV8860 fault line, and switched off again. Results are shown on the
 * self-test screen, by the `diag` command, and raise the matching alarms. */
#ifndef POST_H
#define POST_H
#include <stdint.h>
#include <stdbool.h>

#define POST_FRAM    0x0001u   /* FRAM write / read-back failed                 */
#define POST_RAILS   0x0002u   /* 24 V or 5 V outside limits                    */
#define POST_HX711   0x0004u   /* no load-cell samples                          */
#define POST_S2      0x0008u   /* S2 bus transducer outside 0.25-4.75 V         */
#define POST_S3      0x0010u   /* S3 supply transducer outside 0.25-4.75 V      */
#define POST_VRAIL   0x0020u   /* valve rail did not come up to 20 V            */
#define POST_DRV     0x0040u   /* nFAULT low with all outputs off after reset   */
#define POST_IMAGE   0x0080u   /* v1.2: firmware image CRC does not match       */
#define POST_INFO_HSI 0x0100u  /* information: running on the internal clock    */
#define POST_INFO_LSI 0x0200u  /* information: RTC on LSI (Y2 not running)      */
#define POST_INFO_CPU 0x0400u  /* information: restarted after a CPU fault / watchdog */
#define POST_FAIL_MASK 0x00FFu

void     post_start(uint32_t now);
void     post_tick(uint32_t now);    /* every 10 ms, after sensors_tick and out_tick */
bool     post_done(void);
uint16_t post_result(void);
const char *post_item_name(uint16_t bit);   /* <= 12 chars */

#endif
