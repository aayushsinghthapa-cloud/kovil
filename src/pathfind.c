#include "pathfind.h"

#include <stdlib.h>
#include <string.h>

/* ---- binary min-heap -------------------------------------------------
 * The A* open set: pop-lowest-f in O(log n), push in O(log n). Stored
 * as an implicit tree in a flat array — node i's children live at
 * 2i+1 and 2i+2, so there are no pointers to manage at all. */

typedef struct {
    int idx; /* tile index y*w+x */
    int f;   /* priority: g + heuristic */
} HeapNode;

static HeapNode *heap = NULL;
static int heap_len;

static void heap_push(int idx, int f)
{
    /* place at the end, then bubble up while smaller than the parent */
    int i = heap_len++;
    heap[i].idx = idx;
    heap[i].f = f;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap[parent].f <= heap[i].f)
            break;
        HeapNode t = heap[parent];
        heap[parent] = heap[i];
        heap[i] = t;
        i = parent;
    }
}

static HeapNode heap_pop(void)
{
    /* take the root, move the last element there, sift it down */
    HeapNode top = heap[0];
    heap[0] = heap[--heap_len];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = 2 * i + 2, small = i;
        if (l < heap_len && heap[l].f < heap[small].f)
            small = l;
        if (r < heap_len && heap[r].f < heap[small].f)
            small = r;
        if (small == i)
            break;
        HeapNode t = heap[small];
        heap[small] = heap[i];
        heap[i] = t;
        i = small;
    }
    return top;
}

/* ---- A* --------------------------------------------------------------- */

static int *g_cost = NULL;  /* best known distance from start */
static int *parent = NULL;  /* tile we arrived from, for path recovery */
static int map_w, map_h;

int pathfind_init(int w, int h)
{
    pathfind_shutdown();
    map_w = w;
    map_h = h;
    size_t n = (size_t)w * (size_t)h;
    g_cost = malloc(n * sizeof *g_cost);
    parent = malloc(n * sizeof *parent);
    /* worst case every tile enters the heap once per relaxation;
     * 4-directional relaxations bound it safely at 4n */
    heap = malloc(4 * n * sizeof *heap);
    if (!g_cost || !parent || !heap) {
        pathfind_shutdown();
        return -1;
    }
    return 0;
}

void pathfind_shutdown(void)
{
    free(g_cost);
    free(parent);
    free(heap);
    g_cost = NULL;
    parent = NULL;
    heap = NULL;
}

/* Manhattan distance: admissible (never overestimates) for
 * 4-directional movement, which is what makes A* return true
 * shortest paths. */
static int heuristic(int x, int y, int tx, int ty)
{
    int dx = x > tx ? x - tx : tx - x;
    int dy = y > ty ? y - ty : ty - y;
    return dx + dy;
}

int pathfind_next(const Map *m, int sx, int sy, int tx, int ty,
                  int *nx, int *ny)
{
    if (!g_cost || !map_walkable(m, sx, sy) || !map_walkable(m, tx, ty))
        return 0;
    if (sx == tx && sy == ty)
        return 0;

    const int w = map_w, n = map_w * map_h;
    memset(parent, -1, (size_t)n * sizeof *parent);
    /* "infinity" that still survives + heuristic without overflow */
    for (int i = 0; i < n; i++)
        g_cost[i] = 1 << 29;

    heap_len = 0;
    int start = sy * w + sx, goal = ty * w + tx;
    g_cost[start] = 0;
    heap_push(start, heuristic(sx, sy, tx, ty));

    static const int dx4[4] = { 1, -1, 0, 0 };
    static const int dy4[4] = { 0, 0, 1, -1 };

    while (heap_len > 0) {
        HeapNode cur = heap_pop();
        if (cur.idx == goal)
            break;
        int cx = cur.idx % w, cy = cur.idx / w;
        /* stale heap entry (a better path already relaxed this tile):
         * cheaper to skip here than to re-key inside the heap */
        if (cur.f > g_cost[cur.idx] + heuristic(cx, cy, tx, ty))
            continue;

        for (int d = 0; d < 4; d++) {
            int mx = cx + dx4[d], my = cy + dy4[d];
            if (!map_walkable(m, mx, my))
                continue;
            int ni = my * w + mx;
            int ng = g_cost[cur.idx] + 1;
            if (ng < g_cost[ni]) {
                g_cost[ni] = ng;
                parent[ni] = cur.idx;
                heap_push(ni, ng + heuristic(mx, my, tx, ty));
            }
        }
    }

    if (parent[goal] < 0)
        return 0; /* unreachable */

    /* walk the parent chain backwards from the goal until the tile
     * whose parent is the start: that tile is our first step */
    int step = goal;
    while (parent[step] != start)
        step = parent[step];
    *nx = step % w;
    *ny = step / w;
    return 1;
}
