#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#elif __APPLE__
#include <cstdlib>
#else
#include <cstdlib>
#endif

void openURL(const char* url) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif __APPLE__
    char command[256];
    snprintf(command, sizeof(command), "open %s", url);
    system(command);
#else
    char command[256];
    snprintf(command, sizeof(command), "xdg-open %s", url);
    system(command);
#endif
}