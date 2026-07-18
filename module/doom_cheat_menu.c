#include "doom_ml_compat.h"

#include <stdint.h>
#include <string.h>

#include "am_map.h"
#include "d_event.h"
#include "deh_str.h"
#include "doom_cheat.h"
#include "doom_cheat_menu.h"
#include "doomgeneric.h"
#include "doomstat.h"
#include "g_game.h"
#include "i_video.h"
#include "m_menu.h"
#include "m_misc.h"
#include "p_mobj.h"
#include "s_sound.h"
#include "sounds.h"
#include "st_stuff.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#define CHEAT_MENU_COMMANDS 16
#define CHEAT_MENU_MAX_CODES 40
#define CHEAT_MENU_CODE_LEN 9
#define CHEAT_MENU_DESCRIBED_ROWS 5
#define CHEAT_MENU_COMPACT_ROWS 8
#define CHEAT_MENU_TEXT_X 30
#define CHEAT_MENU_CURSOR_X 2
#define CHEAT_MENU_ROW_Y 36
#define CHEAT_MENU_DESCRIBED_HEIGHT 30
#define CHEAT_MENU_COMPACT_HEIGHT 19
#define CHEAT_MENU_CONTROL_WIDTH 40
#define CHEAT_MENU_CONTROL_GAP 5

typedef enum
{
    CHEAT_PAGE_MAIN,
    CHEAT_PAGE_CHEATS,
    CHEAT_PAGE_MUSIC,
    CHEAT_PAGE_LEVEL,
    CHEAT_PAGE_COUNT
} cheat_page_t;

typedef struct
{
    const char *code;
    int power;
    int player_flag;
} cheat_item_t;

static const char *main_items[] = { "CHEATS", "MUSIC", "LEVEL" };

static const cheat_item_t cheat_items[] =
{
    { "IDDQD",      -1, CF_GODMODE },
    { NULL,          -1, CF_NOCLIP },
    { "IDKFA",      -1, 0 },
    { "IDFA",       -1, 0 },
    { "IDCHOPPERS", -1, 0 },
    { "IDBEHOLD",   -1, 0 },
    { "IDBEHOLDV",  pw_invulnerability, 0 },
    { "IDBEHOLDS",  pw_strength, 0 },
    { "IDBEHOLDI",  pw_invisibility, 0 },
    { "IDBEHOLDR",  pw_ironfeet, 0 },
    { "IDBEHOLDA",  pw_allmap, 0 },
    { "IDBEHOLDL",  pw_infrared, 0 },
    { "IDMYPOS",    -1, 0 },
    { "IDDT",       -1, 0 },
};

static volatile int open_requested;
static volatile unsigned int command_read;
static volatile unsigned int command_write;
static doom_cheat_menu_command_t command_queue[CHEAT_MENU_COMMANDS];

static int menu_active;
static int menu_was_paused;
static int music_previewing;
static cheat_page_t current_page;
static int selected[CHEAT_PAGE_COUNT];
static int scroll_first[CHEAT_PAGE_COUNT];
static int cursor_animation;

static char music_codes[CHEAT_MENU_MAX_CODES][CHEAT_MENU_CODE_LEN];
static int music_ids[CHEAT_MENU_MAX_CODES];
static int music_count;
static int last_music_index = -1;
static int draw_music_marker = -1;

static char level_codes[CHEAT_MENU_MAX_CODES][CHEAT_MENU_CODE_LEN];
static int level_count;
static int draw_level_marker = -1;

static int episode_limit(void)
{
    if (gameversion == exe_chex)
        return 1;

    if (gamemode == shareware)
        return 1;
    if (gamemode == registered)
        return 3;
    if (gamemode == retail)
        return 4;

    return 0;
}

