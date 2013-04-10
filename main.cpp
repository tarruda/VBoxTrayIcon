#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <tchar.h>
#include <shellapi.h>

#include "resources/resources.h"

#include "api.h"


#define WM_TRAYEVENT (WM_USER + 1)
#define WM_TRAY_STARTVM 1
#define WM_TRAY_ACPISHUTDOWN 2
#define WM_TRAY_SAVESTATE 3
#define WM_TRAY_EXIT 10

static UINT WM_EXPLORERCRASH = 0;

static TCHAR wclass[] = _T("vboxtrayicon");
static char title[100];

static NOTIFYICONDATA ndata;

// the user can optionally specify an icon on the command line
static char *iconPath;

// Tray icon context menu
static HMENU runningMenu;
static HMENU stoppedMenu;

static wchar_t *vmname;
static char *vmnameAscii;
static char tooltip[64];

void InitMenus()
{
  runningMenu = CreatePopupMenu();
  stoppedMenu = CreatePopupMenu();
  AppendMenu(runningMenu, MF_STRING, WM_TRAY_SAVESTATE, "Save VM state");
  AppendMenu(runningMenu, MF_STRING, WM_TRAY_ACPISHUTDOWN, "ACPI shutdown");
  AppendMenu(runningMenu, MF_SEPARATOR, NULL, NULL);
  AppendMenu(runningMenu, MF_STRING, WM_TRAY_EXIT, "Exit");
  AppendMenu(stoppedMenu, MF_STRING, WM_TRAY_STARTVM, "Start VM");
  AppendMenu(stoppedMenu, MF_SEPARATOR, NULL, NULL);
  AppendMenu(stoppedMenu, MF_STRING, WM_TRAY_EXIT, "Exit");
}

void UpdateTray(MachineState state)
{
  switch (state) {
    case MachineState_PoweredOff:
      sprintf(tooltip, "%s: Powered off", vmnameAscii); break;
    case MachineState_Running:
      sprintf(tooltip, "%s: Running", vmnameAscii); break;
    case MachineState_Saved:
      sprintf(tooltip, "%s: Saved", vmnameAscii); break;
    default:
      sprintf(tooltip, "%s", vmnameAscii); break;
  };
  strcpy(ndata.szTip, TEXT(tooltip));
  Shell_NotifyIcon(NIM_MODIFY, &ndata);
}

LRESULT CALLBACK HandleTrayEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  HMENU menu;
  UINT clicked;
  POINT point;
  MachineState state;
  // these flags will disable messages about the context menu
  // and focus on returning the clicked item id
  UINT flags = TPM_RETURNCMD | TPM_NONOTIFY;

  switch(lParam)
  {
    case WM_RBUTTONDOWN:
      state = VMGetState();
      GetCursorPos(&point);
      SetForegroundWindow(hWnd); 
      if (state == MachineState_Running)
        menu = runningMenu;
      else
        menu = stoppedMenu;
      clicked = TrackPopupMenu(menu, flags, point.x, point.y, 0, hWnd, NULL);
      switch (clicked)
      {
        case WM_TRAY_STARTVM:
          VMStart();
          UpdateTray(MachineState_Running);
          break;
        case WM_TRAY_SAVESTATE:
          VMSaveState();
          UpdateTray(MachineState_Saved);
          break;
        case WM_TRAY_ACPISHUTDOWN:
          VMAcpiShutdown();
          UpdateTray(MachineState_PoweredOff);
          break;
        case WM_TRAY_EXIT:
          if (VMGetState() == MachineState_Running) {
            if (Ask(L"VM is still running.\nDo you want to save state and exit?")) {
              VMSaveState();
              PostQuitMessage(0);
            }
          } else {
            PostQuitMessage(0);
          }
          break;
      };
      break;
  };
  return DefWindowProc(hWnd, msg, wParam, lParam);
}


// The following declarations will provide functions that allow us to block
// theshutdown process while we cleanly exit the VM. Snippet stolen from:
// https://github.com/toptensoftware/VBoxHeadlessTray
typedef BOOL (WINAPI* SHUTDOWNBLOCKCREATE)(HWND hWnd, LPCWSTR pwszReason);

typedef BOOL (WINAPI* SHUTDOWNBLOCKDESTROY)(HWND hWnd);

