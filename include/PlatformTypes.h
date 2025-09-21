#pragma once

#ifdef _WIN32

#define NOMINMAX
#include <Windows.h>
#undef RGB //Windows leaks this macro and it conflicts with osmanip

using processId = DWORD;
using windowHandle = HWND;
using keycode = WORD;

#else

#include <sys/types.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

using processId = pid_t;
using windowHandle = Window;
using keycode = KeySym;

#endif