static void add_music_code(const char *code, int music_id)
{
    char lump[9];

    if (music_count >= CHEAT_MENU_MAX_CODES
     || music_id <= mus_None || music_id >= NUMMUSIC
     || S_music[music_id].name == NULL)
        return;

    M_snprintf(lump, sizeof(lump), "d_%s", DEH_String(S_music[music_id].name));
    if (W_CheckNumForName(lump) < 0)
        return;

    M_StringCopy(music_codes[music_count], code, CHEAT_MENU_CODE_LEN);
    music_ids[music_count] = music_id;
    music_count++;
}

static void build_music_list(void)
{
    char code[CHEAT_MENU_CODE_LEN];
    int episode;
    int map;
    int number;

    music_count = 0;
    last_music_index = -1;

    if (gamemode == commercial)
    {
        for (number = 1; number <= 35; number++)
        {
            M_snprintf(code, sizeof(code), "IDMUS%02d", number);
            add_music_code(code, ST_MusicCheatCodeToMusic(code + 5));
        }
        return;
    }

    for (episode = 1; episode <= episode_limit(); episode++)
    {
        for (map = 1; map <= 9; map++)
        {
            M_snprintf(code, sizeof(code), "IDMUS%d%d", episode, map);
            add_music_code(code, ST_MusicCheatCodeToMusic(code + 5));
        }
    }
}

static void add_level_code(const char *code, const char *lump)
{
    if (level_count >= CHEAT_MENU_MAX_CODES || W_CheckNumForName((char *)lump) < 0)
        return;

    M_StringCopy(level_codes[level_count], code, CHEAT_MENU_CODE_LEN);
    level_count++;
}

static void build_level_list(void)
{
    char code[CHEAT_MENU_CODE_LEN];
    char lump[9];
    int episode;
    int map;

    level_count = 0;

    if (gamemode == commercial)
    {
        for (map = 1; map <= 40; map++)
        {
            M_snprintf(code, sizeof(code), "IDCLEV%02d", map);
            M_snprintf(lump, sizeof(lump), "MAP%02d", map);
            add_level_code(code, lump);
        }
        return;
    }

    for (episode = 1; episode <= episode_limit(); episode++)
    {
        for (map = 1; map <= 9; map++)
        {
            M_snprintf(code, sizeof(code), "IDCLEV%d%d", episode, map);
            M_snprintf(lump, sizeof(lump), "E%dM%d", episode, map);
            add_level_code(code, lump);
        }
    }
}

static int page_item_count(cheat_page_t page)
{
    switch (page)
    {
        case CHEAT_PAGE_MAIN:
            return 3;
        case CHEAT_PAGE_CHEATS:
            return (int)(sizeof(cheat_items) / sizeof(cheat_items[0]));
        case CHEAT_PAGE_MUSIC:
            return music_count;
        case CHEAT_PAGE_LEVEL:
            return level_count;
        default:
            return 0;
    }
}

static int page_visible_rows(cheat_page_t page)
{
    if (page == CHEAT_PAGE_MUSIC || page == CHEAT_PAGE_LEVEL)
        return CHEAT_MENU_COMPACT_ROWS;

    return CHEAT_MENU_DESCRIBED_ROWS;
}

static int page_row_height(cheat_page_t page)
{
    if (page == CHEAT_PAGE_MUSIC || page == CHEAT_PAGE_LEVEL)
        return CHEAT_MENU_COMPACT_HEIGHT;

    return CHEAT_MENU_DESCRIBED_HEIGHT;
}

static void keep_selection_visible(void)
{
    int count = page_item_count(current_page);
    int visible_rows = page_visible_rows(current_page);
    int *first = &scroll_first[current_page];

    if (count <= 0)
    {
        selected[current_page] = 0;
        *first = 0;
        return;
    }

    if (selected[current_page] >= count)
        selected[current_page] = count - 1;
    if (selected[current_page] < 0)
        selected[current_page] = 0;

    if (selected[current_page] < *first)
        *first = selected[current_page];
    if (selected[current_page] >= *first + visible_rows)
        *first = selected[current_page] - visible_rows + 1;
    if (*first < 0)
        *first = 0;
}

