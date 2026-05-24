#include <windows.h>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hInst; (void)hPrev; (void)lpCmd; (void)nShow;
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (ev) WaitForSingleObject(ev, INFINITE);
    return 0;
}
