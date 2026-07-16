#include "doom_ml_compat.h"
#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

    if (DG_ScreenBuffer != NULL)
    {
        free(DG_ScreenBuffer);
        DG_ScreenBuffer = NULL;
    }

    DG_ScreenBuffer = malloc(
        DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(pixel_t)
    );

    if (DG_ScreenBuffer != NULL)
    {
        memset(
            DG_ScreenBuffer,
            0,
            DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(pixel_t)
        );
    }

	DG_Init();

	D_DoomMain ();
}
