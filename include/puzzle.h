#ifndef KOVIL_PUZZLE_H
#define KOVIL_PUZZLE_H

#include "entity.h"
#include "map.h"
#include "player.h"

/* Runtime state of one floor's puzzles. The rules:
 *  - light the three pip-marked lamps in pip order (wrong order
 *    snuffs them all);
 *  - rotate the arrow pillars until every arrow agrees;
 *  - when both are done the sanctum doors grind open;
 *  - the sluice lever raises a stone causeway across the tank
 *    (a shortcut, not a gate);
 *  - lighting the shrine inside the sanctum completes the floor. */
typedef struct {
    int lamps_total;
    int lamps_next;  /* next expected pip, 1-based */
    int lamps_done;
    int rotors_done; /* also 1 when the floor generated no rotors */
    int doors_open;
    int shrine_lit;
} Puzzle;

void puzzle_init(Puzzle *pz, const Map *m);

/* The E key: interact with whatever puzzle piece is in front of the
 * player (lamp, lever, shrine, or rotor pillar). Returns 1 if
 * anything reacted. */
int puzzle_interact(Puzzle *pz, Map *m, EntityList *el,
                    const Player *p);

/* What WOULD the E key do right now? Returns a short instruction
 * ("E: LIGHT LAMP 2 OF 3") for the HUD, or NULL if nothing is in
 * front of the player. Read-only. */
const char *puzzle_prompt(const Puzzle *pz, const Map *m,
                          const EntityList *el, const Player *p);

#endif
