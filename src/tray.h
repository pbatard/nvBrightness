/*
 * tray.h - Single header, C99 implementation of a system tray icon.
 * https://github.com/zserge/tray
 *
 * Copyright © 2017 Serge Zaitsev <zaitsev.serge@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <dbt.h>

#pragma comment(lib, "dwmapi.lib")

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum {
	AppMode_Default,
	AppMode_AllowDark,
	AppMode_ForceDark,
	AppMode_ForceLight,
	AppMode_Max
} PreferredAppMode;

struct tray_menu;

struct tray {
	HICON icon;
	struct tray_menu* menu;
};

struct tray_menu {
	const wchar_t* text;
	bool disabled;
	bool checked;

	void (*cb)(struct tray_menu*);
	void* context;

	struct tray_menu* submenu;
};

typedef bool (*hotkey_cb)(WPARAM, LPARAM);

static void tray_update(struct tray* tray);

#define WM_TRAY_CALLBACK_MESSAGE (WM_USER + 1)
#define ID_TRAY_FIRST 1000
#define ID_REFRESH_TIMER 1000

extern WNDCLASSEX wc;
extern NOTIFYICONDATA nid;
extern HWND hwnd;
extern HMENU hmenu;
extern const wchar_t* class_name;
extern hotkey_cb hkcb;

// Use GLOBAL_TRAY_INSTANCE *once* in one of the C/C++ sources
#define GLOBAL_TRAY_INSTANCE        \
WNDCLASSEX wc;                      \
NOTIFYICONDATA nid;                 \
HWND hwnd;                          \
HMENU hmenu = NULL;                 \
const wchar_t* class_name = NULL;   \
hotkey_cb hkcb;

static void CALLBACK _tray_device_timer(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	KillTimer(hWnd, ID_REFRESH_TIMER);
	SendMessage(hWnd, WM_HOTKEY, WM_DEVICECHANGE, 0);
}

static LRESULT CALLBACK _tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	static uint64_t last_refresh = 0;
	switch (msg) {
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_TRAY_CALLBACK_MESSAGE:
		if (lparam == WM_LBUTTONUP || lparam == WM_RBUTTONUP) {
			POINT p;
			GetCursorPos(&p);
			SetForegroundWindow(hwnd);
			WORD cmd = TrackPopupMenu(hmenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON |
				TPM_RETURNCMD | TPM_NONOTIFY,
				p.x, p.y, 0, hwnd, NULL);
			SendMessage(hwnd, WM_COMMAND, cmd, 0);
			return 0;
		}
		break;
	case WM_COMMAND:
		if (wparam >= ID_TRAY_FIRST) {
			MENUITEMINFO item = {
				.cbSize = sizeof(MENUITEMINFO), .fMask = MIIM_ID | MIIM_DATA,
			};
			if (GetMenuItemInfo(hmenu, (UINT)wparam, FALSE, &item)) {
				struct tray_menu* menu = (struct tray_menu*)item.dwItemData;
				if (menu != NULL && menu->cb != NULL) {
					menu->cb(menu);
				}
			}
			return 0;
		}
		break;
	case WM_HOTKEY:
		if (hkcb && hkcb(wparam, lparam))
			return 0;
		break;
	case WM_DEVICECHANGE:
		// WM_DEVICECHANGE + DBT_DEVNODES_CHANGED is a better reflection of display
		// changes compared to WM_DISPLAYCHANGE. For one thing WM_DISPLAYCHANGE is
		// *NOT* triggered if you remove the last active display from your machine.
		if (wparam == DBT_DEVNODES_CHANGED) {
			// However, we don't want to clobber the system with notifications, so
			// we time delay our notification by 1 second, to group everything.
			if (GetTickCount64() > last_refresh + 1000) {
				last_refresh = GetTickCount64();
				SetTimer(hwnd, ID_REFRESH_TIMER, 1000, _tray_device_timer);
			}
		}
		break;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

static HMENU _tray_menu(struct tray_menu* m, UINT* id) {
	HMENU hmenu = CreatePopupMenu();
	for (; m != NULL && m->text != NULL; m++, (*id)++) {
		if (wcscmp(m->text, L"-") == 0) {
			InsertMenu(hmenu, *id, MF_SEPARATOR, TRUE, L"");
		} else {
			MENUITEMINFO item;
			memset(&item, 0, sizeof(item));
			item.cbSize = sizeof(MENUITEMINFO);
			item.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE | MIIM_DATA;
			item.fType = 0;
			item.fState = 0;
			if (m->submenu != NULL) {
				item.fMask = item.fMask | MIIM_SUBMENU;
				item.hSubMenu = _tray_menu(m->submenu, id);
			}
			if (m->disabled) {
				item.fState |= MFS_DISABLED;
			}
			if (m->checked) {
				item.fState |= MFS_CHECKED;
			}
			item.wID = *id;
			item.dwTypeData = (LPWSTR)m->text;
			item.dwItemData = (ULONG_PTR)m;

			InsertMenuItem(hmenu, *id, TRUE, &item);
		}
	}
	return hmenu;
}

static void tray_enable_dark_mode() {
	HMODULE hUx = LoadLibraryA("uxtheme.dll");
	if (!hUx)
		return;

	typedef PreferredAppMode(WINAPI* SetPreferredAppMode_t)(PreferredAppMode);
	SetPreferredAppMode_t SetPreferredAppMode =
		(SetPreferredAppMode_t)GetProcAddress(hUx, MAKEINTRESOURCEA(135));
	if (SetPreferredAppMode)
		SetPreferredAppMode(AppMode_AllowDark);

#if 0
	BOOL dark_mode = FALSE;
	DWORD data = 0, size = sizeof(data);
	if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &data, &size) == ERROR_SUCCESS)
		dark_mode = (data == 0);
	DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));
#endif
}

// Using a GUID ensures that Windows recognizes the app even if it changes version or has its .exe moved
static int tray_init(struct tray* tray, const wchar_t* name, const GUID guid, hotkey_cb cb) {
	if (!tray || !name) return -1;

	class_name = name;
	memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = _tray_wnd_proc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = name;
	if (!RegisterClassEx(&wc)) {
		return -1;
	}

	hkcb = cb;
	hwnd = CreateWindowEx(0, class_name, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	if (hwnd == NULL) {
		return -1;
	}
	tray_enable_dark_mode();
	UpdateWindow(hwnd);

	memset(&nid, 0, sizeof(nid));
	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hwnd;
	nid.guidItem = guid;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_GUID;
	nid.uCallbackMessage = WM_TRAY_CALLBACK_MESSAGE;
	Shell_NotifyIcon(NIM_ADD, &nid);

	tray_update(tray);
	return 0;
}

// Register a hotkey through RegisterHotKey().
// Should be called *after* and *if* a callback was provided to tray_init().
static bool tray_register_hotkey(int id, UINT modifiers, UINT vk) {
	if (!hwnd || !hkcb)
		return false;
	return RegisterHotKey(hwnd, id, modifiers, vk);
}

static void tray_unregister_hotkkey(int id) {
	if (hwnd)
		UnregisterHotKey(hwnd, id);
}

static void tray_simulate_hottkey(int id) {
	if (hwnd)
		SendMessage(hwnd, WM_HOTKEY, (WPARAM)id, NULL);
}

static int tray_loop(int blocking) {
	MSG msg;
	if (blocking) {
		GetMessage(&msg, hwnd, 0, 0);
	} else {
		PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE);
	}
	if (msg.message == WM_QUIT) {
		return -1;
	}
	TranslateMessage(&msg);
	DispatchMessage(&msg);
	return 0;
}

static void tray_update(struct tray* tray) {
	HMENU prevmenu = hmenu;
	UINT id = ID_TRAY_FIRST;
	hmenu = _tray_menu(tray->menu, &id);
	SendMessage(hwnd, WM_INITMENUPOPUP, (WPARAM)hmenu, 0);
	if (nid.hIcon) {
		DestroyIcon(nid.hIcon);
	}
	nid.hIcon = tray->icon;
	Shell_NotifyIcon(NIM_MODIFY, &nid);

	if (prevmenu != NULL) {
		DestroyMenu(prevmenu);
	}
}

static void tray_exit() {
	Shell_NotifyIcon(NIM_DELETE, &nid);
	if (nid.hIcon != 0) {
		DestroyIcon(nid.hIcon);
	}
	if (hmenu != 0) {
		DestroyMenu(hmenu);
	}
	PostQuitMessage(0);
	UnregisterClass(class_name, GetModuleHandle(NULL));
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
