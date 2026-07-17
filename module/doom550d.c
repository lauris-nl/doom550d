#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <module.h>
#include <dryos.h>
#include <bmp.h>
#include <config.h>
#include <fio-ml.h>
#include <menu.h>
#include <timer.h>

#include "doomgeneric.h"
#include "m_menu.h"
#include "doomkeys.h"
#include "i_video.h"
#include "doomdef.h"
#include "d_main.h"
#include "d_player.h"
#include "doomstat.h"
#include "doom_audio_ml.h"
#include "doom_debug.h"
#include "m_config.h"
#include "p_saveg.h"

#define DOOM_W 720
#define DOOM_H 480
#define DOOM_X ((720 - DOOM_W) / 2)
#define DOOM_Y ((480 - DOOM_H) / 2)

#define DOOM_LOG_FILE "ML/LOGS/DOOM550D.LOG"
#define DOOM_WAD_DIR "ML/DOOM/"
#define DOOM_SAVE_DIR "ML/DOOM/SAVES"
#define DOOM_CONFIG_DIR "ML/DOOM/CONFIG"
#define DOOM_MAX_WAD_FILES 32
#define DOOM_MAX_LOG_FILES 32

static CONFIG_INT("games.doom550d.wad", doom_wad_choice, -1);
CONFIG_INT("games.doom550d.debug", doom_debug_enabled, 0);

#define KEYQUEUE_SIZE 64

static volatile int module_running = 1;
static volatile int doom_running = 0;
static volatile int doom_task_active = 0;
static volatile uint32_t doom_start_ms = 0;
static volatile uint32_t doom_tick_count = 0;
static volatile uint32_t doom_draw_count = 0;
static volatile int zoom_run_pressed = 0;
static char doom_wad_files[DOOM_MAX_WAD_FILES][FIO_MAX_PATH_LENGTH];
static int doom_wad_file_count = 0;
static int doom_wad_scan_done = 0;
static char doom_requested_wad_path[FIO_MAX_PATH_LENGTH];
static char doom_session_wad_path[FIO_MAX_PATH_LENGTH];

static unsigned short key_queue[KEYQUEUE_SIZE];
static unsigned int key_write = 0;
static unsigned int key_read = 0;
static unsigned char key_state[256];

#define DOOM_RAW_TRACE_MAX 192

struct doom_raw_trace_entry
{
    uint32_t ms;
    uint32_t type;
    uint32_t param;
    uint32_t arg;
    uint32_t obj;
};

static struct doom_raw_trace_entry doom_raw_trace[DOOM_RAW_TRACE_MAX];
static unsigned int doom_raw_trace_count = 0;

static uint32_t saved_palette[256];
static int palette_saved = 0;

extern int menu_redraw_blocked;
extern volatile int doom_ml_exit_requested;
extern boolean menuactive;

static void doom_log_write(const char *text, unsigned int length)
{
    FILE *file;

    if (!doom_debug_enabled)
        return;

    file = FIO_OpenFile(DOOM_LOG_FILE, O_RDWR | O_SYNC);

    if (!file)
        file = FIO_CreateFile(DOOM_LOG_FILE);

    if (!file)
        return;

    FIO_SeekSkipFile(file, 0, SEEK_END);
    FIO_WriteFile(file, text, length);
    FIO_CloseFile(file);
}

#define DOOM_LOG(text) doom_log_write((text), sizeof(text) - 1)

static void doom_log_checkpoint(const char *where)
{
    char buffer[192];
    int length = snprintf(
        buffer,
        sizeof(buffer),
        "%s tick=%d draw=%d module=%d running=%d shutdown=%d doomexit=%d ms=%d\n",
        where,
        (unsigned int)doom_tick_count,
        (unsigned int)doom_draw_count,
        (int)module_running,
        (int)doom_running,
        (int)ml_shutdown_requested,
        (int)doom_ml_exit_requested,
        (unsigned int)get_ms_clock()
    );

    if (length > 0)
    {
        unsigned int write_length =
            length < (int)sizeof(buffer)
            ? (unsigned int)length
            : (unsigned int)sizeof(buffer) - 1;

        doom_log_write(buffer, write_length);
    }
}

static void doom_log_reset(void)
{
    FILE *file;

    if (!doom_debug_enabled)
        return;

    file = FIO_CreateFile(DOOM_LOG_FILE);

    if (!file)
        return;

    static const char header[] =
        "DOOM550D engine log\n"
        "====================\n"
        "Starting Doomgeneric port\n";

    FIO_WriteFile(file, header, sizeof(header) - 1);
    FIO_CloseFile(file);
}

