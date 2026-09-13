#ifndef UI_MUSIC_V2_H
#define UI_MUSIC_V2_H

#include <stdbool.h>
#include <stdint.h>

void s1_ui_music_v2_init(void);
void s1_ui_music_v2_set_state(bool connected, bool playing);
void s1_ui_music_v2_set_progress(uint32_t elapsed_seconds, uint32_t duration_seconds);
void s1_ui_music_v2_key_feedback(uint32_t key);
void s1_ui_music_v2_sync_visibility(void);

#endif
