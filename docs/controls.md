# Controls

Keyboard, mouse and gamepad all work at once. Player one is the keyboard and mouse;
the first gamepad that connects is player one's pad as well. Nothing needs setting up
to start playing.

## Keyboard defaults

Movement is WASD on the Nunchuk stick, which is what the game reads for walking. The
arrow keys are the Wii Remote's D-pad. A and B are Enter/J and Backspace/K so the
common "confirm" and "cancel" keys work with one hand.

| Action | Keys |
| --- | --- |
| Walk (Nunchuk stick) | W A S D |
| D-pad | Arrow keys |
| A | Enter, J |
| B | Backspace, K |
| 1 | 1 |
| 2 | 2 |
| Plus (menu) | =, P |
| Minus (map) | -, O |
| Home | H |
| L (Classic Controller L) | Q |
| R (Classic Controller R) | E |
| ZL (Classic Controller ZL) | Z |
| ZR (Classic Controller ZR) | X |
| C (Nunchuk C) | C |
| Z (Nunchuk Z) | V |
| Shake | F |
| Pointer | Mouse |

The Classic keys only matter with a Classic Controller attachment, and C and Z only with
a Nunchuk; each attachment reads its own buttons. Rune Factory: Tides of Destiny needs
the Nunchuk (`attachment = nunchuk`, the default) for walking and menus.

The mouse moves the Wii pointer; the game's cursor follows it over the window. The mouse
buttons are set in `[mouse]` (below): by default the left button is the remote's A, the
right button is B, and the middle and side buttons do nothing. The gyro pointer is
re-centred with a gamepad button (`recenter`, right stick click by default), not with the
mouse.

## Editing controls in the launcher

**Controls** on the launcher opens the mapping editor for the same `controllers.ini`,
so the file does not have to be edited by hand.

- The **Keyboard** tab lists every action and its keys. Click a row, then press the key
  you want; the action takes that key alone. The small **x** on the row sets the action
  to `none`. To give one action several keys, add them with commas in `controllers.ini`
  instead.
- The **Gamepad** tab lists the Wii buttons and the controller button that presses each.
  Click a row and press a pad button, or right-click the row and pick from the list when
  no pad is connected. **Auto** restores the layout's own mapping and **None** leaves the
  Wii button unused.
- The **Mouse** tab lists the left, right, middle and two side buttons. Click a row and
  pick the Wii button (or **Shake the remote**, or none) that it presses. The small **x**
  sets the button to none.
- The **Player** tab edits player 1's `[player1]` section: whether the keyboard, mouse and
  gamepad drive it, the attachment (none, Nunchuk or Classic Controller), MotionPlus,
  held sideways, what moves the pointer (mouse, gamepad gyro, right stick), the gyro pointer
  speed (0.25 to 4) and the controller button that recentres the pointer. The game needs
  the **Nunchuk** attachment for walking and menus.
- The **Stick deadzone** and **Stick speed** sliders below the gamepad list write
  `deadzone` and `stick_speed` directly, with the same limits the runtime enforces.
- **Restore defaults** removes every override from the file, and **Open guide** opens
  this page.
- Reusing a key or a controller button is allowed; the footer says which action already
  uses it, so an accidental double binding is easy to spot.
- Escape cancels a capture, and closes the window when nothing is being captured.
- Changes are written immediately and apply the next time the game starts.

## Mouse and player 1

`[mouse]` binds each mouse button to one Wii button. The values are `a`, `b`, `one`, `two`,
`plus`, `minus`, `home`, `up`, `down`, `left`, `right`, `c`, `z`, `l`, `r`, `zl`, `zr` (the same
names as `[keys]`), `shake`, or `none`. A mistake stops the console and names the line, as
elsewhere. While a control script drives the game (`WII_RECOMP_CONTROL`) the mouse stays
off, as before.

```ini
[mouse]
left = a
right = b
middle = none
x1 = none
x2 = none
```