static int clamp_int(int value, int low, int high)
{
    if (value < low)
        return low;

    if (value > high)
        return high;

    return value;
}

static uint32_t rgb_to_canon_yuv(
    unsigned int red,
    unsigned int green,
    unsigned int blue
)
{
    /*
     * Integer BT.601-benadering.
     * Canon gebruikt een 0xAAYYUVVV-achtige palettewaarde.
     * Opacity 3 is de normale ondoorzichtige modus op deze DIGIC-generatie.
     */
    int y = (77 * (int)red + 150 * (int)green + 29 * (int)blue) >> 8;
    int u = (-43 * (int)red - 85 * (int)green + 128 * (int)blue) >> 8;
    int v = (128 * (int)red - 107 * (int)green - 21 * (int)blue) >> 8;

    y = clamp_int(y, 0, 255);
    u = clamp_int(u, -128, 127);
    v = clamp_int(v, -128, 127);

    return
        (3u << 24) |
        ((uint32_t)(y & 0xff) << 16) |
        ((uint32_t)(u & 0xff) << 8) |
        ((uint32_t)(v & 0xff));
}

static void save_current_palette(void)
{
    if (palette_saved)
        return;

    for (int i = 0; i < 256; i++)
        saved_palette[i] = LCD_Palette[i * 3 + 2];

    palette_saved = 1;
}

static void install_doom_palette(void)
{
    save_current_palette();

    for (int i = 0; i < 256; i++)
    {
        uint32_t entry = rgb_to_canon_yuv(
            colors[i].r,
            colors[i].g,
            colors[i].b
        );

        EngDrvOut(LCD_Palette[i * 3], entry);
        EngDrvOut(LCD_Palette[i * 3 + 0x300], entry);
    }

    palette_changed = false;
}

static void restore_saved_palette(void)
{
    if (!palette_saved)
        return;

    for (int i = 0; i < 256; i++)
    {
        EngDrvOut(LCD_Palette[i * 3], saved_palette[i]);
        EngDrvOut(LCD_Palette[i * 3 + 0x300], saved_palette[i]);
    }

    palette_saved = 0;
}

static void clear_doom_area(void)
{
    uint8_t *vram = bmp_vram();

    if (!vram)
        return;

    for (int y = 0; y < DOOM_H; y++)
    {
        uint8_t *row = vram + (DOOM_Y + y) * BMPPITCH + DOOM_X;
        memset(row, COLOR_TRANSPARENT_GRAY, DOOM_W);
    }
}

static uint32_t pulse_deadline[256];

static void queue_key(int pressed, unsigned char key)
{
    unsigned int next;
    pressed = pressed ? 1 : 0;

    if (key_state[key] == pressed)
        return;

    next = (key_write + 1) % KEYQUEUE_SIZE;

    if (next == key_read)
    {
        if (pressed)
            return;

        key_read = (key_read + 1) % KEYQUEUE_SIZE;
    }

    key_state[key] = pressed;
    key_queue[key_write] = ((pressed ? 1 : 0) << 8) | key;
    key_write = next;
}

static void tap_key(unsigned char key)
{
    queue_key(1, key);
    queue_key(0, key);
}

static void pulse_key(unsigned char key, uint32_t duration_ms)
{
    queue_key(1, key);
    pulse_deadline[key] = (uint32_t)get_ms_clock() + duration_ms;
}

static void release_key(unsigned char key)
{
    pulse_deadline[key] = 0;
    queue_key(0, key);
}

static void release_direction_keys(void)
{
    release_key(KEY_UPARROW);
    release_key(KEY_DOWNARROW);
    release_key(KEY_LEFTARROW);
    release_key(KEY_RIGHTARROW);
}

static void release_game_keys(void)
{
    release_direction_keys();
    release_key(KEY_STRAFE_L);
    release_key(KEY_STRAFE_R);
    release_key(KEY_RALT);
    release_key(KEY_RSHIFT);
    release_key(KEY_FIRE);
    release_key(KEY_USE);
}

static void update_pulsed_keys(void)
{
    static const unsigned char pulsed_keys[] =
    {
        KEY_UPARROW,
        KEY_DOWNARROW,
        KEY_LEFTARROW,
        KEY_RIGHTARROW,
        KEY_FIRE,
        KEY_USE,
        KEY_ESCAPE,
        KEY_ENTER,
        KEY_TAB,
        'y'
    };

    uint32_t now = (uint32_t)get_ms_clock();

    for (unsigned int i = 0; i < COUNT(pulsed_keys); i++)
    {
        unsigned char key = pulsed_keys[i];
        uint32_t deadline = pulse_deadline[key];

        if (deadline && (int32_t)(now - deadline) >= 0)
            release_key(key);
    }
}

