#include "../bsp.h"
#include "../../app/app.h"

int main(void)
{
    bsp_init();
    app_init();
    for (;;) app_poll();
}
