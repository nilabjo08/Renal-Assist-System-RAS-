/* Non-volatile storage on the MB85RC512T FRAM (64 KB). Every block carries a CRC16.
 *
 *  0x0000  calibration copy A (64 B)       0x0040  calibration copy B
 *  0x0080  settings copy A   (64 B)        0x00C0  settings copy B
 *  0x0100  run state         (32 B)        power-loss detection
 *  0x0120  self-test scratch (32 B)        written and read back at every power-on
 *  0x0140  kit-test record   (<= 704 B)    v1.2: progress + results, survives the power-cycle step
 *  0x0400  dialyser table    256 x 8 B     baseline TCV per dialyser ID
 *  0x0C00  cycle log         2082 x 30 B   ring buffer (PRD FW-06: >= 2,000 records)
 */
#ifndef STORE_H
#define STORE_H
#include <stdint.h>
#include <stdbool.h>

#define NV_CAL_A      0x0000u
#define NV_CAL_B      0x0040u
#define NV_SET_A      0x0080u
#define NV_SET_B      0x00C0u
#define NV_RUN        0x0100u
#define NV_SCRATCH    0x0120u
#define NV_KITTEST    0x0140u
#define NV_KITTEST_MAX (NV_DLZ - NV_KITTEST)
#define NV_DLZ        0x0400u
#define DLZ_ENTRIES   256u
#define NV_LOG        0x0C00u
#define LOG_REC_SIZE  30u
#define LOG_CAPACITY  ((65536u - NV_LOG) / LOG_REC_SIZE)   /* 2082 */

typedef struct {
    uint16_t magic;
    int32_t  scale_zero;        /* HX711 raw counts at 0 g               */
    int32_t  scale_cpg_x100;    /* HX711 counts per gram x100            */
    int16_t  s2_zero_x10;       /* S2 offset, mmHg x10                   */
    int16_t  s3_zero_x10;       /* S3 offset, psig x10                   */
    int16_t  wf_x10;            /* fixed line volume WF for TCV, ml x10  */
    uint16_t flow_k_x10;        /* flow sensor Hz per L/min x10          */
    int16_t  ph_off_x10;
    uint16_t ph_gain_x1000;
    uint8_t  reserved[34];
    uint16_t crc;
} cal_t;

typedef struct {
    uint16_t magic;
    uint16_t service_pin;
    uint8_t  drv_fault_monitor;   /* 1 = DRV8860 nFAULT stops the cycle   */
    uint8_t  germicide_in_lines;  /* set by disinfection, cleared by rinse-out (SAN-03) */
    uint16_t log_head;            /* next record slot                     */
    uint16_t log_count;
    uint32_t log_seq;             /* total cycles ever logged             */
    uint32_t disinfect_time;      /* RTC when disinfection finished       */
    uint16_t tmo_pct;             /* v1.1: step time-limit scale, 0 = 100 % */
    uint8_t  pump_check_off;      /* v1.1: 1 = no pump open-wire check     */
    uint8_t  trace;               /* v1.1: 1 = stream step records on USB  */
    uint8_t  drv_hold;            /* v1.1: 0 = valves 100 % on; 1-7 = hold PWM n x 12.5 % after 110 ms */
    uint32_t boot_count;          /* v1.2: power-ons / resets since first boot */
    char     serial[12];          /* v1.2: board serial, `set serial`, printed on every report */
    uint8_t  reserved[18];
    uint16_t crc;
} settings_t;

typedef struct {
    uint16_t magic;
    uint8_t  active;          /* 1 while a cycle is running            */
    uint8_t  program;
    uint8_t  stage;
    uint8_t  germicide;       /* 1 once germicide entered the dialyser */
    uint8_t  pad[2];
    uint32_t dlz_id;
    uint32_t start_time;
    uint8_t  reserved[14];
    uint16_t crc;
} runstate_t;

typedef struct {
    uint32_t id;              /* 0 = empty slot                       */
    uint16_t baseline_x10;    /* first-use TCV, ml x10                */
    uint8_t  dtype;           /* 1 low, 2 middle, 3 high flux         */
    uint8_t  uses;            /* reprocess count (saturates at 255)   */
} dlz_entry_t;

/* Log record, exactly 30 bytes on FRAM */
typedef struct __attribute__((packed)) {
    uint32_t seq;
    uint32_t time;
    uint32_t dlz_id;
    uint8_t  program;
    uint8_t  result;          /* LOG_* flags                           */
    uint16_t tcv_x10;
    uint16_t baseline_x10;
    uint16_t pct_x10;
    int16_t  leak_x10;        /* vacuum loss over the hold, mmHg x10   */
    uint8_t  alarm;
    uint8_t  stage;           /* stage reached                         */
    uint16_t duration_s;
    uint8_t  uses;
    uint8_t  dtype;
    uint16_t crc;
} log_rec_t;

#define LOG_COMPLETE  0x01u
#define LOG_PASS      0x02u
#define LOG_TCV_FAIL  0x04u
#define LOG_LEAK_FAIL 0x08u
#define LOG_ABORTED   0x10u
#define LOG_FIRST_USE 0x20u

typedef enum { STORE_OK = 0, STORE_CAL_DEFAULTS = 1, STORE_SET_DEFAULTS = 2 } store_status_t;

uint8_t     store_init(void);                 /* returns STORE_* bitmask */
cal_t      *store_cal(void);
settings_t *store_settings(void);
bool        store_save_cal(void);
bool        store_save_settings(void);
void        store_cal_defaults(cal_t *c);

bool store_run_write(const runstate_t *r);
bool store_run_read(runstate_t *r);           /* false if no valid record */
void store_run_clear(void);

bool store_selftest(void);                    /* FRAM write/read-back at NV_SCRATCH */
bool store_kt_read(void *buf, uint32_t len);  /* kit-test record */
bool store_kt_write(const void *buf, uint32_t len);
bool store_healthy(void);                     /* false once any FRAM access failed since boot */
uint16_t store_tmo_pct(void);                 /* 100..300 */

bool store_dlz_find(uint32_t id, dlz_entry_t *out);
bool store_dlz_put(const dlz_entry_t *e);     /* insert or update; evicts oldest slot when full */

bool     store_log_append(log_rec_t *rec);    /* fills seq and crc */
uint16_t store_log_count(void);
bool     store_log_get(uint16_t newest_index, log_rec_t *out);   /* 0 = newest, CRC checked */
void     store_log_clear(void);

#endif