static void update_run_key(void)
{
    if (doom_running && !menuactive && zoom_run_pressed)
        queue_key(1, KEY_RSHIFT);
    else
        release_key(KEY_RSHIFT);
}

void DG_ResetInput(void)
{
    /*
     * A load is selected while the menu key sequence is still in flight.
     * Throw away that old sequence before accepting fresh camera buttons.
     */
    key_read = 0;
    key_write = 0;
    memset(key_queue, 0, sizeof(key_queue));
    memset(key_state, 0, sizeof(key_state));
    memset(pulse_deadline, 0, sizeof(pulse_deadline));
}


static void doom_raw_trace_add(const struct event *event)
{
    unsigned int index;

    if (!doom_debug_enabled || !event)
        return;

    index = doom_raw_trace_count;

    if (index >= DOOM_RAW_TRACE_MAX)
        return;

    doom_raw_trace[index].ms = (uint32_t)get_ms_clock();
    doom_raw_trace[index].type = (uint32_t)event->type;
    doom_raw_trace[index].param = (uint32_t)event->param;
    doom_raw_trace[index].arg = (uint32_t)event->arg;
    doom_raw_trace[index].obj = (uint32_t)(uintptr_t)event->obj;
    doom_raw_trace_count = index + 1;
}

static void doom_raw_trace_dump(void)
{
    FILE *file;
    char line[160];

    if (!doom_debug_enabled)
        return;

    file = FIO_CreateFile("ML/LOGS/DOOMRAW.LOG");

    if (!file)
        return;

    {
        static const char header[] =
            "Doom 550D raw key trace\n"
            "=======================\n";

        FIO_WriteFile(file, header, sizeof(header) - 1);
    }

    for (unsigned int i = 0; i < doom_raw_trace_count; i++)
    {
        int length = snprintf(
            line,
            sizeof(line),
            "%d type=0x%x param=0x%x arg=0x%x obj=0x%x\n",
            (unsigned int)doom_raw_trace[i].ms,
            (unsigned int)doom_raw_trace[i].type,
            (unsigned int)doom_raw_trace[i].param,
            (unsigned int)doom_raw_trace[i].arg,
            (unsigned int)doom_raw_trace[i].obj
        );

        if (length > 0)
        {
            unsigned int write_length =
                length < (int)sizeof(line)
                ? (unsigned int)length
                : (unsigned int)sizeof(line) - 1;

            FIO_WriteFile(file, line, write_length);
        }
    }

    FIO_CloseFile(file);
}

static void cycle_owned_weapon(int direction)
{
    player_t *player;
    int start;

    if (menuactive)
        return;

    player = &players[consoleplayer];

    if (player->pendingweapon != wp_nochange)
        start = (int)player->pendingweapon;
    else
        start = (int)player->readyweapon;

    for (int step = 1; step < NUMWEAPONS; step++)
    {
        int candidate =
            (start + direction * step + NUMWEAPONS) % NUMWEAPONS;

        if (player->weaponowned[candidate])
        {
            player->pendingweapon = (weapontype_t)candidate;
            return;
        }
    }
}

/* Doomgeneric platformfuncties */

void DG_Init(void)
{
    save_current_palette();
    DOOM_LOG("DG_Init complete\n");
}

void DG_DrawFrame(void)
{
    uint8_t *vram = bmp_vram();

    doom_draw_count++;

    if (doom_draw_count <= 10)
        doom_log_checkpoint("draw");

    if (!vram || !DG_ScreenBuffer)
        return;

    if (palette_changed)
        install_doom_palette();

    for (int y = 0; y < DOOM_H; y++)
    {
        uint8_t *dst = vram + (DOOM_Y + y) * BMPPITCH + DOOM_X;
        uint8_t *src = (uint8_t *)DG_ScreenBuffer + y * DOOM_W;
        memcpy(dst, src, DOOM_W);
    }
}

void DG_SleepMs(uint32_t ms)
{
    msleep(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)get_ms_clock();
}

int DG_GetKey(int *pressed, unsigned char *doom_key)
{
    update_run_key();
    update_pulsed_keys();

    if (key_read == key_write)
        return 0;

    unsigned short event = key_queue[key_read];
    key_read = (key_read + 1) % KEYQUEUE_SIZE;

    *pressed = (event >> 8) & 1;
    *doom_key = event & 0xff;

    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    DOOM_LOG("Doom title received\n");
}

