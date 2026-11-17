#ifndef HIPDA_CONFIG_H
#define HIPDA_CONFIG_H

#include "hipda.h"

void config_defaults(Config *c, int pc_build);
int  config_load(Config *c, const char *path);
int  config_save(const Config *c);      /* write theme/login back to disk */
void config_dump(const Config *c);

#endif /* HIPDA_CONFIG_H */