static void close_menu(void)
{
    if (!menu_active)
        return;

    menu_active = 0;
    DG_ResetInput();
    G_ResetInputState();
    D_ClearEvents();

    if (!menu_was_paused && paused)
    {
        paused = false;
        S_ResumeSound();
    }
    else if (menu_was_paused && music_previewing)
    {
        S_PauseSound();
    }

    music_previewing = 0;
}

static void open_menu(void)
{
    if (gamestate != GS_LEVEL || !usergame || players[consoleplayer].mo == NULL)
        return;

    menu_was_paused = paused;
    if (!paused)
    {
        paused = true;
        S_PauseSound();
    }

    M_ClearMenus();
    DG_ResetInput();
    G_ResetInputState();
    D_ClearEvents();
    build_music_list();
    build_level_list();
    current_page = CHEAT_PAGE_MAIN;
    memset(selected, 0, sizeof(selected));
    memset(scroll_first, 0, sizeof(scroll_first));
    cursor_animation = 0;
    music_previewing = 0;
    menu_active = 1;
    S_StartSound(NULL, sfx_swtchn);
}

void doom_cheat_menu_request_open(void)
{
    if (!menu_active)
        open_requested = 1;
}

void doom_cheat_menu_queue(doom_cheat_menu_command_t command)
{
    unsigned int write = command_write;
    unsigned int next = (write + 1) % CHEAT_MENU_COMMANDS;

    if (next == command_read)
        return;

    command_queue[write] = command;
    command_write = next;
}

int doom_cheat_menu_captures_input(void)
{
    return menu_active || open_requested;
}

static void move_selection(int direction)
{
    int count = page_item_count(current_page);

    if (count <= 0)
        return;

    selected[current_page] += direction;
    if (selected[current_page] < 0)
        selected[current_page] = count - 1;
    else if (selected[current_page] >= count)
        selected[current_page] = 0;

    keep_selection_visible();
    S_StartSound(NULL, sfx_pstop);
}

static const char *cheat_code_at(int index)
{
    if (index == 1)
        return logical_gamemission == doom ? "IDSPISPOPD" : "IDCLIP";

    return cheat_items[index].code;
}

static void select_current(void)
{
    int item = selected[current_page];
    char level_code[CHEAT_MENU_CODE_LEN];

    switch (current_page)
    {
        case CHEAT_PAGE_MAIN:
            current_page = (cheat_page_t)(item + 1);
            keep_selection_visible();
            S_StartSound(NULL, sfx_pistol);
            break;

        case CHEAT_PAGE_CHEATS:
        {
            const cheat_item_t *cheat = &cheat_items[item];
            player_t *player = &players[consoleplayer];
            int power_was_active = cheat->power >= 0
                                && cheat->power < NUMPOWERS
                                && player->powers[cheat->power] != 0;

            doom_cheat_execute(cheat_code_at(item));

            // Vanilla expires most active power-up cheats on the next game
            // tick.  The overlay pauses those ticks, so finish that off
            // transition immediately while the menu is open.
            if (power_was_active && !netgame && gameskill != sk_nightmare)
            {
                player->powers[cheat->power] = 0;
                if (cheat->power == pw_invisibility && player->mo != NULL)
                    player->mo->flags &= ~MF_SHADOW;
            }

            S_StartSound(NULL, sfx_pistol);
            break;
        }

        case CHEAT_PAGE_MUSIC:
            if (item < music_count)
            {
                doom_cheat_execute(music_codes[item]);
                last_music_index = item;
                S_ResumeSound();
                music_previewing = 1;
                S_StartSound(NULL, sfx_pistol);
            }
            break;

        case CHEAT_PAGE_LEVEL:
            if (item < level_count)
            {
                M_StringCopy(level_code, level_codes[item], sizeof(level_code));
                close_menu();
                doom_cheat_execute(level_code);
            }
            break;

        default:
            break;
    }
}