/* Magic Lantern module */

static int wad_exists(const char *path)
{
    FILE *file = FIO_OpenFile(path, O_RDONLY | O_SYNC);

    if (!file)
        return 0;

    FIO_CloseFile(file);
    return 1;
}

static int ascii_lower(int character)
{
    return
        character >= 'A' && character <= 'Z'
        ? character + ('a' - 'A')
        : character;
}

static int is_doom_log_name(const char *name)
{
    size_t length;
    static const char prefix[] = "doom";

    if (!name)
        return 0;

    length = strlen(name);
    if (length < 8)
        return 0;

    for (unsigned int i = 0; i < sizeof(prefix) - 1; i++)
        if (ascii_lower(name[i]) != prefix[i])
            return 0;

    return
        name[length - 4] == '.' &&
        ascii_lower(name[length - 3]) == 'l' &&
        ascii_lower(name[length - 2]) == 'o' &&
        ascii_lower(name[length - 1]) == 'g';
}

static int wad_name_compare(const char *left, const char *right)
{
    while (*left && *right)
    {
        int difference = ascii_lower(*left) - ascii_lower(*right);

        if (difference)
            return difference;

        left++;
        right++;
    }

    return ascii_lower(*left) - ascii_lower(*right);
}

static int has_wad_extension(const char *name)
{
    size_t length = strlen(name);

    return
        length > 4 &&
        name[length - 4] == '.' &&
        ascii_lower(name[length - 3]) == 'w' &&
        ascii_lower(name[length - 2]) == 'a' &&
        ascii_lower(name[length - 1]) == 'd';
}

static int has_iwad_header(const char *path)
{
    char header[4];
    FILE *file = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    int read;

    if (!file)
        return 0;

    read = FIO_ReadFile(file, header, sizeof(header));
    FIO_CloseFile(file);

    return read == (int)sizeof(header) && !memcmp(header, "IWAD", 4);
}

static void sort_wad_files(void)
{
    char temporary[FIO_MAX_PATH_LENGTH];

    for (int i = 0; i < doom_wad_file_count - 1; i++)
    {
        for (int j = i + 1; j < doom_wad_file_count; j++)
        {
            if (wad_name_compare(doom_wad_files[i], doom_wad_files[j]) > 0)
            {
                memcpy(temporary, doom_wad_files[i], sizeof(temporary));
                memcpy(doom_wad_files[i], doom_wad_files[j], sizeof(temporary));
                memcpy(doom_wad_files[j], temporary, sizeof(temporary));
            }
        }
    }
}

static void scan_wad_files(void)
{
    char previous_name[FIO_MAX_PATH_LENGTH];
    struct fio_file *file;
    struct fio_dirent *dirent;

    previous_name[0] = '\0';

    if (doom_wad_choice >= 0 && doom_wad_choice < doom_wad_file_count)
    {
        snprintf(
            previous_name,
            sizeof(previous_name),
            "%s",
            doom_wad_files[doom_wad_choice]
        );
    }

    doom_wad_file_count = 0;
    file = alloc_fio_file();

    if (!file)
    {
        doom_wad_scan_done = 1;
        doom_wad_choice = -1;
        return;
    }

    dirent = FIO_FindFirstEx(DOOM_WAD_DIR, file);

    if (!IS_ERROR(dirent))
    {
        do
        {
            struct file_info info = convert_fio_file_info(file);
            char path[FIO_MAX_PATH_LENGTH];
            int path_length;

            if (!info.name[0] || (info.mode & ATTR_DIRECTORY))
                continue;

            if (!has_wad_extension(info.name))
                continue;

            path_length = snprintf(
                path,
                sizeof(path),
                "%s%s",
                DOOM_WAD_DIR,
                info.name
            );

            if (path_length < 0 || path_length >= (int)sizeof(path))
                continue;

            if (!has_iwad_header(path))
                continue;

            snprintf(
                doom_wad_files[doom_wad_file_count],
                sizeof(doom_wad_files[doom_wad_file_count]),
                "%s",
                info.name
            );

            doom_wad_file_count++;
        }
        while (
            doom_wad_file_count < DOOM_MAX_WAD_FILES &&
            FIO_FindNextEx(dirent, file) == 0
        );

        FIO_FindClose(dirent);
    }

    free(file);
    sort_wad_files();
    doom_wad_scan_done = 1;

    if (previous_name[0])
    {
        for (int i = 0; i < doom_wad_file_count; i++)
        {
            if (!wad_name_compare(previous_name, doom_wad_files[i]))
            {
                doom_wad_choice = i;
                return;
            }
        }
    }

    if (doom_wad_choice >= 0 && doom_wad_choice < doom_wad_file_count)
        return;

    doom_wad_choice = doom_wad_file_count ? 0 : -1;

    for (int i = 0; i < doom_wad_file_count; i++)
    {
        if (!wad_name_compare(doom_wad_files[i], "doom1.wad"))
        {
            doom_wad_choice = i;
            break;
        }
    }
}

