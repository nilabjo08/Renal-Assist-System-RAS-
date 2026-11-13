#include "util.h"

uint16_t crc16_ccitt(const void *data, size_t len, uint16_t seed)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = seed;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (uint8_t b = 0; b < 8u; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

void sb_init(sb_t *s, char *buf, size_t cap)
{
    s->buf = buf; s->cap = cap; s->len = 0;
    if (cap) buf[0] = '\0';
}

void sb_char(sb_t *s, char c)
{
    if (s->len + 1u < s->cap) { s->buf[s->len++] = c; s->buf[s->len] = '\0'; }
}

void sb_str(sb_t *s, const char *str)
{
    while (*str) sb_char(s, *str++);
}

void sb_uint_w(sb_t *s, uint32_t v, uint8_t width, char fill)
{
    char tmp[11];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && n < sizeof tmp);
    while (width > n) { sb_char(s, fill); width--; }
    while (n) sb_char(s, tmp[--n]);
}

void sb_int(sb_t *s, int32_t v)
{
    if (v < 0) { sb_char(s, '-'); sb_uint_w(s, (uint32_t)(-(v + 1)) + 1u, 0, ' '); }
    else sb_uint_w(s, (uint32_t)v, 0, ' ');
}

void sb_fixed1(sb_t *s, int32_t v)
{
    if (v < 0) { sb_char(s, '-'); v = -v; }
    sb_uint_w(s, (uint32_t)(v / 10), 0, ' ');
    sb_char(s, '.');
    sb_char(s, (char)('0' + (v % 10)));
}

void sb_pad(sb_t *s, size_t total)
{
    while (s->len < total && s->len + 1u < s->cap) sb_char(s, ' ');
}

void lcd_fmt(char row[20], const char *text)
{
    uint8_t i = 0;
    while (i < 20u && text[i]) { row[i] = text[i]; i++; }
    while (i < 20u) row[i++] = ' ';
}

static bool leap(uint16_t y) { return (y % 4u == 0u && y % 100u != 0u) || y % 400u == 0u; }
static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

void date_from_secs(uint32_t t, uint16_t *y, uint8_t *mo, uint8_t *d, uint8_t *h, uint8_t *mi, uint8_t *s)
{
    *s = (uint8_t)(t % 60u); t /= 60u;
    *mi = (uint8_t)(t % 60u); t /= 60u;
    *h = (uint8_t)(t % 24u); t /= 24u;
    uint16_t yr = 2000;
    for (;;) { uint32_t dy = leap(yr) ? 366u : 365u; if (t < dy) break; t -= dy; yr++; }
    uint8_t m = 0;
    for (;;) { uint32_t dm = mdays[m] + ((m == 1u && leap(yr)) ? 1u : 0u); if (t < dm) break; t -= dm; m++; }
    *y = yr; *mo = (uint8_t)(m + 1u); *d = (uint8_t)(t + 1u);
}

uint32_t secs_from_date(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s)
{
    uint32_t days = 0;
    for (uint16_t yr = 2000; yr < y; yr++) days += leap(yr) ? 366u : 365u;
    for (uint8_t m = 0; m + 1u < mo && m < 12u; m++) days += mdays[m] + ((m == 1u && leap(y)) ? 1u : 0u);
    days += (uint32_t)d - 1u;
    return ((days * 24u + h) * 60u + mi) * 60u + s;
}
