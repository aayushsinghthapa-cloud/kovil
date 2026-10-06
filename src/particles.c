#include "particles.h"

#include <math.h>
#include <stddef.h>

#include "raycaster.h"
#include "util.h"

#define MAX_PARTICLES 512

typedef struct Particle Particle;
struct Particle {
    double x, y, z;    /* z: height in wall units, 0 floor .. 1 ceiling */
    double vx, vy, vz;
    double life, max_life;
    uint32_t color;
    int gravity;       /* chips fall; beam motes rise on their own */
    Particle *next;    /* free list OR live list link */
};

static Particle pool[MAX_PARTICLES];
static Particle *free_head = NULL;
static Particle *live_head = NULL;
static Rng rng = { 0x9A97717Eu };

void particles_reset(void)
{
    /* thread the whole pool onto the free list */
    free_head = &pool[0];
    for (int i = 0; i < MAX_PARTICLES - 1; i++)
        pool[i].next = &pool[i + 1];
    pool[MAX_PARTICLES - 1].next = NULL;
    live_head = NULL;
}

static Particle *take(void)
{
    if (!free_head)
        return NULL; /* pool exhausted: oldest effects simply win */
    Particle *p = free_head;
    free_head = p->next;
    p->next = live_head;
    live_head = p;
    return p;
}

static double frand(double lo, double hi)
{
    return lo + (hi - lo) * (rng_next(&rng) >> 8) / 16777216.0;
}

void particles_burst(double x, double y, double z, int count,
                     uint32_t color)
{
    for (int i = 0; i < count; i++) {
        Particle *p = take();
        if (!p)
            return;
        p->x = x;
        p->y = y;
        p->z = z + frand(-0.05, 0.05);
        p->vx = frand(-1.6, 1.6);
        p->vy = frand(-1.6, 1.6);
        p->vz = frand(0.5, 2.4);
        p->max_life = p->life = frand(0.3, 0.7);
        p->color = color;
        p->gravity = 1;
    }
}

void particles_beam(double x, double y, uint32_t color)
{
    for (int i = 0; i < 22; i++) {
        Particle *p = take();
        if (!p)
            return;
        p->x = x + frand(-0.3, 0.3);
        p->y = y + frand(-0.3, 0.3);
        p->z = frand(0.0, 0.3);
        p->vx = 0.0;
        p->vy = 0.0;
        p->vz = frand(0.6, 1.4); /* rises */
        p->max_life = p->life = frand(0.8, 1.6);
        p->color = color;
        p->gravity = 0;
    }
}

void particles_update(double dt)
{
    Particle **link = &live_head;
    while (*link) {
        Particle *p = *link;
        p->life -= dt;
        if (p->life <= 0.0) {
            /* unlink from live list, push back on the free list —
             * the pointer-to-pointer walk removes without a prev
             * variable or special head case */
            *link = p->next;
            p->next = free_head;
            free_head = p;
            continue;
        }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->z += p->vz * dt;
        if (p->gravity) {
            p->vz -= 6.0 * dt;
            if (p->z < 0.02) { /* settle on the floor */
                p->z = 0.02;
                p->vz = 0.0;
                p->vx *= 0.8;
                p->vy *= 0.8;
            }
        }
        link = &p->next;
    }
}

void particles_render(Framebuffer *fb, const Player *p)
{
    const double *zbuf = raycast_depth();
    const int w = fb->w, h = fb->h;
    int horizon = h / 2 + (int)p->pitch;
    double cz = 0.5 + p->z;
    double invDet = 1.0 / (p->planeX * p->dirY - p->dirX * p->planeY);

    for (Particle *pt = live_head; pt; pt = pt->next) {
        double sx = pt->x - p->x, sy = pt->y - p->y;
        double tx = invDet * (p->dirY * sx - p->dirX * sy);
        double ty = invDet * (-p->planeY * sx + p->planeX * sy);
        if (ty <= 0.1)
            continue;

        int screenX = (int)(w / 2.0 * (1.0 + tx / ty));
        if (screenX < 0 || screenX >= w || ty >= zbuf[screenX])
            continue;
        int screenY = horizon + (int)((cz - pt->z) * h / ty);

        /* size shrinks with distance and as life runs out */
        int size = (int)(3.0 / ty * (0.5 + pt->life / pt->max_life));
        if (size < 1)
            size = 1;
        fb_fill_rect(fb, screenX - size / 2, screenY - size / 2,
                     size, size, pt->color);
    }
}
