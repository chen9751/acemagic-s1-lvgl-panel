#ifndef S1_INPUT_DISPATCH_H
#define S1_INPUT_DISPATCH_H

#include "../ui/s1_ui.h"
#include "../ui/pages/ui_music_v2.h"

/*
 * W1 input has one control owner: s1_ui_key().  The music visual layer only
 * receives a non-owning notification so it can flash the corresponding button
 * without intercepting or changing navigation/control behavior.
 */
static inline void s1_input_dispatch_key(uint32_t key)
{
    s1_ui_music_v2_key_feedback(key);
    s1_ui_key(key);
}

#endif
