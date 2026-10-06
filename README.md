# KOVIL — Temple of the Lost Lamps

A retro raycasting dungeon crawler set in ancient South Indian temple
ruins, written from scratch in C99. The renderer, physics, pathfinding,
generation, audio synthesis, font, and every data structure are
hand-written; the only external dependency is SDL2, used purely for the
window, input, and audio device.

You are the temple keeper, descending three procedurally generated
floors to relight the shrine lamps before the complex is lost — past
awakened dwarapalaka statues, yali beasts, naga serpents, an asura
war-band of bowmen and champions, and finally the Asura King himself.

## Building

Requires a C99 compiler and SDL2.

```
# macOS
brew install sdl2
make
./kovil

# Linux (Debian/Ubuntu)
sudo apt install libsdl2-dev
make
./kovil
```

`make asan` builds `kovil_asan` with Address/UB sanitizers for
leak-hunting (macOS has no Valgrind; on Linux, `valgrind ./kovil`
works too).

## Controls

| Input        | Action                                    |
|--------------|-------------------------------------------|
| WASD / mouse | move / look                               |
| Space        | jump                                      |
| Ctrl / Shift | sprint / sneak                            |
| Shift + shield | block (shield in the offhand slot or in hand) |
| LMB          | use held item (swing, shoot, drink)       |
| RMB          | throw consecrated flame                   |
| 1–5 / wheel  | select hotbar slot                        |
| I            | open/close the inventory (the OFF slot is your left hand) |
| E            | interact: lamps, pillars, levers, chests, the shrine |
| V            | first/third-person view                   |
| M            | minimap                                   |
| C            | controls panel                            |
| H            | re-show floor objectives                  |
| F5 / F9      | save / load the descent                   |
| Esc          | pause · then R restart run · Q quit       |
| Enter        | replay (on the victory screen)            |

Inside the inventory, every gesture works the way Minecraft trained
your hands:

| Input             | Action                                          |
|-------------------|-------------------------------------------------|
| Left click        | pick the whole stack up, put it down, or swap   |
| Press and drag    | drop on another slot to move it there, or to swap the two items; drop outside the slots to put it back |
| **Shift + click** | **send a stack straight to the hotbar** (or back to storage) |
| Right click       | take half the stack; or, holding one, lay down a single item |
| 1–5 over a slot   | swap that slot with that hotbar slot            |

Shift-click is the fast one: the storage grid is deliberately not
usable in a fight, so getting a potion into your hand means sending
it to the hotbar first.

## How to win a floor

1. Light the three pip-marked lamps **in order** (1, 2, 3 gold pips).
   A wrong lamp snuffs them all.
2. Turn the arrow pillars with E until **every arrow agrees**.
3. The sanctum doors grind open — light the shrine inside.
4. On floor 3 the Asura King guards the shrine. He must fall first.

Chests hold healing, strength, and swiftness potions. Fallen asura
warriors drop their bows, swords and shields. The temple adapts: kill
a kind of guardian quickly and often, and the next of its kin come
back tougher and faster — and more guardians condense out of the dark
over time, up to a per-floor cap, always waking somewhere you can't
see.

Every guardian has a signature move. The dwarapalaka freezes back
into unhurtable stone to knit its wounds; the yali pounces from mid
range; the naga crawls low and its bite leaves ten seconds of venom
(one tenth of a heart per second — your hearts turn green); the
bowman looses a three-arrow volley every fourth draw; the champion's
shield turns one blow in three.

## Configuration

`kovil.ini` is created next to the binary on first run:
`mouse_sensitivity`, `volume`, `show_minimap`, `window_scale`.

`kovil.sav` (binary, magic + version header) stores floor, seed,
health, inventory and the temple's adaptation memory. Loading rebuilds
the floor from its seed; mid-floor puzzle progress resets by design.

## Credits

- Palette: **"Resurrect 64"** by Kerrie Lake
  (https://lospec.com/palette-list/resurrect-64).
- Everything else — textures, sprites, sounds, font — is generated
  procedurally in code. The game runs with no asset files at all.
- Enemy designs draw on guardian and demon iconography (dwarapalaka,
  yali, naga, asura) and deliberately never depict worshipped deities.
