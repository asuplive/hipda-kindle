#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(Config *c, int pc_build) {
    memset(c, 0, sizeof(*c));
    str_copy(c->forum_server, sizeof(c->forum_server), "https://www.4d4y.com");
    str_copy(c->image_host, sizeof(c->image_host), "https://img02.4d4y.com");
    str_copy(c->net_cmd, sizeof(c->net_cmd), "curl");
    str_copy(c->user_agent, sizeof(c->user_agent),
             "Mozilla/5.0 (compatible; HiPDA-Kindle/0.9)");
    c->full_refresh_every = 6;
    c->timeout = 20;
    c->font_scale = 0;
    c->refresh_mode = 3;            /* hybrid: DU + full flash every N */
    c->rotation = 0;                /* upright by default               */
    c->img_mode = 0;                /* placeholders only, tap to view   */
    c->nforums = 0;                 /* 0 = use the full built-in catalog */
    if (pc_build) {
        str_copy(c->config_path, sizeof(c->config_path), "config.ini");
        str_copy(c->font_file, sizeof(c->font_file), "fonts/reading.bin");
        str_copy(c->cache_dir, sizeof(c->cache_dir), ".cache/http");
        str_copy(c->cookie_file, sizeof(c->cookie_file), ".cache/cookies.txt");
    } else {
        str_copy(c->config_path, sizeof(c->config_path), "/mnt/us/hipda/config.ini");
        str_copy(c->font_file, sizeof(c->font_file), "/mnt/us/hipda/fonts/reading.bin");
        str_copy(c->cache_dir, sizeof(c->cache_dir), "/mnt/us/hipda/cache");
        str_copy(c->cookie_file, sizeof(c->cookie_file), "/mnt/us/hipda/cookies.txt");
    }
}

static void set_int(const char *v, int *dst) { *dst = parse_int(v, *dst); }

static void set_str(const char *v, char *dst, size_t cap) {
    str_copy(dst, cap, str_trim((char *)v));
}

int config_load(Config *c, const char *path) {
    if (path && *path) str_copy(c->config_path, sizeof(c->config_path), path);
    FILE *f = fopen(c->config_path, "r");
    if (!f) {
        log_msg("config: %s not found, using defaults", c->config_path);
        return -1;
    }
    char line[HIPDA_PATH_LEN];
    while (fgets(line, sizeof(line), f)) {
        char *p = str_trim(line);
        if (!*p || *p == '#' || *p == ';' || *p == '[') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = str_trim(p);
        char *val = str_trim(eq + 1);
        /* NOTE: str_caseeq() returns 1 when equal (strcmp style, not strcmp's
         * "nonzero = different"). Every branch here must NOT be negated —
         * the earlier `!str_caseeq` form made each line assign to the first
         * field it did *not* match, so no setting was ever applied. */
        if      (str_caseeq(key, "forum_server")) set_str(val, c->forum_server, sizeof(c->forum_server));
        else if (str_caseeq(key, "image_host"))   set_str(val, c->image_host, sizeof(c->image_host));
        else if (str_caseeq(key, "username"))     set_str(val, c->username, sizeof(c->username));
        else if (str_caseeq(key, "password_md5")) set_str(val, c->password_md5, sizeof(c->password_md5));
        else if (str_caseeq(key, "cookie_file"))  set_str(val, c->cookie_file, sizeof(c->cookie_file));
        else if (str_caseeq(key, "font_file"))    set_str(val, c->font_file, sizeof(c->font_file));
        else if (str_caseeq(key, "cache_dir"))    set_str(val, c->cache_dir, sizeof(c->cache_dir));
        else if (str_caseeq(key, "net_cmd"))      set_str(val, c->net_cmd, sizeof(c->net_cmd));
        else if (str_caseeq(key, "user_agent"))   set_str(val, c->user_agent, sizeof(c->user_agent));
        else if (str_caseeq(key, "font_scale"))   set_int(val, &c->font_scale);
        else if (str_caseeq(key, "full_refresh_every")) set_int(val, &c->full_refresh_every);
        else if (str_caseeq(key, "timeout"))      set_int(val, &c->timeout);
        else if (str_caseeq(key, "offline"))      set_int(val, &c->offline);
        else if (str_caseeq(key, "no_login"))     set_int(val, &c->no_login);
        else if (str_caseeq(key, "theme"))        set_int(val, &c->theme);
        else if (str_caseeq(key, "cal_swap"))     set_int(val, &c->cal_swap);
        else if (str_caseeq(key, "cal_flip_x"))   set_int(val, &c->cal_flip_x);
        else if (str_caseeq(key, "cal_flip_y"))   set_int(val, &c->cal_flip_y);
        else if (str_caseeq(key, "refresh_mode")) set_int(val, &c->refresh_mode);
        else if (str_caseeq(key, "rotation"))     set_int(val, &c->rotation);
        else if (str_caseeq(key, "img_mode"))     set_int(val, &c->img_mode);
        else if (str_caseeq(key, "forums")) {
            c->nforums = 0;
            char *tok = strtok(val, ", \t");
            while (tok && c->nforums < (int)(sizeof(c->forums) / sizeof(c->forums[0]))) {
                int fid = parse_int(tok, 0);
                if (fid > 0) c->forums[c->nforums++] = fid;
                tok = strtok(NULL, ", \t");
            }
        }
    }
    fclose(f);
    return 0;
}

