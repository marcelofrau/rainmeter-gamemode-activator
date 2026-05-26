#include "detector.h"
#include <psapi.h>
#include <ctype.h>
#include <stdio.h>

static bool str_contains_any_lower(const char *str, const char *substrings[], int count) {
    char lower[256];
    int i = 0;
    while (str[i] && i < (int)sizeof(lower) - 1) {
        lower[i] = (char)tolower((unsigned char)str[i]);
        i++;
    }
    lower[i] = '\0';

    for (int j = 0; j < count; j++) {
        if (strstr(lower, substrings[j]))
            return true;
    }
    return false;
}

static void check_process_modules(HANDLE hProcess, int *flags) {
    HMODULE modules[1024];
    DWORD cbNeeded;

    if (!EnumProcessModules(hProcess, modules, sizeof(modules), &cbNeeded))
        return;

    static const char *gfx_dlls[] = {
        "d3d9", "d3d10", "d3d10core", "d3d11", "d3d12",
        "dxgi", "opengl32", "vulkan-1", "vulkan-1-loader",
    };
    static const char *game_input_dlls[] = {
        "xinput1_3", "xinput1_4", "xinput9_1_0",
        "dinput8",
    };
    static const char *extra_game_signals[] = {
        "nvapi", "nvapi64", "amd_ags",
        "opengl32",
        "vulkan-1", "vulkan-1-loader",
    };

    int count = cbNeeded / sizeof(HMODULE);
    for (int i = 0; i < count; i++) {
        char name[MAX_PATH];
        if (!GetModuleBaseNameA(hProcess, modules[i], name, sizeof(name)))
            continue;

        if (str_contains_any_lower(name, gfx_dlls, sizeof(gfx_dlls) / sizeof(gfx_dlls[0])))
            *flags |= GAME_FLAG_DX_LOADED;

        if (str_contains_any_lower(name, game_input_dlls, sizeof(game_input_dlls) / sizeof(game_input_dlls[0])))
            *flags |= GAME_FLAG_HAS_GINPUT;

        if (str_contains_any_lower(name, extra_game_signals, sizeof(extra_game_signals) / sizeof(extra_game_signals[0])))
            *flags |= GAME_FLAG_HAS_GINPUT;
    }
}

static bool is_known_non_game(const char *name) {
    if (!name || !*name) return false;
    char lower[MAX_PROCESS_NAME];
    int i = 0;
    while (name[i] && i < (int)sizeof(lower) - 1) {
        lower[i] = (char)tolower((unsigned char)name[i]);
        i++;
    }
    lower[i] = '\0';

    static const char *list[] = {
        "firefox.exe", "chrome.exe", "msedge.exe", "opera.exe", "brave.exe",
        "code.exe", "codep.exe",
        "WindowsTerminal.exe", "cmd.exe", "powershell.exe", "pwsh.exe",
        "explorer.exe", "mstsc.exe",
        "Teams.exe", "zoom.exe", "slack.exe",
        "SearchHost.exe", "SearchApp.exe",
        "StartMenuExperienceHost.exe",
        "ShellExperienceHost.exe", "sihost.exe",
        "TextInputHost.exe", "RuntimeBroker.exe",
        "taskhostw.exe", "ApplicationFrameHost.exe",
        "SystemSettings.exe", "SettingsApp.exe",
        "Rainmeter.exe",
        "wallpaper64.exe", "wallpaper32.exe",
        "discord.exe",
        "spotify.exe",
        "Steam.exe", "EpicGamesLauncher.exe", "Battle.net.exe",
        "devenv.exe",
        "idea64.exe", "rider64.exe", "clion64.exe",
        "sublime_text.exe",
        "notepad++.exe",
        "PowerToys.exe",
        NULL
    };

    for (int j = 0; list[j]; j++) {
        if (strcmp(lower, list[j]) == 0)
            return true;
    }
    return false;
}