BOOL ShutdownBlockCreate(HWND hWnd, LPCWSTR pwszReason)
{
  SHUTDOWNBLOCKCREATE pfn = (SHUTDOWNBLOCKCREATE)GetProcAddress(GetModuleHandle("user32.dll"), "ShutdownBlockReasonCreate");
  if (pfn)
    return pfn(hWnd, pwszReason);
  else
    return FALSE;
}

BOOL ShutdownBlockDestroy(HWND hWnd)
{
  SHUTDOWNBLOCKDESTROY pfn = (SHUTDOWNBLOCKDESTROY)GetProcAddress(GetModuleHandle("user32.dll"), "ShutdownBlockReasonDestroy");
  if (pfn)
    return pfn(hWnd);
  else
    return FALSE;
}

LRESULT CALLBACK HandleEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_EXPLORERCRASH) {
    Shell_NotifyIcon(NIM_ADD, &ndata);
    return 0;
  }

  switch (msg)
  {
    case WM_TRAYEVENT:
      return HandleTrayEvent(hWnd, msg, wParam, lParam);
    case WM_CREATE:
      InitMenus();
      return 0;
    case WM_QUERYENDSESSION:
      if (VMGetState() == MachineState_Running)
        ShutdownBlockCreate(hWnd, L"Shutting down virtual machine");
      return TRUE;
    case WM_ENDSESSION:
      if (wParam) {
        if (VMGetState() == MachineState_Running)
          VMSaveState();
        FreeVirtualbox();
      }
      ShutdownBlockDestroy(hWnd);
      return 0;
    default: return DefWindowProc(hWnd, msg, wParam, lParam);
  };
}

int ParseOptions()
{
  size_t size;

  if (__argc == 1)
    return 0;

  vmnameAscii = *(__argv + 1);
  // convert the vm name to wchar_t
  size = strlen(vmnameAscii) + 1;
  vmname = new wchar_t[size];
  mbstowcs(vmname, vmnameAscii, size);

  if (__argc > 1)
    iconPath = *(__argv + 2);

  return 1;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPSTR lpCmdLine, int nCmdShow)
{
  WNDCLASSEX wcex;
  MSG msg;
  HWND hWnd;

  if (ParseOptions() == 0) {
    ShowError(L"Need to provide the VM name as first argument");
    return 1;
  }

  // initialize the virtualbox api
  if (!InitVirtualbox(vmname))
    return 1;

  // This will make the process exit before VBoxSVC, which will give
  // the chance to cleanly shutdown the VM
  // Stolen from: https://github.com/toptensoftware/VBoxHeadlessTray
  SetProcessShutdownParameters(0x400, 0);

  // every application that wants to use a message loop needs to
  // initialize/register this structure
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = HandleEvent;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  if (iconPath == NULL)
    wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
  else
    wcex.hIcon = (HICON) LoadImage(NULL, iconPath, IMAGE_ICON, 0, 0,
        LR_LOADFROMFILE| LR_DEFAULTSIZE| LR_SHARED);
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)COLOR_APPWORKSPACE;
  wcex.lpszMenuName = NULL;
  wcex.lpszClassName = wclass;
  wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);
  RegisterClassEx(&wcex);

  sprintf(title, "VirtualBox Tray Icon: %s", vmnameAscii);
  // without creating a window no message queue will exist, so this is
  // needed even if the window will be hidden most(or all) of the time
  hWnd = CreateWindow(wclass, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
      CW_USEDEFAULT, 500, 100, NULL, NULL, hInstance, NULL);

  // fill structure that holds data about the tray icon
  ndata.cbSize = sizeof(NOTIFYICONDATA);
  ndata.hWnd = hWnd;
  ndata.uCallbackMessage = WM_TRAYEVENT; // custom message to identify tray events 
  if (iconPath == NULL)
    ndata.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_VBOXICON));
  else
    ndata.hIcon = (HICON) LoadImage(NULL, iconPath, IMAGE_ICON, 16, 16,
        LR_LOADFROMFILE| LR_SHARED);
  ndata.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  Shell_NotifyIcon(NIM_ADD, &ndata);
  UpdateTray(VMGetState());

  // listen for the "explorer crash event" so we can add the icon
  // again
  WM_EXPLORERCRASH = RegisterWindowMessageA("TaskbarCreated");

  while (GetMessage(&msg, NULL, 0, 0))
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  // remove tray icon
  Shell_NotifyIcon(NIM_DELETE, &ndata);
  // cleanup virtualbox api resources
  FreeVirtualbox();
  return (int) msg.wParam;
}