static void doom_log_wad(const char *prefix, const char *path)
{
    char buffer[128];
    int length = snprintf(buffer, sizeof(buffer), "%s%s\n", prefix, path);

    if (length > 0)
    {
        unsigned int write_length =
            length < (int)sizeof(buffer)
            ? (unsigned int)length
            : (unsigned int)sizeof(buffer) - 1;

        doom_log_write(buffer, write_length);
    }
}

static void show_storage_message(
    const char *title,
    const char *instruction
)
{
    menu_redraw_blocked = 1;
    clrscr();

    bmp_printf(
        FONT(FONT_LARGE, COLOR_WHITE, COLOR_BLACK),
        90, 150,
        "%s",
        title
    );
    bmp_printf(
        FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK),
        85, 235,
        "%s",
        instruction
    );

    msleep(5000);
    clrscr();
    menu_redraw_blocked = 0;
}

static void ensure_doom_directories(void)
{
    FIO_CreateDirectory("ML/DOOM");
    FIO_CreateDirectory(DOOM_SAVE_DIR);
    FIO_CreateDirectory(DOOM_CONFIG_DIR);
}

static int verify_save_storage(void)
{
    const char *test_path = P_TempSaveGameFile();
    FILE *file;

    doom_log_wad("Save test path: ", test_path);
    FIO_RemoveFile(test_path);
    file = FIO_CreateFile(test_path);

    if (!file)
    {
        DOOM_LOG("ERROR: could not create save test file\n");
        return 0;
    }

    FIO_CloseFile(file);

    if (FIO_RemoveFile(test_path) != 0)
    {
        DOOM_LOG("ERROR: could not remove save test file\n");
        return 0;
    }

    return 1;
}

static void doom550d_task(void *arg)
{
    const char *wad_path = doom_requested_wad_path;

    doom_log_reset();

    if (!wad_path[0])
    {
        DOOM_LOG("ERROR: no IWAD found in ML/DOOM\n");
        show_storage_message(
            "No WAD files found",
            "Copy a Doom IWAD file to ML/DOOM"
        );
        doom_running = 0;
        doom_task_active = 0;
        return;
    }

    if (!wad_exists(wad_path))
    {
        doom_log_wad("ERROR: missing ", wad_path);

        bmp_printf(
            FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK),
            80, 185,
            "Missing selected WAD:"
        );
        bmp_printf(
            FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK),
            80, 215,
            "%s",
            wad_path
        );

        msleep(3000);
        clrscr();
        doom_running = 0;
        doom_task_active = 0;
        return;
    }

    if (doom_session_wad_path[0] && strcmp(doom_session_wad_path, wad_path))
    {
        doom_log_wad("ERROR: current session IWAD: ", doom_session_wad_path);
        doom_log_wad("ERROR: requested IWAD: ", wad_path);
        DOOM_LOG("ERROR: camera restart required before changing IWAD\n");

        menu_redraw_blocked = 1;
        clrscr();
        bmp_printf(
            FONT(FONT_LARGE, COLOR_WHITE, COLOR_BLACK),
            155, 155,
            "WAD changed"
        );
        bmp_printf(
            FONT(FONT_LARGE, COLOR_WHITE, COLOR_BLACK),
            105, 225,
            "Restart camera"
        );

        msleep(5000);
        clrscr();
        menu_redraw_blocked = 0;
        doom_running = 0;
        doom_task_active = 0;
        return;
    }

    snprintf(
        doom_session_wad_path,
        sizeof(doom_session_wad_path),
        "%s",
        wad_path
    );
    P_SetSaveGameDir(wad_path);

    if (!verify_save_storage())
    {
        doom_log_wad("ERROR: save storage is not writable for ", wad_path);
        show_storage_message(
            "Save storage error",
            "Cannot write to ML/DOOM/SAVES"
        );
        doom_session_wad_path[0] = '\0';
        doom_running = 0;
        doom_task_active = 0;
        return;
    }

    doom_log_wad("Selected IWAD: ", wad_path);

    clrscr();
    menu_redraw_blocked = 1;
    doom_running = 1;
    doom_start_ms = (uint32_t)get_ms_clock();
    doom_tick_count = 0;
    doom_draw_count = 0;
    zoom_run_pressed = 0;
    doom_log_checkpoint("task-start");
    key_read = 0;
    key_write = 0;
    memset(key_state, 0, sizeof(key_state));
    memset(pulse_deadline, 0, sizeof(pulse_deadline));
    doom_raw_trace_count = 0;
    gamestate = GS_DEMOSCREEN;
    wipegamestate = GS_DEMOSCREEN;
    D_ResetDisplayState();

    char *argv[] =
    {
        "doom",
        "-iwad",
        (char *)wad_path,
        0
    };

    doom_ml_exit_requested = 0;
    DOOM_LOG("Calling doomgeneric_Create\n");
    doomgeneric_Create(3, argv);
    DOOM_LOG("doomgeneric_Create returned\n");

    while (1)
    {
        if (!module_running)
        {
            doom_log_checkpoint("stop-module-running");
            break;
        }

        if (!doom_running)
        {
            doom_log_checkpoint("stop-doom-running");
            break;
        }

        if (ml_shutdown_requested)
        {
            doom_log_checkpoint("stop-ml-shutdown");
            break;
        }

        if (doom_ml_exit_requested)
        {
            doom_log_checkpoint("stop-doom-exit");
            break;
        }

        doom_tick_count++;

        if (doom_tick_count <= 10)
            doom_log_checkpoint("tick-before");

        doomgeneric_Tick();

        if (doom_tick_count <= 10)
            doom_log_checkpoint("tick-after");
    }

    DOOM_LOG("Leaving Doom loop\n");

    M_SaveDefaults();
    doom_raw_trace_dump();
    release_game_keys();
    M_ResetSessionState();
    clear_doom_area();
    restore_saved_palette();
    clrscr();

    doom_running = 0;
    doom_task_active = 0;
    doom_ml_exit_requested = 0;
    menu_redraw_blocked = 0;
}

