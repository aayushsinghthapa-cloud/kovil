#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(Config *c)
{
    c->sensitivity = 1.0;
    c->volume = 0.8;
    c->show_minimap = 1;
    c->window_scale = 4;
}

/* strip leading/trailing whitespace in place, return the start */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static void write_defaults(const Config *c, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return; /* read-only directory: just play with defaults */
    fprintf(f,
            "# Kovil settings. '#' starts a comment.\n"
            "mouse_sensitivity=%.2f\n"
            "volume=%.2f\n"
            "show_minimap=%d\n"
            "window_scale=%d\n",
            c->sensitivity, c->volume, c->show_minimap,
            c->window_scale);
    fclose(f);
}

void config_load(Config *c, const char *path)
{
    config_defaults(c);

    FILE *f = fopen(path, "r");
    if (!f) {
        /* first run: leave a template the player can edit */
        write_defaults(c, path);
        return;
    }

    char line[128];
    while (fgets(line, sizeof line, f)) {
        /* a comment can also follow a value: cut at the '#' */
        char *hash = strchr(line, '#');
        if (hash)
            *hash = '\0';

        char *eq = strchr(line, '=');
        if (!eq)
            continue; /* blank or malformed line: ignore, don't die */
        *eq = '\0';

        const char *key = trim(line);
        const char *val = trim(eq + 1);

        if (strcmp(key, "mouse_sensitivity") == 0) {
            double v = atof(val);
            if (v >= 0.1 && v <= 5.0)
                c->sensitivity = v;
        } else if (strcmp(key, "volume") == 0) {
            double v = atof(val);
            if (v >= 0.0 && v <= 1.0)
                c->volume = v;
        } else if (strcmp(key, "show_minimap") == 0) {
            c->show_minimap = atoi(val) != 0;
        } else if (strcmp(key, "window_scale") == 0) {
            int v = atoi(val);
            if (v >= 1 && v <= 8)
                c->window_scale = v;
        }
        /* unknown keys are ignored: old configs keep working */
    }
    fclose(f);
}
