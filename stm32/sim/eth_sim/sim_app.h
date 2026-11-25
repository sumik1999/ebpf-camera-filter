#ifndef SIM_APP_H
#define SIM_APP_H
#include <stdint.h>
extern uint32_t g_echo_rx;          /* echo requests received by the app */
extern uint32_t g_ctrl_ok, g_ctrl_refused;   /* rulebooks installed / refused (port 9000) */
void sim_app_init(void);
void sim_fatal(const char *why);
#endif
