/*
 * nvBrightness - nVidia Control Panel brightness at your fingertips
 *
 * Copyright © 2025 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "tray.h"

// Globals
static int8_t brightness = 75;
static bool enabled = true;
static bool use_alternate_keys = false;
static struct tray tray;

// Callbacks for Tray
static void AlternateKeysCallback(struct tray_menu* item)
{
	use_alternate_keys = !use_alternate_keys;
	OutputDebugStringA("Alternate Keys: ");
	OutputDebugStringA(use_alternate_keys ? "Enabled\n" : "Disabled\n");
	item->checked = !item->checked;
	tray_update(&tray);
}

static void PauseCallback(struct tray_menu* item)
{
	enabled = !enabled;
	item->checked = !item->checked;
	tray_update(&tray);
}

static void ExitCallback(struct tray_menu* item)
{
	(void)item;
	tray_exit();
}

// Keyboard procs
static void ChangeBrightness(bool bIncrease)
{
	static wchar_t icon_path[256];
	static wchar_t menu_txt[64];

	brightness += bIncrease ? +5 : -5;
	if (brightness < 0)
		brightness = 0;
	if (brightness > 100)
		brightness = 100;

	swprintf(icon_path, ARRAYSIZE(icon_path), L"C:\\Projects\\nvBrightness\\icons\\%02d.ico", brightness);
	swprintf(menu_txt, ARRAYSIZE(menu_txt), L"Brightness: %d%%", brightness);
	tray.icon = icon_path;
	tray.menu[0].text = menu_txt;
	tray_update(&tray);
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (enabled && nCode == HC_ACTION && lParam != 0) {
		KBDLLHOOKSTRUCT* kbhs = (KBDLLHOOKSTRUCT*)lParam;

		if (!use_alternate_keys) {
			// TODO: Check if Alt/Ctr/Shift are pressed and ignore then?
			if ((kbhs->vkCode == VK_SCROLL || kbhs->vkCode == VK_PAUSE) && (kbhs->flags == 0x0)) {
				ChangeBrightness((kbhs->vkCode == VK_PAUSE));
				return 1;
			}
		} else {
			// Alternate way, that uses the Internet navigation keys
			if ((kbhs->vkCode == VK_LEFT || kbhs->vkCode == VK_RIGHT) && ((kbhs->flags & 0xA1) == 0x21)) {
				ChangeBrightness((kbhs->vkCode == VK_RIGHT));
				return 1;
			}
		}
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	HHOOK hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(0), 0);
	static struct tray_menu menu[] = {
		{.text = L"Brightness: 75%" },
		{.text = L"Pause", .checked = 0, .cb = PauseCallback },
		{.text = L"Use Internet Keys", .checked = 0, .cb = AlternateKeysCallback },
		{.text = L"Exit", .cb = ExitCallback },
		{.text = NULL }
	};
	tray.icon = L"C:\\Projects\\nvBrightness\\icons\\75.ico";
	tray.menu = menu;

	if (tray_init(&tray) < 0) {
		OutputDebugStringA("Failed to create tray\n");
		return 1;
	}
	while (tray_loop(1) == 0);

	UnhookWindowsHookEx(hHook);

	return 0;
}