static bool is_known_game(const char *name) {
    if (!name || !*name) return false;
    char lower[MAX_PROCESS_NAME];
    int i = 0;
    while (name[i] && i < (int)sizeof(lower) - 1) {
        lower[i] = (char)tolower((unsigned char)name[i]);
        i++;
    }
    lower[i] = '\0';

    static const char *list[] = {
        /* Java / sandbox */
        "javaw.exe", "java.exe",
        /* Hytale */
        "hytale.exe",
        /* Roblox */
        "RobloxPlayerBeta.exe",
        /* osu! */
        "osu!.exe",
        /* 2D / indie */
        "Terraria.exe", "Stardew Valley.exe",
        "factorio.exe", "RimWorldWin64.exe",
        /* Paradox */
        "ck3.exe", "eu4.exe", "hoi4.exe", "stellaris.exe",
        /* Emulators */
        "pcsx2.exe", "dolphin.exe", "rpcs3.exe",
        "yuzu.exe", "citra.exe",
        /* Valve */
        "cs2.exe", "dota2.exe",
        /* Riot */
        "League of Legends.exe", "VALORANT-Win64-Shipping.exe",
        NULL
    };

    for (int j = 0; list[j]; j++) {
        if (strcmp(lower, list[j]) == 0)
            return true;
    }
    return false;
}

/* ── Blacklist ────────────────────────────────────────────────────────
 * A plain-text file (gamemode_blacklist.txt) listing process names,
 * one per line, that should never trigger game detection. */

#define MAX_BLACKLIST_ENTRIES 64

static char user_blacklist[MAX_BLACKLIST_ENTRIES][MAX_PROCESS_NAME];
static int  user_blacklist_count = 0;

void blacklist_load(const char *path) {
    user_blacklist_count = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f) && user_blacklist_count < MAX_BLACKLIST_ENTRIES) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;

        char *end = p + strlen(p) - 1;
        while (end >= p && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t'))
            *end-- = '\0';

        if (*p == '\0') continue;

        char *dst = user_blacklist[user_blacklist_count];
        int i = 0;
        while (p[i] && i < MAX_PROCESS_NAME - 1) {
            dst[i] = (char)tolower((unsigned char)p[i]);
            i++;
        }
        dst[i] = '\0';
        user_blacklist_count++;
    }
    fclose(f);
}

static bool is_user_blacklisted(const char *name) {
    if (!name || !*name || user_blacklist_count == 0)
        return false;
    char lower[MAX_PROCESS_NAME];
    int i = 0;
    while (name[i] && i < MAX_PROCESS_NAME - 1) {
        lower[i] = (char)tolower((unsigned char)name[i]);
        i++;
    }
    lower[i] = '\0';
    for (int j = 0; j < user_blacklist_count; j++) {
        if (strcmp(lower, user_blacklist[j]) == 0)
            return true;
    }
    return false;
}

/* ── Whitelist ────────────────────────────────────────────────────────
 * A plain-text file (gamemode_whitelist.txt) listing process names,
 * one per line, that should always trigger game detection. */

#define MAX_WHITELIST_ENTRIES 64

static char whitelist[MAX_WHITELIST_ENTRIES][MAX_PROCESS_NAME];
static int  whitelist_count = 0;

void whitelist_load(const char *path) {
    whitelist_count = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f) && whitelist_count < MAX_WHITELIST_ENTRIES) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;

        char *end = p + strlen(p) - 1;
        while (end >= p && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t'))
            *end-- = '\0';

        if (*p == '\0') continue;

        char *dst = whitelist[whitelist_count];
        int i = 0;
        while (p[i] && i < MAX_PROCESS_NAME - 1) {
            dst[i] = (char)tolower((unsigned char)p[i]);
            i++;
        }
        dst[i] = '\0';
        whitelist_count++;
    }
    fclose(f);
}

