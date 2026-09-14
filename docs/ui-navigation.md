# S1 navigation and verification

The primary pages form a vertical loop: HOME → HOME ASSISTANT → LED → MUSIC.
DOWN advances, UP reverses. HOME and BACK return to HOME.

On HOME, LEFT/RIGHT select a light, OK toggles it, and MENU opens its detail.
On HOME ASSISTANT, LEFT/RIGHT select a device, OK toggles a light, and MENU
opens its detail page. HA names and status synchronize before drawing.

On LED, MENU cycles Mode → Brightness → Speed → Mode. LEFT/RIGHT browse modes
or adjust the selected setting from 1 to 5. OK applies a browsed mode.
Brightness and speed affect the active mode, not an unconfirmed preview.
Failed writes retain the previous values. Re-entering LED selects Mode.
Mode labels are RAINBOW, BREATHING, COLOR, AUTO and OFF.

On MUSIC, LEFT/RIGHT select previous/next and OK controls play/pause.
MENU toggles the idle-timeout lock. Music v2 owns the playback status;
there is no second gray PAUSED overlay. UP/DOWN still navigate primary pages.

Light detail pages refresh HA on entry, every three seconds while visible,
and before local key actions. OK toggles power; MENU switches brightness and
color temperature while on; UP/DOWN adjust the value. LEFT/RIGHT and volume
have no action. Failed commands preserve the previous local value.
Unknown/unavailable HA states are treated as synchronization failures.

W1 KEY_MENU and KEY_COMPOSE both map to MENU. Only initial presses are handled.

## Verification

```sh
git submodule update --init --depth 1 lvgl
cmake -S . -B build/app -DCMAKE_BUILD_TYPE=Release
cmake --build build/app -j2
cmake -S . -B build/tests -DS1_UI_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/tests -j2
ctest --test-dir build/tests --output-on-failure
```

The Linux application needs SDL2 and libcurl development packages. UI checks
use real LVGL with HA/LED service doubles, and render 170 × 320 PPM frames.
They cover vertical navigation, HA labels, LED focus/limits/write failure,
Music state and dispatch, and Home clock centering. Physical LCD, serial LED,
Bluetooth and HA integration must also be checked on the S1 device.
