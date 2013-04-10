// This was written using the sdk example as base and the virtualbox api
// reference. I did not look at real examples(eg: VBoxHeadless.Less) so it may
// not be following the best practices.
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <windows.h>

#include "api.h"

#define SAFE_RELEASE(x) \
    if (x) { \
        x->Release(); \
        x = NULL; \
    }

// main interface to virtualbox api
static IVirtualBox *virtualbox = NULL;
// running vm
IMachine *machine = NULL;
// vm name
const wchar_t *name;

void VMStart()
{
  HRESULT rc;
  IProgress *progress;
  ISession *session;
  BSTR stype = SysAllocString(L"headless");

  rc = CoCreateInstance(CLSID_Session, NULL, CLSCTX_INPROC_SERVER,
      IID_ISession, (void**)&session);
  if (!SUCCEEDED(rc)) {
    ShowError(L"Failed to create session instance for '%s'. rc = 0x%x", name, rc);
    return;
  }

  rc = machine->LaunchVMProcess(session, stype, NULL, &progress);
  if (!SUCCEEDED(rc))
    ShowError(L"Failed to start '%s'. rc = 0x%x", name, rc);

  progress->WaitForCompletion(-1);
  session->UnlockMachine();
  SysFreeString(stype);
  SAFE_RELEASE(progress);
  SAFE_RELEASE(session);
}

void VMSaveState()
{
  HRESULT rc;
  IProgress *progress;
  ISession *session;
  IConsole *console;

  rc = CoCreateInstance(CLSID_Session, NULL, CLSCTX_INPROC_SERVER,
      IID_ISession, (void**)&session);
  if (!SUCCEEDED(rc)) {
    ShowError(L"Failed to create session instance for '%s'. rc = 0x%x", name, rc);
    return;
  }
  
  rc = machine->LockMachine(session, LockType_Shared);
  if (!SUCCEEDED(rc)) {
    ShowError(L"Failed to lock '%s'. rc = 0x%x", name, rc);
    return;
  }

  session->get_Console(&console);
  rc = console->SaveState(&progress);
  if (FAILED(rc)) {
    ShowError(L"Failed to save '%s' state. rc = 0x%x", name, rc);
    return;
  }

  progress->WaitForCompletion(-1);
  session->UnlockMachine();
  SAFE_RELEASE(progress);
  SAFE_RELEASE(console);
  SAFE_RELEASE(session);
}

void VMAcpiShutdown()
{
  HRESULT rc;
  ISession *session;
  IConsole *console;

  rc = CoCreateInstance(CLSID_Session, NULL, CLSCTX_INPROC_SERVER,
      IID_ISession, (void**)&session);
  if (!SUCCEEDED(rc)) {
    ShowError(L"Failed to create session instance for '%s'. rc = 0x%x", name, rc);
    return;
  }

  rc = machine->LockMachine(session, LockType_Shared);
  if (!SUCCEEDED(rc)) {
    ShowError(L"Failed to lock '%s'. rc = 0x%x", name, rc);
    return;
  }

  session->get_Console(&console);
  rc = console->PowerButton();
  if (FAILED(rc))
    ShowError(L"Failed to press '%s' power button. rc = 0x%x", name, rc);

  session->UnlockMachine();
  SAFE_RELEASE(console);
  SAFE_RELEASE(session);
}

MachineState VMGetState()
{
  MachineState state;
  machine->get_State(&state);
  return state;
}

int InitVirtualbox(const wchar_t *n)
{
  HRESULT rc;
  BSTR guid;
  name = n;
  BSTR machineName = SysAllocString(name);

  // initialize the COM subsystem.
  CoInitialize(NULL);

  // instantiate the VirtualBox root object.
  CoCreateInstance(CLSID_VirtualBox, NULL, CLSCTX_LOCAL_SERVER,
      IID_IVirtualBox, (void**)&virtualbox);

  // try to find the machine
  rc = virtualbox->FindMachine(machineName, &machine);

  if (FAILED(rc)) {
    ShowError(L"Could not find virtual machine named '%s'", name);
    return 0;
  } else {
    // get the machine uuid
    rc = machine->get_Id(&guid);
    if (!SUCCEEDED(rc)) {
      ShowError(L"Failed to get uuid for '%s'. rc = 0x%x", name, rc);
      return 0;
    }

    // if the vm is saved, start it now
    if (VMGetState() == MachineState_Saved)
      VMStart();

    SysFreeString(machineName);
    return 1;
  }
}

void FreeVirtualbox()
{
  virtualbox->Release();
  CoUninitialize();
}

/* Utility functions */

int Ask(const wchar_t * format, ...)
{
  int res;
  char result[2048];
  wchar_t msg[1024];
  va_list args;
  va_start(args, format);
  vswprintf(msg, format, args);
  va_end(args);
  wsprintf(result, "%S", msg);
  res = MessageBox(NULL, result, "Confirm", MB_YESNO | MB_ICONQUESTION);
  if (res == IDYES)
    return 1;
  return 0;
}

void ShowError(const wchar_t * format, ...)
{
  char result[2048];
  wchar_t msg[1024];
  va_list args;
  va_start(args, format);
  vswprintf(msg, format, args);
  va_end(args);
  // convert to char*(couldn't get mingw compile with unicode)
  wsprintf(result, "%S", msg);

  MessageBox(NULL, result, "Error", MB_OK | MB_ICONERROR);
}

void ShowInfo(const wchar_t *format, ...)
{
  char result[2048];
  wchar_t msg[1024];
  va_list args;
  va_start(args, format);
  vswprintf(msg, format, args);
  va_end(args);
  // convert to char*(couldn't get mingw compile with unicode)
  wsprintf(result, "%S", msg);

  MessageBox(NULL, result, "Info", MB_OK | MB_ICONINFORMATION);
}