bool detect_game(GameInfo *info) {
    if (!info) return false;
    memset(info, 0, sizeof(*info));

    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindowVisible(hwnd))
        return false;

    GetWindowThreadProcessId(hwnd, &info->pid);
    if (info->pid == 0)
        return false;

    GetWindowTextA(hwnd, info->windowTitle, sizeof(info->windowTitle));

    LONG style   = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    RECT wr;
    GetWindowRect(hwnd, &wr);

    HMONITOR hm = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hm) return false;

    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(hm, &mi)) return false;

    bool fullscreen =
        wr.left   <= mi.rcMonitor.left   &&
        wr.top    <= mi.rcMonitor.top    &&
        wr.right  >= mi.rcMonitor.right  &&
        wr.bottom >= mi.rcMonitor.bottom;

    bool no_caption = !(style & WS_CAPTION) && !(style & WS_THICKFRAME);
    bool topmost    = (exStyle & WS_EX_TOPMOST) != 0;

    /* ── Process info ──────────────────────────────────────────
     * Try limited rights first (works even for elevated/system processes).
     * The full path is checked so that ANY process under C:\Windows\ is
     * rejected outright – this catches Start Menu / Search / etc. even
     * if their process names change between Windows builds. */

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info->pid);

    if (hProcess) {
        char path[MAX_PATH];
        DWORD sz = sizeof(path);
        if (QueryFullProcessImageNameA(hProcess, 0, path, &sz)) {
            /* Extract filename for the blacklist */
            char *name = strrchr(path, '\\');
            if (name) {
                strcpy(info->processName, name + 1);
                *name = '\0';    /* truncate to directory for system check */
            } else {
                strcpy(info->processName, path);
            }

            /* Reject anything living under C:\Windows\ (system components) */
            for (int i = 0; path[i]; i++)
                path[i] = (char)tolower((unsigned char)path[i]);
            if (strncmp(path, "c:\\windows\\", 11) == 0) {
                strcpy(info->processName, "system");
                CloseHandle(hProcess);
                return false;
            }
        }
        CloseHandle(hProcess);
    }

    /* Fallback: classic GetModuleBaseNameA */
    if (!info->processName[0]) {
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                               FALSE, info->pid);
        if (hProcess) {
            GetModuleBaseNameA(hProcess, NULL, info->processName,
                               sizeof(info->processName));
            CloseHandle(hProcess);
        }
    }

    /* Module enumeration (may fail for protected processes). */
    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                           FALSE, info->pid);
    if (hProcess) {
        check_process_modules(hProcess, &info->flags);
        CloseHandle(hProcess);
    }

    if (!info->processName[0])
        strcpy(info->processName, "unknown");

    if (fullscreen)            info->flags |= GAME_FLAG_FULLSCREEN;
    if (no_caption)            info->flags |= GAME_FLAG_NO_CAPTION;
    if (topmost)               info->flags |= GAME_FLAG_TOPMOST;

    info->hwnd = hwnd;

    /* Whitelist check: force game mode for explicitly listed processes.
     * Runs before the blacklist so user entries override it. */
    if (whitelist_count > 0) {
        char lower[MAX_PROCESS_NAME];
        int i = 0;
        while (info->processName[i] && i < MAX_PROCESS_NAME - 1) {
            lower[i] = (char)tolower((unsigned char)info->processName[i]);
            i++;
        }
        lower[i] = '\0';
        for (int j = 0; j < whitelist_count; j++) {
            if (strcmp(lower, whitelist[j]) == 0)
                return true;
        }
    }

    if (is_known_game(info->processName))
        return true;

    if (is_known_non_game(info->processName))
        return false;

    if (is_user_blacklisted(info->processName))
        return false;

    if (fullscreen && no_caption)
        return true;

    if (no_caption && (info->flags & GAME_FLAG_DX_LOADED))
        return true;

    if (topmost && no_caption)
        return true;

    /* Windowed game with borders: loaded game-input APIs
     * (XInput, DirectInput) or GPU vendor libs (NVAPI, AGS).
     * XInput/DInput are almost exclusively used by games. */
    if (info->flags & GAME_FLAG_HAS_GINPUT)
        return true;

    return false;
}