static MENU_SELECT_FUNC(doom550d_start)
{
    if (doom_task_active)
        return;

    scan_wad_files();
    doom_requested_wad_path[0] = '\0';

    if (doom_wad_choice >= 0 && doom_wad_choice < doom_wad_file_count)
    {
        snprintf(
            doom_requested_wad_path,
            sizeof(doom_requested_wad_path),
            "%s%s",
            DOOM_WAD_DIR,
            doom_wad_files[doom_wad_choice]
        );
    }

    doom_task_active = 1;

    task_create(
        "doom_task",
        0x1c,
        0x10000,
        doom550d_task,
        (void *)0
    );
}

static MENU_UPDATE_FUNC(doom_wad_update)
{
    if (!doom_wad_scan_done)
        scan_wad_files();

    if (doom_wad_choice >= 0 && doom_wad_choice < doom_wad_file_count)
        MENU_SET_VALUE("%s", doom_wad_files[doom_wad_choice]);
    else
        MENU_SET_VALUE("No IWAD files");
}

static MENU_SELECT_FUNC(doom_wad_select)
{
    if (!doom_wad_scan_done)
        scan_wad_files();

    if (!doom_wad_file_count || !delta)
        return;

    doom_wad_choice += delta;

    while (doom_wad_choice < 0)
        doom_wad_choice += doom_wad_file_count;

    while (doom_wad_choice >= doom_wad_file_count)
        doom_wad_choice -= doom_wad_file_count;
}

static MENU_SELECT_FUNC(doom_logs_clear)
{
    char paths[DOOM_MAX_LOG_FILES][FIO_MAX_PATH_LENGTH];
    struct fio_file *file;
    struct fio_dirent *dirent;
    int found = 0;
    int deleted = 0;
    int failed = 0;

    if (doom_task_active || doom_running)
    {
        NotifyBox(3000, "Stop Doom before clearing logs");
        return;
    }

    file = alloc_fio_file();
    if (!file)
    {
        NotifyBox(3000, "Cannot scan ML/LOGS");
        return;
    }

    dirent = FIO_FindFirstEx("ML/LOGS/", file);
    if (!IS_ERROR(dirent))
    {
        do
        {
            struct file_info info = convert_fio_file_info(file);
            int length;

            if (!info.name[0] || (info.mode & ATTR_DIRECTORY))
                continue;
            if (!is_doom_log_name(info.name))
                continue;

            length = snprintf(
                paths[found],
                sizeof(paths[found]),
                "ML/LOGS/%s",
                info.name
            );
            if (length < 0 || length >= (int)sizeof(paths[found]))
                continue;

            found++;
        }
        while (
            found < DOOM_MAX_LOG_FILES &&
            FIO_FindNextEx(dirent, file) == 0
        );

        FIO_FindClose(dirent);
    }
    free(file);

    for (int i = 0; i < found; i++)
    {
        if (FIO_RemoveFile(paths[i]) == 0)
            deleted++;
        else
            failed++;
    }
    doom_raw_trace_count = 0;

    if (!found)
        NotifyBox(3000, "No Doom logs found");
    else if (failed)
        NotifyBox(4000, "Doom logs: %d deleted, %d failed", deleted, failed);
    else
        NotifyBox(3000, "%d Doom log%s deleted", deleted, deleted == 1 ? "" : "s");
}

