#ifndef DDOS_PROTOTYPE_H
#define DDOS_PROTOTYPE_H

/* Start-up wiring of the DDoS prototype (see ddos_prototype_init.c and
 * exp_config.h). Both return 0 on success, -1 if a program was refused or
 * the config number is invalid. */

#ifdef __cplusplus
extern "C" {
#endif

int ddos_prototype_init(void);              /* build-time EXP_CONFIG     */
int ddos_prototype_init_config(int cfg);    /* run-time choice, 0..4     */
const char *ddos_config_name(int cfg);      /* e.g. "C4: VM bytecode at driver hook" */

#ifdef __cplusplus
}
#endif

#endif /* DDOS_PROTOTYPE_H */
