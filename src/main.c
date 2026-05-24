#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "detector.h"
#include "logger.h"
#include "resource.h"

#ifndef VERSION
#define VERSION "0.0.0"
#endif

#define WM_TRAYICON   (WM_APP + 1)
#define ID_TRAY_TOGGLE 1001
#define ID_TRAY_ABOUT 1002
#define ID_TRAY_QUIT  1003
#define TIMER_POLL    1

static Logger log;
static NOTIFYICONDATAA nid;
static HINSTANCE hInst;
static bool game_active = false;
static GameInfo current_info;
static int poll_interval = 2000;
static char logfile_path[MAX_PATH] = "gamemode.log";
static char marker_path[MAX_PATH * 2];
static HANDLE hMarkerProcess = NULL;
static bool verbose = false;
static bool manual_override = false;

static const char *flags_to_string(int flags) {
    static char buf[128];
    buf[0] = '\0';

    if (flags & GAME_FLAG_FULLSCREEN) strcat(buf, "fullscreen ");
    if (flags & GAME_FLAG_TOPMOST)    strcat(buf, "topmost ");
    if (flags & GAME_FLAG_DX_LOADED)  strcat(buf, "dx ");
    if (flags & GAME_FLAG_NO_CAPTION) strcat(buf, "borderless ");
    if (flags & GAME_FLAG_HAS_GINPUT) strcat(buf, "input ");

    size_t len = strlen(buf);
    if (len > 0) buf[len - 1] = '\0';
    return buf;
}

static void update_tray_icon(HWND hwnd, bool active) {
    (void)hwnd;
    nid.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(active ? IDI_GAMEMODE : IDI_SLEEP));
    strcpy(nid.szTip, active ? "Game Mode Activator - Game Active" : "Game Mode Activator");
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

