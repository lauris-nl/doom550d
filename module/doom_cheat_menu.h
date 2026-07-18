#ifndef DOOM_CHEAT_MENU_H
#define DOOM_CHEAT_MENU_H

typedef enum
{
    DOOM_CHEAT_MENU_UP,
    DOOM_CHEAT_MENU_DOWN,
    DOOM_CHEAT_MENU_SELECT,
    DOOM_CHEAT_MENU_BACK
} doom_cheat_menu_command_t;

void doom_cheat_menu_request_open(void);
void doom_cheat_menu_queue(doom_cheat_menu_command_t command);
int doom_cheat_menu_captures_input(void);
void doom_cheat_menu_ticker(void);
void doom_cheat_menu_draw(void);
void doom_cheat_menu_reset(void);

#endif