`[player1]` (also `[player2]` to `[player4]`) holds the keys the Player tab writes:
`source` (`keyboard`, `mouse`, `pad`, `pad:<serial>`, `none`; several with commas),
`attachment` (`none`, `nunchuk`, `classic`), `motionplus` and `sideways` (`yes` or `no`),
`pointer` (`mouse`, `gyro`, `stick`, `none`), `gyro_speed` (above 0, the launcher offers
0.25 to 4) and `recenter` (a controller button). Only one player can have the keyboard and
one the mouse.

## Changing the keyboard

The launcher editor writes this section for you. To edit it by hand, defaults are
written to `controllers.ini` beside the console
(`work/wii-recomp/recomp/build/controllers.ini`) the first time the console runs, with
every line commented out. Uncomment a line to override that action. Each action takes
up to four keys, separated by commas, for example:

```ini
[keys]
stick_up = w
stick_down = s
stick_left = a
stick_right = d
a = enter, j, space
b = backspace, k, escape
```

Key names are letters, digits, `f1` to `f12`, `up`, `down`, `left`, `right`, `enter`,
`backspace`, `space`, `tab`, `escape`, `minus`, `equal`, `left_shift`, and so on, or
`none`.

## Gamepad

Gamepads are read through SDL3. The launcher's Controllers card lists what is
connected; Rescan looks again, and **Test input** opens a window that shows every
button and both sticks as the game reads them. In game, the first pad is player one.
By default the Wii Remote and Nunchuk are laid out the way the game expects:

| Gamepad | Wii Remote + Nunchuk |
| --- | --- |
| A | A |
| B or ZR | B |
| Y | 1 |
| X | 2 |
| D-pad | D-pad |
| Plus / Minus / Home | Plus / Minus / Home |
| L | Nunchuk C |
| ZL | Nunchuk Z |
| Left stick | Nunchuk stick / pointer (with gyro) |

A Classic Controller attachment can be selected in `controllers.ini`
(`attachment = classic`), which maps the face buttons, shoulders and D-pad straight
through.

### Changing the gamepad

The launcher's **Controls** window edits this section; the lines below are the manual
form. Each line names a Wii button and the controller button that presses it:

```ini
[pad]
b = x
two = y
```

The Wii buttons are `a`, `b`, `one`, `two`, `plus`, `minus`, `home`, `up`, `down`,
`left`, `right`, `c` and `z` (the Nunchuk), and `classic_l`, `classic_r`, `classic_zl`
and `classic_zr`. The controller buttons are `a`, `b`, `x`, `y`, `l`, `r`, `zl`, `zr`,
`plus`, `minus`, `home`, `capture`, `left_stick`, `right_stick`, `dpad_up`, `dpad_down`,
`dpad_left` and `dpad_right`. A line replaces every source of that Wii button, in every
layout, so `b = x` makes X press B and stops the B button doing it; `auto` gives the
button back to the layout and `none` leaves it unused. The setting applies to every
controller.

Sticks get a circle in the middle where a worn stick can rest, and a speed for the rest
of their travel:

```ini
[pad]
deadzone = 0.18
stick_speed = 1.0
```

`deadzone` runs from 0 to 0.9: the fraction of the stick that is ignored. `stick_speed`
runs from 0.25 to 4 and scales what is left, so 2 reaches full tilt at half travel. Both
apply to the Nunchuk stick, the Classic Controller's sticks and the right stick when it
drives the pointer. The input check draws the deadzone as an inner circle and the
shaped position as an orange dot, while the grey dot is the raw position.

The first time the console runs it writes the file with every line commented out.
Uncomment a line to use it. If a name is wrong the console stops and says which line and
what it accepts.

## Frame rate overlay

The launcher's **Frame rate overlay** switch draws the current FPS in the window's top
right corner. It is saved in `config/launcher.ini` and passed to the runtime as
`WII_RECOMP_FPS_OVERLAY=1`. The same switch turns it off.