static void go_back(void)
{
    if (current_page == CHEAT_PAGE_MAIN)
    {
        close_menu();
        S_StartSound(NULL, sfx_swtchx);
        return;
    }

    current_page = CHEAT_PAGE_MAIN;
    keep_selection_visible();
    S_StartSound(NULL, sfx_swtchn);
}

static void handle_command(doom_cheat_menu_command_t command)
{
    switch (command)
    {
        case DOOM_CHEAT_MENU_UP:
            move_selection(-1);
            break;
        case DOOM_CHEAT_MENU_DOWN:
            move_selection(1);
            break;
        case DOOM_CHEAT_MENU_SELECT:
            select_current();
            break;
        case DOOM_CHEAT_MENU_BACK:
            go_back();
            break;
    }
}

void doom_cheat_menu_ticker(void)
{
    if (open_requested)
    {
        open_requested = 0;
        command_read = command_write;
        open_menu();
    }

    while (menu_active && command_read != command_write)
    {
        doom_cheat_menu_command_t command = command_queue[command_read];
        command_read = (command_read + 1) % CHEAT_MENU_COMMANDS;
        handle_command(command);
    }

    cursor_animation = (cursor_animation + 1) & 15;
}

static void draw_centered(int y, char *text)
{
    M_WriteText((SCREENWIDTH - M_StringWidth(text)) / 2, y, text);
}

static int cheat_control_position(int index)
{
    const cheat_item_t *item = &cheat_items[index];
    player_t *player = &players[consoleplayer];

    if (index == (int)(sizeof(cheat_items) / sizeof(cheat_items[0])) - 1)
        return AM_GetCheatLevel();
    if (item->player_flag)
        return (player->cheats & item->player_flag) != 0 ? 2 : 0;
    if (item->power >= 0 && item->power < NUMPOWERS)
        return player->powers[item->power] != 0 ? 2 : 0;

    return -1;
}

static int current_music_row(void)
{
    int current = S_GetMusicNumber();
    int found = -1;
    int matches = 0;
    int i;

    for (i = 0; i < music_count; i++)
    {
        if (music_ids[i] == current)
        {
            found = i;
            matches++;
        }
    }

    if (matches == 1)
        return found;
    if (last_music_index >= 0 && last_music_index < music_count
     && music_ids[last_music_index] == current)
        return last_music_index;

    return -1;
}

static int current_level_row(void)
{
    int i;

    for (i = 0; i < level_count; i++)
    {
        const char *code = level_codes[i];

        if (gamemode == commercial)
        {
            if (code[6] == '0' + (gamemap / 10)
             && code[7] == '0' + (gamemap % 10))
                return i;
        }
        else if (code[6] == '0' + gameepisode
              && code[7] == '0' + gamemap)
        {
            return i;
        }
    }

    return -1;
}

static void row_text(int index, char *buffer, size_t size)
{
    switch (current_page)
    {
        case CHEAT_PAGE_MAIN:
            M_StringCopy(buffer, main_items[index], size);
            break;

        case CHEAT_PAGE_CHEATS:
            if (index == (int)(sizeof(cheat_items) / sizeof(cheat_items[0])) - 1)
            {
                M_StringCopy(buffer, "IDDT", size);
                break;
            }

            M_StringCopy(buffer, cheat_code_at(index), size);
            break;

        case CHEAT_PAGE_MUSIC:
            M_StringCopy(buffer, music_codes[index], size);
            break;

        case CHEAT_PAGE_LEVEL:
            M_StringCopy(buffer, level_codes[index], size);
            break;

        default:
            buffer[0] = '\0';
            break;
    }
}