void config_dump(const Config *c) {
    log_msg("config: server=%s user=%s scale=%d flash_every=%d forums=%d offline=%d theme=%d",
            c->forum_server, c->username[0] ? c->username : "(guest)",
            c->font_scale, c->full_refresh_every, c->nforums, c->offline, c->theme);
}

/* Persist the handful of settings the device can change (login, theme,
 * font size, refresh cadence). Written to config_path, which lives on
 * /mnt/us and is therefore writable. */
int config_save(const Config *c) {
    if (!c->config_path[0]) return -1;
    char tmp[HIPDA_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s.new", c->config_path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        log_msg("config: cannot write %s", tmp);
        return -1;
    }
    fprintf(f, "# written by hipda-kindle on the device\n");
    fprintf(f, "forum_server=%s\n", c->forum_server);
    fprintf(f, "image_host=%s\n", c->image_host);
    fprintf(f, "username=%s\n", c->username);
    fprintf(f, "password_md5=%s\n", c->password_md5);
    fprintf(f, "cookie_file=%s\n", c->cookie_file);
    fprintf(f, "font_file=%s\n", c->font_file);
    fprintf(f, "cache_dir=%s\n", c->cache_dir);
    fprintf(f, "net_cmd=%s\n", c->net_cmd);
    fprintf(f, "user_agent=%s\n", c->user_agent);
    fprintf(f, "font_scale=%d\n", c->font_scale);
    fprintf(f, "full_refresh_every=%d\n", c->full_refresh_every);
    fprintf(f, "timeout=%d\n", c->timeout);
    fprintf(f, "theme=%d\n", c->theme);
    fprintf(f, "cal_swap=%d\n", c->cal_swap);
    fprintf(f, "cal_flip_x=%d\n", c->cal_flip_x);
    fprintf(f, "cal_flip_y=%d\n", c->cal_flip_y);
    fprintf(f, "refresh_mode=%d\n", c->refresh_mode);
    fprintf(f, "rotation=%d\n", c->rotation);
    fprintf(f, "img_mode=%d\n", c->img_mode);
    fprintf(f, "offline=%d\n", c->offline);
    fprintf(f, "no_login=%d\n", c->no_login);
    if (c->nforums > 0) {
        fprintf(f, "forums=");
        for (int i = 0; i < c->nforums; i++)
            fprintf(f, "%s%d", i ? "," : "", c->forums[i]);
        fputc('\n', f);
    }
    if (fclose(f)) return -1;
    /* rename over the original; fall back to a direct copy if rename fails */
    if (rename(tmp, c->config_path)) {
        FILE *src = fopen(tmp, "r");
        FILE *dst = fopen(c->config_path, "w");
        if (!src || !dst) {
            if (src) fclose(src);
            if (dst) fclose(dst);
            return -1;
        }
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
        fclose(src);
        fclose(dst);
    }
    log_msg("config: saved to %s", c->config_path);
    return 0;
}
