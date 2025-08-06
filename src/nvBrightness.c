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

#include "resource.h"
#include "tray.h"

// nVidia Color data definitions
#define NV_COLOR_RED                0
#define NV_COLOR_GREEN              1
#define NV_COLOR_BLUE               2
#define NV_COLOR_MAX                3

#define NV_ATTR_BRIGHTNESS          0
#define NV_ATTR_CONTRAST            1
#define NV_ATTR_GAMMA               2
#define NV_ATTR_MAX                 3

#define NV_COLOR_REGISTRY_INDEX		3538946

// Logging
static char log_msg[256];
#define log(...) do { sprintf_s(log_msg, sizeof(log_msg), "nvBrightness: " __VA_ARGS__); OutputDebugStringA(log_msg); } while(0)

// Globals
static int32_t brightness = 50;
static int32_t nvColorSettings[NV_ATTR_MAX][NV_COLOR_MAX] = { 0 };
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

// Registry procs
static int ReadColorDataFromRegistry(void)
{
	int ret = -1;
	wchar_t SubKeyName[128], ColorKeyName[32];
	HKEY hDevices = NULL, hColorData = NULL;
	DWORD i, j, size, type, cSubKeys = 0, cbMaxSubKey = 0, cbName = 0;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\NVIDIA Corporation\\Global\\NVTweak\\Devices", 0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE, &hDevices) != ERROR_SUCCESS) {
		log("Could not open NVTweak\\Devices\n");
		goto out;
	}

	if (RegQueryInfoKey(hDevices, NULL, NULL, NULL, &cSubKeys, &cbMaxSubKey, NULL, NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS || cSubKeys == 0) {
		log("Could not find NVTweak\\Devices subkeys\n");
		goto out;
	}

	for (i = 0; i < cSubKeys; i++) {
		cbName = ARRAYSIZE(SubKeyName);
		if (RegEnumKeyEx(hDevices, i, SubKeyName, &cbName, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
			continue;
		wcsncat_s(SubKeyName, ARRAYSIZE(SubKeyName), L"\\Color", _TRUNCATE);
		if (RegOpenKeyEx(hDevices, SubKeyName, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hColorData) == ERROR_SUCCESS) {
			for (j = 0; j < 9; j++) {
				swprintf(ColorKeyName, ARRAYSIZE(ColorKeyName), L"%d", NV_COLOR_REGISTRY_INDEX + j);
				size = sizeof(DWORD);
				RegQueryValueEx(hColorData, ColorKeyName, 0, &type, (LPBYTE)&nvColorSettings[j / 3][j % 3], &size);
			}
			RegCloseKey(hColorData);
		}
	}

	// Sanity/defaults check
	for (j = 0; j < 9; j++) {
		if (nvColorSettings[j / 3][j % 3] < 80 || nvColorSettings[j / 3][j % 3] > 120)
			nvColorSettings[j / 3][j % 3] = 100;
	}

	// Compute our own brightness percentage
	brightness = nvColorSettings[NV_ATTR_BRIGHTNESS][NV_COLOR_RED] + nvColorSettings[NV_ATTR_BRIGHTNESS][NV_COLOR_GREEN] + nvColorSettings[NV_ATTR_BRIGHTNESS][NV_COLOR_BLUE];
	brightness = (brightness / 3 - 80) * 5;
	if (brightness < 0)
		brightness = 0;
	if (brightness > 100)
		brightness = 100;
	ret = 0;

out:
	if (hDevices != NULL)
		RegCloseKey(hDevices);

	return ret;
}

static int WriteColorDataToRegistry(void)
{
	int ret = -1;
	wchar_t SubKeyName[128], ColorKeyName[32];
	HKEY hDevices = NULL, hColorData = NULL;
	DWORD i, j, size, type, val, cSubKeys = 0, cbMaxSubKey = 0, cbName = 0;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\NVIDIA Corporation\\Global\\NVTweak\\Devices", 0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE, &hDevices) != ERROR_SUCCESS) {
		log("Could not open NVTweak\\Devices\n");
		goto out;
	}

	if (RegQueryInfoKey(hDevices, NULL, NULL, NULL, &cSubKeys, &cbMaxSubKey, NULL, NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS || cSubKeys == 0) {
		log("Could not find NVTweak\\Devices subkeys\n");
		goto out;
	}

	for (i = 0; i < cSubKeys; i++) {
		cbName = ARRAYSIZE(SubKeyName);
		if (RegEnumKeyEx(hDevices, i, SubKeyName, &cbName, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
			continue;
		wcsncat_s(SubKeyName, ARRAYSIZE(SubKeyName), L"\\Color", _TRUNCATE);
		if (RegOpenKeyEx(hDevices, SubKeyName, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hColorData) == ERROR_SUCCESS) {
			// Only update the brightness values
			for (j = 0; j < 3; j++) {
				swprintf(ColorKeyName, ARRAYSIZE(ColorKeyName), L"%d", NV_COLOR_REGISTRY_INDEX + j);
				size = sizeof(DWORD);
				if (RegQueryValueEx(hColorData, ColorKeyName, 0, &type, (LPBYTE)&val, &size) != ERROR_SUCCESS) {
					// Only update a brightness value if it already exists
					continue;
				}
				if (RegSetValueEx(hColorData, ColorKeyName, 0, type, (LPBYTE)&nvColorSettings[NV_ATTR_BRIGHTNESS][j], size) != ERROR_SUCCESS)
					log("Failed to update %S\\%S value\n", SubKeyName, ColorKeyName);
			}
			RegCloseKey(hColorData);
		}
	}

out:
	if (hDevices != NULL)
		RegCloseKey(hDevices);

	return ret;
}

// Keyboard procs
static void ChangeBrightness(bool bIncrease)
{
	static wchar_t menu_txt[64];

	brightness += bIncrease ? +5 : -5;
	if (brightness < 0)
		brightness = 0;
	if (brightness > 100)
		brightness = 100;

	nvColorSettings[NV_ATTR_BRIGHTNESS][NV_COLOR_RED] = 80 + (brightness / 5);
	nvColorSettings[NV_ATTR_BRIGHTNESS][NV_COLOR_GREEN] = 80 + (brightness / 5);
	nvColorSettings[NV_ATTR_BRIGHTNESS][NV_COLOR_BLUE] = 80 + (brightness / 5);
	WriteColorDataToRegistry();

	swprintf(menu_txt, ARRAYSIZE(menu_txt), L"Brightness: %d%%", brightness);
	tray.icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_00 + brightness / 5));
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
	static wchar_t menu_txt[64];
	int ret = 1;

	if (ReadColorDataFromRegistry() < 0) {
		log("This system doesn't appear to use an nVidia GPU - Aborting\n");
		goto out;
	}

	swprintf(menu_txt, ARRAYSIZE(menu_txt), L"Brightness: %d%%", brightness);

	HHOOK hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(0), 0);
	static struct tray_menu menu[] = {
		{.text = menu_txt },
		{.text = L"Pause", .checked = 0, .cb = PauseCallback },
		{.text = L"Use Internet Keys", .checked = 0, .cb = AlternateKeysCallback },
		{.text = L"Exit", .cb = ExitCallback },
		{.text = NULL }
	};
	tray.icon =	LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_00 + brightness / 5));
	tray.menu = menu;

	if (tray_init(&tray) < 0) {
		OutputDebugStringA("Failed to create tray\n");
		return 1;
	}
	while (tray_loop(1) == 0);

	UnhookWindowsHookEx(hHook);
	ret = 0;

out:
	return ret;
}