static void spawn_marker(void) {
    if (hMarkerProcess) return;

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;

    if (CreateProcessA(NULL, marker_path, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        hMarkerProcess = pi.hProcess;
        logger_log(&log, ">> Spawned marker process (PID: %lu)",
                   (unsigned long)pi.dwProcessId);
    } else {
        logger_log(&log, "!! Failed to spawn marker process (error %lu)",
                   (unsigned long)GetLastError());
    }
}

static void kill_marker(void) {
    if (!hMarkerProcess) return;
    TerminateProcess(hMarkerProcess, 0);
    WaitForSingleObject(hMarkerProcess, 100);
    CloseHandle(hMarkerProcess);
    hMarkerProcess = NULL;
    logger_log(&log, "<< Terminated marker process");
}

static void show_context_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    bool effective = game_active || manual_override;
    AppendMenuA(menu, MF_STRING, ID_TRAY_TOGGLE,
                effective ? "Disable Game Mode" : "Enable Game Mode");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, ID_TRAY_ABOUT, "About");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, ID_TRAY_QUIT, "Quit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageA(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static INT_PTR CALLBACK about_dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            char ver[32];
            snprintf(ver, sizeof(ver), "v%s", VERSION);
            SetDlgItemTextA(hwnd, IDC_ABOUT_NAME, "Game Mode Activator");
            SetDlgItemTextA(hwnd, IDC_ABOUT_LIC, "MIT License");
            SetDlgItemTextA(hwnd, IDC_ABOUT_VER, ver);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, LOWORD(wParam));
                return TRUE;
            }
            break;
    }
    return FALSE;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            char exe_path[MAX_PATH];
            GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
            char *sep = strrchr(exe_path, '\\');
            if (sep) {
                sep[1] = '\0';
                snprintf(marker_path, sizeof(marker_path), "%sgamemode_active.exe", exe_path);

                char whitelist_path[MAX_PATH * 2];
                snprintf(whitelist_path, sizeof(whitelist_path), "%sgamemode_whitelist.txt", exe_path);
                whitelist_load(whitelist_path);
            } else {
                strcpy(marker_path, "gamemode_active.exe");
                whitelist_load("gamemode_whitelist.txt");
            }

            ZeroMemory(&nid, sizeof(nid));
            nid.cbSize = sizeof(NOTIFYICONDATAA);
            nid.hWnd = hwnd;
            nid.uID = 1;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(IDI_SLEEP));
            strcpy(nid.szTip, "Game Mode Activator");
            Shell_NotifyIconA(NIM_ADD, &nid);

            SetTimer(hwnd, TIMER_POLL, poll_interval, NULL);

            GameInfo info;
            if (detect_game(&info)) {
                game_active = true;
                current_info = info;
                update_tray_icon(hwnd, true);
                spawn_marker();
                logger_log(&log, ">> GAME STARTED: %s (PID: %lu, Title: \"%s\", Flags: %s)",
                           info.processName, (unsigned long)info.pid,
                           info.windowTitle, flags_to_string(info.flags));
            }
            break;
        }

        case WM_TIMER: {
            if (wParam != TIMER_POLL) break;
            GameInfo info;
            bool detected = detect_game(&info);

            if (manual_override) {
                if (detected) {
                    game_active = true;
                    current_info = info;
                } else {
                    game_active = false;
                }
                break;
            }

            if (detected && !game_active) {
                game_active = true;
                current_info = info;
                update_tray_icon(hwnd, true);
                spawn_marker();
                logger_log(&log, ">> GAME STARTED: %s (PID: %lu, Title: \"%s\", Flags: %s)",
                           info.processName, (unsigned long)info.pid,
                           info.windowTitle, flags_to_string(info.flags));
            } else if (!detected && game_active) {
                game_active = false;
                update_tray_icon(hwnd, false);
                kill_marker();
                logger_log(&log, "<< GAME STOPPED: %s (PID: %lu)",
                           current_info.processName, (unsigned long)current_info.pid);
            } else if (detected && game_active && info.pid != current_info.pid) {
                kill_marker();
                logger_log(&log, "<< GAME STOPPED: %s (PID: %lu)",
                           current_info.processName, (unsigned long)current_info.pid);
                current_info = info;
                update_tray_icon(hwnd, true);
                spawn_marker();
                logger_log(&log, ">> GAME STARTED: %s (PID: %lu, Title: \"%s\", Flags: %s)",
                           info.processName, (unsigned long)info.pid,
                           info.windowTitle, flags_to_string(info.flags));
            }
            break;
        }

        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP)
                show_context_menu(hwnd);
            break;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_TRAY_TOGGLE: {
                    if (manual_override) {
                        manual_override = false;
                        if (!game_active) {
                            kill_marker();
                            update_tray_icon(hwnd, false);
                        }
                        logger_log(&log, "Manual override disabled");
                    } else {
                        manual_override = true;
                        if (!game_active) {
                            spawn_marker();
                            update_tray_icon(hwnd, true);
                        }
                        logger_log(&log, "Manual override enabled");
                    }
                    break;
                }
                case ID_TRAY_ABOUT:
                    DialogBoxA(hInst, MAKEINTRESOURCEA(IDD_ABOUT), hwnd, about_dlg_proc);
                    break;
                case ID_TRAY_QUIT:
                    DestroyWindow(hwnd);
                    break;
            }
            break;
        }

        case WM_DESTROY: {
            bool effective = game_active || manual_override;
            if (effective) {
                logger_log(&log, "<< Shutting down with game mode active");
            }
            kill_marker();
            KillTimer(hwnd, TIMER_POLL);
            Shell_NotifyIconA(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static bool parse_cmdline(LPSTR lpCmdLine) {
    bool once = false;
    char *p = lpCmdLine;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        if (strncmp(p, "--interval", 10) == 0) {
            p += 10;
            while (*p == ' ' || *p == '\t') p++;
            if (*p) {
                poll_interval = atoi(p) * 1000;
                if (poll_interval < 100) poll_interval = 100;
                while (*p && *p != ' ') p++;
            }
        } else if (strncmp(p, "--logfile", 9) == 0) {
            p += 9;
            while (*p == ' ' || *p == '\t') p++;
            if (*p) {
                int i = 0;
                while (*p && *p != ' ' && *p != '\t' && i < MAX_PATH - 1)
                    logfile_path[i++] = *p++;
                logfile_path[i] = '\0';
            }
        } else if (strcmp(p, "--once") == 0) {
            once = true;
            p += 6;
        } else if (strcmp(p, "-v") == 0 || strcmp(p, "--verbose") == 0) {
            verbose = true;
            p += (p[1] == '-' ? 9 : 2);
        } else if (strcmp(p, "--version") == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "rainmeter-gamemode-activator v%s", VERSION);
            MessageBoxA(NULL, buf, "Version", MB_OK);
            exit(0);
        } else if (strcmp(p, "--help") == 0) {
            MessageBoxA(NULL,
                "Usage: gamemode.exe [options]\n\n"
                "--interval N   Polling interval in seconds (default: 2)\n"
                "--logfile PATH Log file path (default: gamemode.log)\n"
                "--once         Check once and exit\n"
                "-v, --verbose  Show console window with verbose logs\n"
                "--version      Show version\n"
                "--help         Show this help",
                "Help", MB_OK);
            exit(0);
        } else {
            p++;
        }
    }
    return once;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)nCmdShow;
    hInst = hInstance;

    HANDLE hMutex = CreateMutexA(NULL, FALSE, "Local\\RainmeterGameModeActivator");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        if (verbose) {
            AllocConsole();
            printf("Another instance is already running.\n");
        }
        return 0;
    }

    bool once = parse_cmdline(lpCmdLine);

    if (!logger_init(&log, logfile_path)) {
        MessageBoxA(NULL, "Failed to open log file.", "Error", MB_ICONERROR);
        return 1;
    }

    if (verbose) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        SetConsoleTitleA("Game Mode Activator - Verbose");
        log.console = true;
    }

    logger_log(&log, "=== Game Mode Activator started ===");
    logger_log(&log, "Logging to: %s", logfile_path);
    logger_log(&log, "Interval: %dms", poll_interval);

    if (once) {
        GameInfo info;
        bool detected = detect_game(&info);
        if (detected) {
            char buf[1024];
            snprintf(buf, sizeof(buf),
                "Game active: %s (PID: %lu)\nTitle: %s\nFlags: %s",
                info.processName, (unsigned long)info.pid,
                info.windowTitle, flags_to_string(info.flags));
            MessageBoxA(NULL, buf, "Game Mode Activator", MB_OK);
            logger_log(&log, ">> GAME STARTED: %s (PID: %lu, Title: \"%s\", Flags: %s)",
                       info.processName, (unsigned long)info.pid,
                       info.windowTitle, flags_to_string(info.flags));
        } else {
            MessageBoxA(NULL, "No game detected.", "Game Mode Activator", MB_OK);
        }
        logger_log(&log, "=== Game Mode Activator stopped ===");
        logger_close(&log);
        return 0;
    }

    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_APP));
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = "GameModeActivatorClass";

    if (!RegisterClassExA(&wc)) {
        logger_log(&log, "Failed to register window class");
        logger_close(&log);
        return 1;
    }

    HWND hwnd = CreateWindowExA(0, "GameModeActivatorClass", "Game Mode Activator",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                 300, 200, NULL, NULL, hInstance, NULL);
    if (!hwnd) {
        logger_log(&log, "Failed to create window");
        logger_close(&log);
        return 1;
    }

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    logger_log(&log, "=== Game Mode Activator stopped ===");
    logger_close(&log);
    return (int)msg.wParam;
}
