#ifndef UI_H
#define UI_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { KEV_PRESS, KEV_LONG } key_ev_t;

void ui_init(void);
void ui_key(uint8_t key, key_ev_t ev);     /* key = KEY_* bit */
void ui_tick(uint32_t now);                /* every 100 ms: renders the LCD */
void ui_render(char rows[4][20]);          /* current screen text (used by tests and CLI) */
bool ui_in_service(void);

#endif