static const char *row_description(int index)
{
    static const char *main_descriptions[] =
    {
        "CLASSIC DOOM CHEATS",
        "SELECT AVAILABLE MUSIC",
        "WARP TO AVAILABLE MAP"
    };
    static const char *cheat_descriptions[] =
    {
        "GOD MODE",
        "NO CLIPPING",
        "WEAPONS AMMO KEYS",
        "WEAPONS AND AMMO",
        "CHAINSAW POWER",
        "POWERUP PROMPT",
        "INVULNERABILITY",
        "BERSERK",
        "INVISIBILITY",
        "RADIATION SUIT",
        "COMPUTER MAP",
        "LIGHT AMP",
        "SHOW POSITION",
        "(NORMAL MAP, FULL MAP, MAP+OBJECTS)"
    };

    if (current_page == CHEAT_PAGE_MAIN)
        return main_descriptions[index];
    if (current_page == CHEAT_PAGE_CHEATS)
        return cheat_descriptions[index];

    return NULL;
}

static void draw_control(int x, int y, int position)
{
    // Three native Doom slider positions: binary cheats use the endpoints,
    // while IDDT uses all three automap modes.
    M_DrawThermo(x, y, 3, position);
}

static char *page_title(void)
{
    switch (current_page)
    {
        case CHEAT_PAGE_MAIN:
            return "CHEAT MENU";
        case CHEAT_PAGE_CHEATS:
            return "CHEATS";
        case CHEAT_PAGE_MUSIC:
            return "MUSIC";
        case CHEAT_PAGE_LEVEL:
            return "LEVEL";
        default:
            return "";
    }
}

void doom_cheat_menu_draw(void)
{
    char text[32];
    char *skull;
    int count;
    int first;
    int visible;
    int visible_rows;
    int row_height;
    int row;

    if (!menu_active)
        return;

    draw_centered(18, page_title());

    count = page_item_count(current_page);
    if (count <= 0)
    {
        draw_centered(86, "NONE AVAILABLE");
        return;
    }

    keep_selection_visible();
    draw_music_marker = current_page == CHEAT_PAGE_MUSIC
                      ? current_music_row() : -1;
    draw_level_marker = current_page == CHEAT_PAGE_LEVEL
                      ? current_level_row() : -1;
    first = scroll_first[current_page];
    visible_rows = page_visible_rows(current_page);
    row_height = page_row_height(current_page);
    visible = count - first;
    if (visible > visible_rows)
        visible = visible_rows;

    skull = cursor_animation < 8 ? "M_SKULL1" : "M_SKULL2";
    for (row = 0; row < visible; row++)
    {
        int index = first + row;
        int y = CHEAT_MENU_ROW_Y + row * row_height;
        int text_x = CHEAT_MENU_TEXT_X;
        const char *description = row_description(index);

        row_text(index, text, sizeof(text));

        if (current_page == CHEAT_PAGE_CHEATS)
        {
            int position = cheat_control_position(index);

            if (position >= 0)
                draw_control(text_x, y + 2, position);
            text_x += CHEAT_MENU_CONTROL_WIDTH + CHEAT_MENU_CONTROL_GAP;
        }

        M_WriteText2x(text_x, y, text);
        if (description != NULL)
            M_WriteText(text_x, y + 18, (char *)description);
        if ((current_page == CHEAT_PAGE_MUSIC
          && index == draw_music_marker)
         || (current_page == CHEAT_PAGE_LEVEL
          && index == draw_level_marker))
        {
            char *marker = "PLAYING";
            int marker_x = SCREENWIDTH - CHEAT_MENU_TEXT_X
                         - M_StringWidth(marker) * 2;

            M_WriteText2x(marker_x, y, marker);
        }

        if (index == selected[current_page])
            V_DrawPatchDirect(CHEAT_MENU_CURSOR_X, y - 2,
                              W_CacheLumpName(skull, PU_CACHE));
    }

    if (count > visible_rows)
    {
        if (first > 0)
            M_WriteText(296, CHEAT_MENU_ROW_Y, "^");
        if (first + visible < count)
            M_WriteText(296,
                        CHEAT_MENU_ROW_Y
                            + (visible_rows - 1) * row_height,
                        "V");

    }
}

void doom_cheat_menu_reset(void)
{
    open_requested = 0;
    command_read = command_write;
    close_menu();
    current_page = CHEAT_PAGE_MAIN;
}
