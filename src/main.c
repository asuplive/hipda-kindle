#include "app.h"
#include "hipda.h"

#include <stdio.h>
#include <string.h>

static void usage(const char *exe) {
    printf("hipda-kindle %s -- native HiPDA/Discuz reader for jailbroken Kindles\n\n"
           "usage: %s [options]\n"
           "  --config PATH    config.ini (default /mnt/us/hipda/config.ini)\n"
           "  --font PATH      HUF1 bitmap font (default fonts/unifont.bin)\n"
           "  --screen WxH@DPI override screen geometry (PC preview only)\n"
           "  --offline        only read cached pages, never touch the network\n"
           "  --no-login       skip logging.php entirely\n"
           "  --diag           show a high-contrast screen test (no font/net)\n"
           "  --dump PATH      fetch one forum path, print decoded UTF-8 to stdout\n"
           "  --export         render every screen and write preview/pages.json (PC)\n"
           "  --selftest       run parser/layout/font regression checks (PC)\n"
           "  --version\n", HIPDA_VERSION, exe);
}

int main(int argc, char **argv) {
    AppOptions opt;
    memset(&opt, 0, sizeof(opt));
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--config") && i + 1 < argc) opt.config_path = argv[++i];
        else if (!strcmp(a, "--font") && i + 1 < argc) opt.font_path = argv[++i];
        else if (!strcmp(a, "--screen") && i + 1 < argc) opt.screen = argv[++i];
        else if (!strcmp(a, "--dump") && i + 1 < argc) { opt.dump_target = argv[++i]; opt.dump_url = 1; }
        else if (!strcmp(a, "--offline")) opt.offline = 1;
        else if (!strcmp(a, "--no-login")) opt.no_login = 1;
        else if (!strcmp(a, "--diag")) opt.diag = 1;
        else if (!strcmp(a, "--export")) opt.export_preview = 1;
        else if (!strcmp(a, "--selftest")) opt.selftest = 1;
        else if (!strcmp(a, "--version")) { printf("%s\n", HIPDA_VERSION); return 0; }
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(argv[0]); return 0; }
        else { fprintf(stderr, "unknown option: %s\n", a); usage(argv[0]); return 2; }
    }
    if (opt.selftest) return app_selftest();
    return app_run(&opt);
}
