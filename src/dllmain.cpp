// =============================================================================
//  The Guild 2: Renaissance - mouse cursor lag fix            (proxy dinput8.dll)
//  v1.0.0
//
//  Problem
//  -------
//  Moving the mouse - especially fast, or with a high-polling-rate mouse - drops
//  the frame rate to a slideshow. Standing still is fine. The community workaround
//  was to delete the cursor .tga files (which removes the lag but also the cursor).
//
//  Cause
//  -----
//  Windows sends WM_SETCURSOR to a window on every mouse move inside it, so the app
//  can set the cursor shape. The Guild 2's WM_SETCURSOR handler does an EXPENSIVE
//  cursor redraw every single time. A gaming mouse generates hundreds of moves per
//  second -> hundreds of full cursor redraws per second -> the frame rate collapses.
//  (Deleting the cursor .tga made that handler a no-op, which is why it "fixed" it.)
//
//  Fix
//  ---
//  The game imports dinput8.dll, so this proxy is loaded at startup. It forwards
//  every dinput8 call to the real system DLL (input keeps working) and subclasses
//  the game window to THROTTLE WM_SETCURSOR: at most one is passed to the game every
//  kThrottleMs milliseconds; the rest are answered with TRUE (keep the current
//  cursor). The game's heavy handler then runs ~66x/second instead of hundreds, so
//  the lag is gone - while the game still draws its OWN cursor from its OWN .tga,
//  with all of its context shapes (normal / allowed / forbidden / drag). This DLL
//  never touches the cursor image and embeds nothing.
//
//  Install: drop next to GuildII.exe as dinput8.dll, keep the cursor .tga files in
//  place, run the game. Nothing to configure.
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

// Minimum interval between WM_SETCURSOR messages forwarded to the game (~66/s).
// Lower = the cursor's context shape updates more often but the game works harder;
// 15 ms is imperceptible and comfortably kills the lag.
static const DWORD kThrottleMs = 15;

// -----------------------------------------------------------------------------
//  1) Transparent forwarding to the real system dinput8.dll
// -----------------------------------------------------------------------------
static HMODULE          g_real    = nullptr;
static CRITICAL_SECTION g_lock;
static bool             g_locked  = false;

