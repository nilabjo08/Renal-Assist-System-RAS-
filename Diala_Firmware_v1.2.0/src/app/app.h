/* Application core shared by the target (main.c) and the host simulator. */
#ifndef APP_H
#define APP_H
#include <stdint.h>
#include <stdbool.h>
void app_init(void);
void app_poll(void);          /* call continuously from the main loop */
bool app_tables_ok(void);
#endif