static struct menu_entry doom550d_menu[] =
{
    {
        .name = "Doom",
        .select = doom550d_start,
        .help = "Run Doom with the selected IWAD from ML/DOOM.",
        .children = (struct menu_entry[])
        {
            {
                .name = "WAD",
                .priv = &doom_wad_choice,
                .select = doom_wad_select,
                .update = doom_wad_update,
                .help = "Choose an IWAD found in ML/DOOM.",
                .help2 = "PWAD level and mod files are not shown.",
            },
            {
                .name = "Debug logging",
                .priv = &doom_debug_enabled,
                .min = 0,
                .max = 1,
                .choices = (const char *[]) { "OFF", "ON" },
                .help = "Write Doom diagnostics to ML/LOGS.",
                .help2 = "Keep OFF for normal play; enable before reproducing a bug.",
            },
            {
                .name = "Clear Doom logs",
                .select = doom_logs_clear,
                .help = "Delete only ML/LOGS/DOOM*.LOG files.",
                .help2 = "Savegames, WADs and configuration are never removed.",
            },
            MENU_EOL,
        },
    }
};

/*
 * Gebruik de ruwe Canon-events.
 *
 * De portable ML API voegt alle richting-loslaat-events samen tot
 * MODULE_KEY_UNPRESS_UDLR. Op de 550D bestaan echter vier afzonderlijke
 * raw events; daarmee kunnen we echte hold/release-besturing maken.
 */
#define DOOM_BGMT_PRESS_RIGHT    0x1a
#define DOOM_BGMT_UNPRESS_RIGHT  0x1b
#define DOOM_BGMT_PRESS_LEFT     0x1c
#define DOOM_BGMT_UNPRESS_LEFT   0x1d
#define DOOM_BGMT_PRESS_UP       0x1e
#define DOOM_BGMT_UNPRESS_UP     0x1f
#define DOOM_BGMT_PRESS_DOWN     0x20
#define DOOM_BGMT_UNPRESS_DOWN   0x21
#define DOOM_BGMT_WHEEL_LEFT       0x02
#define DOOM_BGMT_WHEEL_RIGHT      0x03
#define DOOM_BGMT_PRESS_SET        0x04
#define DOOM_BGMT_UNPRESS_SET      0x05
#define DOOM_BGMT_MENU             0x06
#define DOOM_BGMT_INFO             0x07
#define DOOM_BGMT_PLAY             0x09
#define DOOM_BGMT_TRASH            0x0a
#define DOOM_BGMT_PRESS_ZOOM_IN    0x0b
#define DOOM_BGMT_UNPRESS_ZOOM_IN  0x0c
#define DOOM_BGMT_PRESS_ZOOM_OUT   0x0d
#define DOOM_BGMT_UNPRESS_ZOOM_OUT 0x0e
#define DOOM_BGMT_Q                0x0f
#define DOOM_BGMT_PRESS_HALFSHUTTER   0x3f
#define DOOM_BGMT_UNPRESS_HALFSHUTTER 0x40
#define DOOM_BGMT_PRESS_FULLSHUTTER   0x41
#define DOOM_BGMT_UNPRESS_FULLSHUTTER 0x42

