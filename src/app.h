#ifndef HIPDA_APP_H
#define HIPDA_APP_H

typedef struct {
    const char *config_path;
    const char *font_path;
    const char *screen;        /* "WxH@DPI" override, PC only */
    int  export_preview;
    int  selftest;
    int  offline;
    int  no_login;
    int  diag;                 /* draw a high-contrast panel test, no font/net */
    int  dump_url;             /* print one decoded URL response and exit */
    const char *dump_target;
} AppOptions;

int app_run(const AppOptions *opt);
int app_selftest(void);

#endif /* HIPDA_APP_H */
