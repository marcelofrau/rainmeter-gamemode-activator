#ifndef DETECTOR_H
#define DETECTOR_H

#include <windows.h>
#include <stdbool.h>

#define MAX_PROCESS_NAME 260

typedef enum {
    GAME_FLAG_FULLSCREEN = 1 << 0,
    GAME_FLAG_TOPMOST    = 1 << 1,
    GAME_FLAG_DX_LOADED  = 1 << 2,
    GAME_FLAG_NO_CAPTION = 1 << 3,
    GAME_FLAG_HAS_GINPUT = 1 << 4,
} GameFlags;

typedef struct {
    DWORD  pid;
    char   processName[MAX_PROCESS_NAME];
    HWND   hwnd;
    char   windowTitle[256];
    int    flags;
} GameInfo;

bool detect_game(GameInfo *info);

void whitelist_load(const char *path);

#endif
