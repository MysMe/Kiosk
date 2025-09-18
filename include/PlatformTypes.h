#pragma once

#ifdef _WIN32
#include <Windows.h>
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
