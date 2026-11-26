// Thread names on Windows (host_thread.h has the others): SetThreadDescription, Windows 10 1607 on,
// looked up at run time so an older Windows just goes without.
#ifdef _WIN32
#include <windows.h>
#include "host_thread.h"

typedef HRESULT (WINAPI *SetDescriptionCall)(HANDLE, PCWSTR);

void host_thread_name(const char *name) {
  static SetDescriptionCall set;
  static int looked;
  if (!looked) {
    HMODULE kernel = GetModuleHandleA("kernelbase.dll");
    set = kernel ? (SetDescriptionCall)(void *)GetProcAddress(kernel, "SetThreadDescription") : 0;
    looked = 1;
  }
  if (!set) return;
  wchar_t wide[32];
  int count = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 32);
  if (count > 0) set(GetCurrentThread(), wide);
}
#endif
