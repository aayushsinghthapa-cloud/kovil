#ifndef KOVIL_PATHFIND_H
#define KOVIL_PATHFIND_H

#include "map.h"

/* A* over the tile grid. Call pathfind_init once after generating a
 * map (it sizes the scratch buffers); pathfind_next then finds the
 * first step of a shortest path from (sx,sy) toward (tx,ty) and
 * writes it to (*nx,*ny). Returns 1 on success, 0 if unreachable. */
int pathfind_init(int w, int h);
void pathfind_shutdown(void);

int pathfind_next(const Map *m, int sx, int sy, int tx, int ty,
                  int *nx, int *ny);

#endif
