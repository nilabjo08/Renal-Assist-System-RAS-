#ifndef UTIL_H
#define UTIL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

uint16_t crc16_ccitt(const void *data, size_t len, uint16_t seed);

/* Bounded string builder (no printf in the firmware). */
typedef struct { char *buf; size_t cap; size_t len; } sb_t;
void sb_init(sb_t *s, char *buf, size_t cap);
void sb_str(sb_t *s, const char *str);
void sb_char(sb_t *s, char c);
void sb_int(sb_t *s, int32_t v);
void sb_uint_w(sb_t *s, uint32_t v, uint8_t width, char fill);   /* right-aligned */
void sb_fixed1(sb_t *s, int32_t v_x10);                         /* 123 -> "12.3" */
void sb_pad(sb_t *s, size_t total);                             /* pad with spaces */

/* Copy into a 20-char LCD row, space padded, no terminator needed. */
void lcd_fmt(char row[20], const char *text);

void date_from_secs(uint32_t secs, uint16_t *y, uint8_t *mo, uint8_t *d, uint8_t *h, uint8_t *mi, uint8_t *s);
uint32_t secs_from_date(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s);

static inline int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline uint32_t elapsed(uint32_t now, uint32_t since) { return now - since; }  /* wrap-safe */

#endif