static unsigned int doom550d_keypress_raw(unsigned int context)
{
    struct event *event = (struct event *)(uintptr_t)context;
    unsigned int key;

    if (!doom_running || !event)
        return 1;

    doom_raw_trace_add(event);

    if (event->type != 0)
        return 1;

    key = event->param;

    switch (key)
    {
        case DOOM_BGMT_PRESS_UP:
            if (menuactive)
                tap_key(KEY_UPARROW);
            else
                queue_key(1, KEY_UPARROW);
            return 0;

        case DOOM_BGMT_UNPRESS_UP:
            release_key(KEY_UPARROW);
            return 0;

        case DOOM_BGMT_PRESS_DOWN:
            if (menuactive)
                tap_key(KEY_DOWNARROW);
            else
                queue_key(1, KEY_DOWNARROW);
            return 0;

        case DOOM_BGMT_UNPRESS_DOWN:
            release_key(KEY_DOWNARROW);
            return 0;

        case DOOM_BGMT_PRESS_LEFT:
            if (menuactive)
                tap_key(KEY_LEFTARROW);
            else
                queue_key(1, KEY_LEFTARROW);
            return 0;

        case DOOM_BGMT_UNPRESS_LEFT:
            release_key(KEY_LEFTARROW);
            return 0;

        case DOOM_BGMT_PRESS_RIGHT:
            if (menuactive)
                tap_key(KEY_RIGHTARROW);
            else
                queue_key(1, KEY_RIGHTARROW);
            return 0;

        case DOOM_BGMT_UNPRESS_RIGHT:
            release_key(KEY_RIGHTARROW);
            return 0;

        case DOOM_BGMT_PRESS_SET:
            if (menuactive)
                tap_key(KEY_ENTER);
            else
                queue_key(1, KEY_FIRE);
            return 0;

        case DOOM_BGMT_UNPRESS_SET:
            release_key(KEY_FIRE);
            return 0;

        case DOOM_BGMT_PRESS_HALFSHUTTER:
        case DOOM_BGMT_UNPRESS_HALFSHUTTER:
        case DOOM_BGMT_PRESS_FULLSHUTTER:
        case DOOM_BGMT_UNPRESS_FULLSHUTTER:
            /* Canon handles autofocus before the GUI event reaches modules.
             * Consume the event here, but never use it for Doom controls. */
            release_key(KEY_FIRE);
            return 0;

        case DOOM_BGMT_WHEEL_LEFT:
            if (!menuactive)
                cycle_owned_weapon(-1);
            return 0;

        case DOOM_BGMT_WHEEL_RIGHT:
            if (!menuactive)
                cycle_owned_weapon(1);
            return 0;

        case DOOM_BGMT_INFO:
            if (menuactive)
                tap_key(KEY_ESCAPE);
            else
                tap_key(KEY_TAB);
            return 0;

        case DOOM_BGMT_PLAY:
            if (!menuactive)
                pulse_key(KEY_USE, 140);
            return 0;

        case DOOM_BGMT_PRESS_ZOOM_OUT:
            if (!menuactive)
                queue_key(1, KEY_RALT);
            return 0;

        case DOOM_BGMT_UNPRESS_ZOOM_OUT:
            release_key(KEY_RALT);
            return 0;

        case DOOM_BGMT_PRESS_ZOOM_IN:
            zoom_run_pressed = 1;
            update_run_key();
            return 0;

        case DOOM_BGMT_UNPRESS_ZOOM_IN:
            zoom_run_pressed = 0;
            update_run_key();
            return 0;

        case DOOM_BGMT_Q:
            release_game_keys();
            tap_key('y');
            return 0;

        /* Never let Delete open a hidden Magic Lantern menu over Doom. */
        case DOOM_BGMT_TRASH:
            release_game_keys();
            tap_key(KEY_ESCAPE);
            return 0;

        case DOOM_BGMT_MENU:
            if ((uint32_t)get_ms_clock() - doom_start_ms < 5000)
            {
                return 0;
            }

            release_game_keys();
            tap_key(KEY_ESCAPE);
            return 0;

        default:
            return 1;
    }
}

static unsigned int doom550d_init(void)
{
    module_running = 1;
    doom_running = 0;
    doom_task_active = 0;
    doom_wad_scan_done = 0;
    doom_requested_wad_path[0] = '\0';
    doom_session_wad_path[0] = '\0';

    ensure_doom_directories();

    menu_add("Games", doom550d_menu, COUNT(doom550d_menu));

    printf("doom550d: Doomgeneric menu registered\n");
    return 0;
}

static unsigned int doom550d_deinit(void)
{
    doom_log_checkpoint("deinit-enter");
    module_running = 0;
    doom_running = 0;
    doom_task_active = 0;

    doom_audio_ml_force_shutdown();

    msleep(100);
    clear_doom_area();
    restore_saved_palette();
    menu_redraw_blocked = 0;

    DOOM_LOG("Module deinitialized\n");
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(doom550d_init)
    MODULE_DEINIT(doom550d_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_KEYPRESS_RAW, doom550d_keypress_raw, 0)
MODULE_CBRS_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(doom_wad_choice)
    MODULE_CONFIG(doom_debug_enabled)
MODULE_CONFIGS_END()
