// Moves the mouse cursor to a random position on the screen at a fixed interval.
// Usage: AutoMouseMover.exe [interval in milliseconds]
// Each start registers the program, with its arguments, to run at sign-in.
// Running it again while an instance is active stops that instance and keeps the registration.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEFAULT_INTERVAL_MS 50000
#define MINIMUM_INTERVAL_MS 1000
#define MAXIMUM_INTERVAL_MS 3600000
#define IMAGE_PATH_CAPACITY 1024
#define COMMAND_CAPACITY 1536

static const WCHAR ApplicationName[] = L"AutoMouseMover";
static const WCHAR WindowClassName[] = L"AutoMouseMoverHiddenWindow";

static ULONG64 RandomState;

static void SeedRandom(void)
{
    LARGE_INTEGER counter;

    QueryPerformanceCounter(&counter);
    RandomState = (ULONG64)counter.QuadPart ^ ((ULONG64)GetCurrentProcessId() << 32);

    // xorshift never leaves the all-zero state, so it must not start there.
    if (RandomState == 0) RandomState = 1;
}

// xorshift64*: the position only has to look random, not be unpredictable.
static ULONG NextRandom(void)
{
    RandomState ^= RandomState >> 12;
    RandomState ^= RandomState << 25;
    RandomState ^= RandomState >> 27;
    return (ULONG)((RandomState * 0x2545F4914F6CDD1DULL) >> 32);
}

static void MoveCursorToRandomPosition(void)
{
    INPUT input = { 0 };

    // Absolute coordinates run from 0 to 65535 across the whole virtual desktop, so every monitor can be reached
    // and the result does not depend on resolution or DPI scaling.
    input.type = INPUT_MOUSE;
    input.mi.dx = (LONG)(NextRandom() >> 16);
    input.mi.dy = (LONG)(NextRandom() >> 16);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    // Injected input is handled like a real mouse movement, so it also resets the system idle timer.
    SendInput(1, &input, sizeof(input));
}

static const WCHAR* GetArguments(void)
{
    const WCHAR* cursor = GetCommandLineW();

    // Skip the program path, which may be quoted.
    if (*cursor == L'"')
    {
        cursor++;
        while (*cursor && *cursor != L'"') cursor++;
        if (*cursor) cursor++;
    }
    else
    {
        while (*cursor > L' ') cursor++;
    }
    while (*cursor == L' ' || *cursor == L'\t') cursor++;

    return cursor;
}

static void RegisterStartup(const WCHAR* arguments)
{
    WCHAR command[COMMAND_CAPACITY];
    DWORD length = GetModuleFileNameW(NULL, command + 1, IMAGE_PATH_CAPACITY);

    if (length == 0 || length >= IMAGE_PATH_CAPACITY) return;

    command[0] = L'"';
    length++;
    command[length++] = L'"';
    if (*arguments)
    {
        command[length++] = L' ';
        while (*arguments && length < COMMAND_CAPACITY - 1) command[length++] = *arguments++;
    }
    command[length++] = L'\0';

    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", ApplicationName, REG_SZ, command, length * sizeof(WCHAR));
}

static DWORD ReadInterval(const WCHAR* cursor)
{
    DWORD interval = 0;

    while (*cursor >= L'0' && *cursor <= L'9' && interval <= MAXIMUM_INTERVAL_MS) interval = interval * 10 + (DWORD)(*cursor++ - L'0');

    if (interval == 0) return DEFAULT_INTERVAL_MS;
    if (interval < MINIMUM_INTERVAL_MS) return MINIMUM_INTERVAL_MS;
    if (interval > MAXIMUM_INTERVAL_MS) return MAXIMUM_INTERVAL_MS;
    return interval;
}

// Runs on a thread pool thread when another instance signals the exit event.
static VOID CALLBACK OnExitRequested(PVOID parameter, BOOLEAN timedOut)
{
    PostMessageW((HWND)parameter, WM_CLOSE, 0, 0);
}

static LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_TIMER)
    {
        MoveCursorToRandomPosition();
        return 0;
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int show)
{
    // Unprefixed names live in the session namespace, the same as a "Local\" prefix.
    HANDLE exitEvent = CreateEventW(NULL, FALSE, FALSE, ApplicationName);
    HANDLE exitWait = NULL;
    WNDCLASSW windowClass = { 0 };
    const WCHAR* arguments;
    DWORD interval;
    HWND window;
    MSG message;

    if (exitEvent == NULL) return 1;

    // The event already exists when another instance is running: signal it to stop and leave.
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        SetEvent(exitEvent);
        CloseHandle(exitEvent);
        return 0;
    }

    arguments = GetArguments();
    RegisterStartup(arguments);
    interval = ReadInterval(arguments);
    SeedRandom();

    windowClass.lpfnWndProc = MainWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = WindowClassName;
    if (!RegisterClassW(&windowClass)) return 1;

    // The window is never shown; it only receives timer ticks and the close request.
    window = CreateWindowExW(0, WindowClassName, ApplicationName, 0, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (window == NULL) return 1;

    if (!RegisterWaitForSingleObject(&exitWait, exitEvent, OnExitRequested, window, INFINITE, WT_EXECUTEONLYONCE)) return 1;
    if (SetTimer(window, 1, interval, NULL) == 0) return 1;

    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        DispatchMessageW(&message);
    }

    // Wait for a callback that may still be running before the event handle goes away.
    UnregisterWaitEx(exitWait, INVALID_HANDLE_VALUE);
    CloseHandle(exitEvent);
    return 0;
}
