#ifndef CLI_H
#define CLI_H
void cli_init(void);
void cli_poll(void);
void cli_line(const char *s);   /* one line out on USB (non-blocking; dropped if the buffer is full) */
#endif
