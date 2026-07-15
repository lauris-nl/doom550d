#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <module.h>
#include <dryos.h>
#include <bmp.h>
#include <fio-ml.h>
#include <menu.h>
#include <timer.h>

#include "doomgeneric.h"
#include "m_menu.h"
#include "doomkeys.h"
#include "i_video.h"
#include "doomdef.h"
#include "d_player.h"
#include "doomstat.h"

#define DOOM_W 640
#define DOOM_H 400
#define DOOM_X ((720 - DOOM_W) / 2)
#define DOOM_Y ((480 - DOOM_H) / 2)

#define DOOM_LOG_FILE "ML/LOGS/DOOM550D.LOG"
#define DOOM_WAD_FILE "ML/DOOM/doom1.wad"

#define KEYQUEUE_SIZE 64

static volatile int module_running = 1;
static volatile int doom_running = 0;
static volatile int doom_task_active = 0;
static volatile uint32_t doom_start_ms = 0;
static volatile uint32_t doom_tick_count = 0;
static volatile uint32_t doom_draw_count = 0;

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
    FILE *file = FIO_OpenFile(DOOM_LOG_FILE, O_RDWR | O_SYNC);

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
    FILE *file = FIO_CreateFile(DOOM_LOG_FILE);

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

    if (!event)
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
    FILE *file = FIO_CreateFile("ML/LOGS/DOOMRAW.LOG");
    char line[160];

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

static int wad_exists(void)
{
    FILE *file = FIO_OpenFile(DOOM_WAD_FILE, O_RDONLY | O_SYNC);

    if (!file)
        return 0;

    FIO_CloseFile(file);
    return 1;
}

static void doom550d_task(void *arg)
{
    doom_log_reset();

    if (!wad_exists())
    {
        DOOM_LOG("ERROR: ML/DOOM/doom1.wad not found\n");

        bmp_printf(
            FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK),
            80, 200,
            "Missing: ML/DOOM/doom1.wad"
        );

        msleep(3000);
        clrscr();
        doom_running = 0;
        doom_task_active = 0;
        return;
    }

    clrscr();
    menu_redraw_blocked = 1;
    doom_running = 1;
    doom_start_ms = (uint32_t)get_ms_clock();
    doom_tick_count = 0;
    doom_draw_count = 0;
    doom_log_checkpoint("task-start");
    key_read = 0;
    key_write = 0;
    memset(key_state, 0, sizeof(key_state));
    memset(pulse_deadline, 0, sizeof(pulse_deadline));
    doom_raw_trace_count = 0;

    char *argv[] =
    {
        "doom",
        "-iwad",
        DOOM_WAD_FILE,
        "-nosound",
        "-nomusic",
        0
    };

    doom_ml_exit_requested = 0;
    DOOM_LOG("Calling doomgeneric_Create\n");
    doomgeneric_Create(5, argv);
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

    doom_raw_trace_dump();
    release_game_keys();
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

    doom_task_active = 1;

    task_create(
        "doom_task",
        0x1c,
        0x10000,
        doom550d_task,
        (void *)0
    );
}

static struct menu_entry doom550d_menu[] =
{
    {
        .name = "Doom",
        .select = doom550d_start,
        .help = "Run Doom from ML/DOOM/doom1.wad.",
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
#define DOOM_BGMT_PRESS_ZOOM_IN    0x0b
#define DOOM_BGMT_UNPRESS_ZOOM_IN  0x0c
#define DOOM_BGMT_PRESS_ZOOM_OUT   0x0d
#define DOOM_BGMT_UNPRESS_ZOOM_OUT 0x0e
#define DOOM_BGMT_Q                0x0f
#define DOOM_BGMT_PRESS_HALFSHUTTER   0x3f
#define DOOM_BGMT_UNPRESS_HALFSHUTTER 0x40
#define DOOM_BGMT_PRESS_FULLSHUTTER   0x41
#define DOOM_BGMT_UNPRESS_FULLSHUTTER 0x42

#define DOOM_OLC_EVENT                0x56
#define DOOM_FRONT_BUTTON_ARG         0x09
#define DOOM_FRONT_BUTTON_FLAG        0x04000000

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

    /*
     * De voorste scherptediepteknop verschijnt op de 550D als een
     * OLC-event. De status staat in bit 0x04000000 van event->obj.
     */
    if (key == DOOM_OLC_EVENT &&
        event->arg == DOOM_FRONT_BUTTON_ARG &&
        event->obj)
    {
        uint32_t flags = *(volatile uint32_t *)event->obj;

        if (!menuactive && (flags & DOOM_FRONT_BUTTON_FLAG))
            queue_key(1, KEY_RSHIFT);
        else
            release_key(KEY_RSHIFT);

        return 0;
    }

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

        /*
         * De ontspanknop start Canon-schermacties en wordt daarom
         * niet als Doom-besturing gebruikt.
         */
        case DOOM_BGMT_PRESS_HALFSHUTTER:
        case DOOM_BGMT_UNPRESS_HALFSHUTTER:
        case DOOM_BGMT_PRESS_FULLSHUTTER:
        case DOOM_BGMT_UNPRESS_FULLSHUTTER:
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
                queue_key(1, KEY_STRAFE_L);
            return 0;

        case DOOM_BGMT_UNPRESS_ZOOM_OUT:
            release_key(KEY_STRAFE_L);
            return 0;

        case DOOM_BGMT_PRESS_ZOOM_IN:
            if (!menuactive)
                queue_key(1, KEY_STRAFE_R);
            return 0;

        case DOOM_BGMT_UNPRESS_ZOOM_IN:
            release_key(KEY_STRAFE_R);
            return 0;

        case DOOM_BGMT_Q:
            release_game_keys();
            DOOM_LOG("Q sent Y\n");
            tap_key('y');
            return 0;

        case DOOM_BGMT_MENU:
            if ((uint32_t)get_ms_clock() - doom_start_ms < 5000)
            {
                DOOM_LOG("Ignored startup MENU event\n");
                return 0;
            }

            release_game_keys();
            DOOM_LOG("MENU sent Escape\n");
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

