#ifndef KOVIL_CONFIG_H
#define KOVIL_CONFIG_H

/* Settings read from kovil.ini, a hand-parsed key=value file.
 * Missing file or missing keys fall back to these defaults, and a
 * missing file is written out with the defaults so players can find
 * and edit it. */
typedef struct {
    double sensitivity; /* mouse look multiplier, default 1.0 */
    double volume;      /* master volume 0..1, default 0.8 */
    int show_minimap;   /* minimap on at startup, default 1 */
    int window_scale;   /* window = 320x200 * scale, default 4 */
} Config;

void config_defaults(Config *c);
void config_load(Config *c, const char *path);

#endif