typedef HRESULT (WINAPI *PFN_DirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
typedef HRESULT (WINAPI *PFN_DllCanUnloadNow)(void);
typedef HRESULT (WINAPI *PFN_DllGetClassObject)(REFCLSID, REFIID, LPVOID*);
typedef HRESULT (WINAPI *PFN_DllRegisterServer)(void);
typedef HRESULT (WINAPI *PFN_DllUnregisterServer)(void);
typedef void*   (WINAPI *PFN_GetdfDIJoystick)(void);

static PFN_DirectInput8Create  pDirectInput8Create  = nullptr;
static PFN_DllCanUnloadNow     pDllCanUnloadNow     = nullptr;
static PFN_DllGetClassObject   pDllGetClassObject   = nullptr;
static PFN_DllRegisterServer   pDllRegisterServer   = nullptr;
static PFN_DllUnregisterServer pDllUnregisterServer = nullptr;
static PFN_GetdfDIJoystick     pGetdfDIJoystick     = nullptr;

// Lazily load the real dinput8.dll and resolve its exports. Not done in DllMain
// (loader lock); the first forwarded call triggers it.
static void EnsureRealLoaded()
{
    if (g_real) return;
    if (g_locked) EnterCriticalSection(&g_lock);
    if (!g_real)
    {
        wchar_t path[MAX_PATH];
        // A 32-bit process is redirected to SysWOW64 here -> the 32-bit dinput8.dll.
        UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (n && n + 16 < MAX_PATH)
        {
            lstrcatW(path, L"\\dinput8.dll");
            g_real = LoadLibraryW(path);
        }
        if (g_real)
        {
            pDirectInput8Create  = (PFN_DirectInput8Create)  GetProcAddress(g_real, "DirectInput8Create");
            pDllCanUnloadNow     = (PFN_DllCanUnloadNow)     GetProcAddress(g_real, "DllCanUnloadNow");
            pDllGetClassObject   = (PFN_DllGetClassObject)   GetProcAddress(g_real, "DllGetClassObject");
            pDllRegisterServer   = (PFN_DllRegisterServer)   GetProcAddress(g_real, "DllRegisterServer");
            pDllUnregisterServer = (PFN_DllUnregisterServer) GetProcAddress(g_real, "DllUnregisterServer");
            pGetdfDIJoystick     = (PFN_GetdfDIJoystick)     GetProcAddress(g_real, "GetdfDIJoystick");
        }
    }
    if (g_locked) LeaveCriticalSection(&g_lock);
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    EnsureRealLoaded();
    return pDirectInput8Create ? pDirectInput8Create(hinst, version, riid, out, outer) : E_FAIL;
}
extern "C" HRESULT WINAPI DllCanUnloadNow(void)
{
    EnsureRealLoaded();
    return pDllCanUnloadNow ? pDllCanUnloadNow() : S_FALSE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out)
{
    EnsureRealLoaded();
    return pDllGetClassObject ? pDllGetClassObject(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI DllRegisterServer(void)
{
    EnsureRealLoaded();
    return pDllRegisterServer ? pDllRegisterServer() : E_FAIL;
}
extern "C" HRESULT WINAPI DllUnregisterServer(void)
{
    EnsureRealLoaded();
    return pDllUnregisterServer ? pDllUnregisterServer() : E_FAIL;
}
extern "C" void* WINAPI GetdfDIJoystick(void)
{
    EnsureRealLoaded();
    return pGetdfDIJoystick ? pGetdfDIJoystick() : nullptr;
}

// -----------------------------------------------------------------------------
//  2) The fix: subclass the game window and throttle WM_SETCURSOR
// -----------------------------------------------------------------------------
static WNDPROC g_origProc = nullptr;
static HWND    g_window   = nullptr;
static bool    g_unicode  = true;
static DWORD   g_lastPass = 0;

static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Windows sends WM_SETCURSOR on every mouse move in the client area. Letting the
    // game handle each one triggers its expensive cursor redraw. We let one through
    // per kThrottleMs and answer the rest ourselves (TRUE = cursor already set), so
    // the game keeps its current cursor and skips the heavy work.
    if (msg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT)
    {
        DWORD now = GetTickCount();
        if (now - g_lastPass < kThrottleMs)
            return TRUE;
        g_lastPass = now;
    }
    return g_unicode ? CallWindowProcW(g_origProc, hWnd, msg, wParam, lParam)
                     : CallWindowProcA(g_origProc, hWnd, msg, wParam, lParam);
}

// Pick the process's main top-level window (skip splash/helper windows).
static BOOL CALLBACK FindMainWindow(HWND hWnd, LPARAM out)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid != GetCurrentProcessId())          return TRUE;
    if (!IsWindowVisible(hWnd))                return TRUE;
    if (GetWindow(hWnd, GW_OWNER) != nullptr)  return TRUE;
    RECT rc; GetWindowRect(hWnd, &rc);
    if ((rc.right - rc.left) < 200 || (rc.bottom - rc.top) < 200) return TRUE;
    *reinterpret_cast<HWND*>(out) = hWnd;
    return FALSE;
}

// The window does not exist yet at load time (and may change splash -> main), so poll
// for it and (re)subclass whichever real top-level window is current.
static DWORD WINAPI InstallThread(LPVOID)
{
    for (int i = 0; i < 2400; ++i)            // ~120 s budget
    {
        HWND hWnd = nullptr;
        EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&hWnd));
        if (hWnd && hWnd != g_window)
        {
            g_unicode = (IsWindowUnicode(hWnd) != FALSE);
            WNDPROC prev = g_unicode
                ? (WNDPROC)SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)SubclassProc)
                : (WNDPROC)SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (LONG_PTR)SubclassProc);
            if (prev && prev != SubclassProc)
            {
                g_origProc = prev;
                g_window   = hWnd;
            }
        }
        Sleep(50);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hInst);
        InitializeCriticalSection(&g_lock);
        g_locked = true;
        CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
