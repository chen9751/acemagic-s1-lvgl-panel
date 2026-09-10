# S1 navigation and light pages

Based on main e038b4f and the hardware baseline: 170 × 320 LCD, logical W1
input events, unchanged LCD/LED drivers. No air mouse or unverified LED RGB,
segment or pixel control is introduced.

## Modules

- `ui/ui_router.c/h`: the eight-page horizontal ring and HOME-only branches.
- `ui/pages/ui_page.c/h`: shared panel geometry, page names and device placeholders.
- `ui/pages/ui_light.c/h`: one reusable light view with independent per-entity
  OFF / brightness / temperature state and remembered values.
- `ui/s1_ui.c`: UI composition, input dispatch and the existing HOME, LED and
  MUSIC implementation. These existing pages are deliberately kept together
  for this first refactor.
- `services/ha_client.c/h`: existing service transport, with a small explicit
  per-entity power wrapper. No polling or external state synchronization added.

## Navigation

RIGHT follows this ring; LEFT follows it in reverse, including wraparound:

空调 → 浴霸 → 窗帘 → HOME → 客厅灯 → 书房灯 → 卧室灯 → 小卧室灯 → 空调

Only HOME UP enters LED and HOME DOWN enters MUSIC. UP/DOWN on placeholders
has no action. UP/DOWN on lights is local to the light's control mode.
HOME always returns home. BACK returns home except in MUSIC control mode,
where the first BACK returns to the MUSIC page and the next returns home.
LED and MUSIC are branches outside the ring: LEFT/RIGHT do not leave them.
MUSIC control mode keeps LEFT/RIGHT for previous/next and OK for play/pause;
UP leaves control mode. LED keeps UP/DOWN selection and OK application.

## Lights

The page header identifies the room; OFF content only shows OFF, without a
bar or value. OK sends `light.turn_on` with remembered brightness and enters
brightness mode on success. MENU alternates brightness and temperature while
on. UP/DOWN adjust by 10 points, clamped to brightness 10–100% and temperature
0–100 (2700–6500 K, preserving the existing conversion). OK from either ON
mode sends explicit per-entity `light.turn_off` and returns to OFF on success.
LEFT/RIGHT always navigate between horizontal pages, in every light mode.
VOL+/VOL- do nothing on lights. OFF UP/DOWN/MENU do nothing.

Service failures keep the previous value/mode and display `Command failed`.
Page visits send no requests. Values and mode are retained across page visits;
startup begins locally OFF with brightness/temperature at 50. This is local
command state, not a live representation of HA. External state, persistence,
and device-specific temperature ranges are deferred. Existing synchronous
HA request timeouts remain unchanged.

The four entity IDs are unchanged from main. The old HA list, including its
extra lights and all-off UI, is replaced by the requested eight-page layout;
the existing HA service functions remain available.

## W1 mapping

Navigation, ENTER, BACK, HOME and volume retain existing logical mappings.
`KEY_MENU` maps to `S1_KEY_MENU`. The hardware baseline explicitly says the
physical meaning of `KEY_COMPOSE` is not confirmed, so it is left unmapped.
Confirm which physical remote button produces `KEY_MENU` on the device before
assigning an alternative scan code. Only initial key presses are processed,
as in the original driver.

## Verification

Normal Linux build (SDL2 and libcurl development packages required):

```sh
git submodule update --init --depth 1 lvgl
cmake -S . -B build/app -DCMAKE_BUILD_TYPE=Release
cmake --build build/app -j2
```

Hardware-free checks use the pinned real LVGL and all UI sources/fonts, with
HA/LED service doubles. SDL is disabled only in a generated test configuration;
production `lv_conf.h` remains unchanged.

```sh
cmake -S . -B build/tests -DS1_UI_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/tests -j2
ctest --test-dir build/tests --output-on-failure
```

Tests cover both ring directions, HOME-only branch access, all four entity
mappings, each light mode, bounded adjustment, failed commands, per-page
retention, ignored volume, BACK/HOME, MUSIC actions and LED application.
They also render 170 × 320 PPM frames in the test build directory.
GitHub Actions builds the full Linux application and runs these checks on main.
Actual LCD, remote and HA service operation still require the S1 hardware.
Never stage `config/ha.conf`; it remains ignored and untracked.
