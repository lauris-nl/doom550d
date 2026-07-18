#include "doom_ml_compat.h"

#include <ctype.h>
#include <string.h>

#include "am_map.h"
#include "doom_cheat.h"
#include "doomstat.h"
#include "m_cheat.h"
#include "st_stuff.h"

static int code_equals(const char *left, const char *right)
{
    while (*left && *right)
    {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return 0;
        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static void reset_cheat_sequences(void)
{
    int i;

    cht_ResetCheat(&cheat_mus);
    cht_ResetCheat(&cheat_god);
    cht_ResetCheat(&cheat_ammo);
    cht_ResetCheat(&cheat_ammonokey);
    cht_ResetCheat(&cheat_noclip);
    cht_ResetCheat(&cheat_commercial_noclip);
    for (i = 0; i < 7; i++)
        cht_ResetCheat(&cheat_powerup[i]);
    cht_ResetCheat(&cheat_choppers);
    cht_ResetCheat(&cheat_clev);
    cht_ResetCheat(&cheat_mypos);
    cht_ResetCheat(&cheat_amap);
}

int doom_cheat_execute(const char *code)
{
    event_t event;
    size_t i;
    size_t length;

    if (code == NULL || gamestate != GS_LEVEL || !usergame)
        return 0;

    length = strlen(code);
    if (length == 0 || length >= MAX_CHEAT_LEN)
        return 0;

    reset_cheat_sequences();

    // IDDT normally lives inside the active automap responder.  Reuse its
    // state transition directly so the camera menu can invoke it anywhere.
    if (code_equals(code, "iddt"))
        return AM_ExecuteCheat();

    memset(&event, 0, sizeof(event));
    event.type = ev_keydown;

    for (i = 0; i < length; i++)
    {
        int character = tolower((unsigned char)code[i]);

        if (character < 33 || character > 126)
            return 0;

        event.data1 = character;
        event.data2 = character;
        ST_Responder(&event);
    }

    return 1;
}
