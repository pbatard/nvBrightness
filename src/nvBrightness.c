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
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "resource.h"
#include "tray.h"
#include "nvapi.h"
#include "registry.h"

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

// Globals
static int32_t brightness = 50;
static int32_t nvColorSettings[NV_ATTR_MAX][NV_COLOR_MAX] = { 0 };
static bool enabled = true;
static bool use_alternate_keys = false;
static struct tray tray;

// Logging
static void logger(char* format, ...)
{
	char log_msg[256];

	va_list argp;
	va_start(argp, format);
	vsprintf_s(log_msg, sizeof(log_msg), format, argp);
	va_end(argp);
	OutputDebugStringA(log_msg);
}

// Calculate the Gamma offset at a specific index.
NvF32 CalculateGamma(NvS32 index, NvS32 brightness, NvS32 contrast, NvS32 gamma)
{
	NvF32 fBrightness, fContrast, fGamma;

	fContrast = (NvF32)(contrast - 100) / 100.0f;
	if (fContrast <= 0.0f)
		fContrast = (fContrast + 1.0f) * ((NvF32)index / 1023.0f - 0.5f);
	else
		fContrast = ((NvF32)index / 1023.0f - 0.5f) / (1.0f - fContrast);

	fBrightness = (NvF32)(brightness - 100) / 100.0f + fContrast + 0.5f;
	if (fBrightness < 0.0f)
		fBrightness = 0.0f;
	if (fBrightness > 1.0f)
		fBrightness = 1.0f;

	fGamma = (NvF32)pow((double)fBrightness, 1.0 / ((double)gamma / 100.0));
	if (fGamma < 0.0f)
		fGamma = 0.0f;
	if (fGamma > 1.0f)
		fGamma = 1.0f;

	return fGamma;
}

// Callbacks for Tray
static void AboutCallback(struct tray_menu* item)
{
	(void)item;
	MessageBoxW(NULL, L"nvBrightness v1.0\n\nCopyright © 2025 Pete Batard <pete@akeo.ie>\n"
		"https://github.com/pbatard/nvBrightness", L"About", MB_OK);
}

static void AlternateKeysCallback(struct tray_menu* item)
{
	use_alternate_keys = !use_alternate_keys;
	OutputDebugStringA("Alternate Keys: ");
	OutputDebugStringA(use_alternate_keys ? "Enabled\n" : "Disabled\n");
	item->checked = !item->checked;
	WriteRegistryKey32(REGKEY_HKCU, "UseAlternateKeys", item->checked);
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
static int ReadNvColorDataFromRegistry(void)
{
	int ret = -1;
	wchar_t SubKeyName[128], ColorKeyName[32];
	HKEY hDevices = NULL, hColorData = NULL;
	DWORD i, j, size, type, cSubKeys = 0, cbName;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\NVIDIA Corporation\\Global\\NVTweak\\Devices", 0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE, &hDevices) != ERROR_SUCCESS) {
		logger("Could not open NVTweak\\Devices\n");
		goto out;
	}

	if (RegQueryInfoKey(hDevices, NULL, NULL, NULL, &cSubKeys, NULL, NULL, NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS || cSubKeys == 0) {
		logger("Could not find NVTweak\\Devices subkeys\n");
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

static int WriteNvColorDataToRegistry(void)
{
	int ret = -1;
	wchar_t SubKeyName[128], ColorKeyName[32];
	HKEY hDevices = NULL, hColorData = NULL;
	DWORD i, j, size, type, val, cSubKeys = 0, cbName;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\NVIDIA Corporation\\Global\\NVTweak\\Devices", 0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE, &hDevices) != ERROR_SUCCESS) {
		logger("Could not open NVTweak\\Devices\n");
		goto out;
	}

	if (RegQueryInfoKey(hDevices, NULL, NULL, NULL, &cSubKeys, NULL, NULL, NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS || cSubKeys == 0) {
		logger("Could not find NVTweak\\Devices subkeys\n");
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
					logger("Failed to update %S\\%S value\n", SubKeyName, ColorKeyName);
			}
			RegCloseKey(hColorData);
		}
	}

out:
	if (hDevices != NULL)
		RegCloseKey(hDevices);

	return ret;
}

// nVidia API Procs
static int NvInit(void)
{
	NvAPI_Status r;
	NvAPI_ShortString errStr = { 0 };

	if (NvAPI_Init(logger) != 0) {
		logger("Failed to init NvAPI\n");
		return -1;
	}
	if (NvAPI_Initialize == NULL)
		return -1;
	r = NvAPI_Initialize();
	if (r != NVAPI_OK) {
		NvAPI_GetErrorMessage(r, errStr);
		logger("NvAPI_Initialize: %d %s\n", r, errStr);
		if (NvAPI_Exit != NULL)
			NvAPI_Exit();
		return -1;
	}

	return 0;
}

static __inline void NvExit(void)
{
	NvAPI_Exit();
}

static int NvGetGpuCount(void)
{
	NvAPI_Status r;
	NvAPI_ShortString errStr = { 0 };
	NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
	NvU32 gpuCount = 0;

	if (NvAPI_EnumPhysicalGPUs == NULL)
		return -1;
	r = NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
	if (r != NVAPI_OK) {
		NvAPI_GetErrorMessage(r, errStr);
		logger("NvAPI_EnumPhysicalGPUs: %d %s\n", r, errStr);
		return -1;
	}

	return (int)gpuCount;
}

void NvCreateGammaCorrection(NV_GAMMA_CORRECTION_EX* gammaCorrection)
{
	gammaCorrection->version = NVGAMMA_CORRECTION_EX_VER;
	gammaCorrection->unknown = 1;

	for (NvS32 Index = 0; Index < NV_GAMMARAMPEX_NUM_VALUES; Index++) {
		for (int Color = 0; Color < NV_COLOR_MAX; Color++) {
			gammaCorrection->gammaRampEx[NV_COLOR_MAX * Index + Color] = CalculateGamma(
				Index,
				nvColorSettings[NV_ATTR_BRIGHTNESS][Color],
				nvColorSettings[NV_ATTR_CONTRAST][Color],
				nvColorSettings[NV_ATTR_GAMMA][Color]
			);
		}
	}
}

int NvUpdateGamma(void)
{
	static NV_GAMMA_CORRECTION_EX gammaCorrection;
	int ret = -1;
	NvAPI_Status r;
	NvAPI_ShortString errStr = { 0 };
	NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
	NvU32 gpuCount = 0;

	NvCreateGammaCorrection(&gammaCorrection);

	r = NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
	if (r != NVAPI_OK) {
		NvAPI_GetErrorMessage(r, errStr);
		logger("NvAPI_EnumPhysicalGPUs: %d %s\n", r, errStr);
		return -1;
	}

	for (NvU32 i = 0; i < gpuCount; i++) {
		NvU32 displayCount = 0;
		NvU32 busId = 0;
		NvU32 busSlotId = 0;

		r = NvAPI_GPU_GetConnectedDisplayIds(gpuHandles[i], NULL, &displayCount, 0);
		if (r != NVAPI_OK) {
			NvAPI_GetErrorMessage(r, errStr);
			logger("NvAPI_GPU_GetConnectedDisplayIds: %d %s\n", r, errStr);
			return -1;
		}

		if (displayCount == 0)
			return -1;

		NV_GPU_DISPLAYIDS* dispIds = calloc(displayCount, sizeof(NV_GPU_DISPLAYIDS));
		if (dispIds == NULL) {
			logger("Could not allocate NV_GPU_DISPLAYIDS array\n");
			return -1;
		}
		dispIds[0].version = NV_GPU_DISPLAYIDS_VER;

		r = NvAPI_GPU_GetConnectedDisplayIds(gpuHandles[i], dispIds, &displayCount, 0);
		if (r == NVAPI_OK) {
			for (NvU32 j = 0; j < displayCount; j++) {
				r = NvAPI_DISP_SetTargetGammaCorrection(dispIds[j].displayId, &gammaCorrection);
				if (r != NVAPI_OK) {
					NvAPI_GetErrorMessage(r, errStr);
					logger("NvAPI_DISP_SetTargetGammaCorrection failed for displayId[%d] (0x%08x): %d %s\n", j, dispIds[j].displayId, r, errStr);
				} else {
#ifdef _DEBUG
					logger("Updated gamma for displayId[%d] = 0x%08x to %d%% brightness\n", j, dispIds[j].displayId, brightness);
#endif
					ret = 0;
				}
			}
		}
		free(dispIds);
	}

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
	NvUpdateGamma();
	WriteNvColorDataToRegistry();

	tray.icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_00 + brightness / 5));
	tray_update(&tray);
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (enabled && nCode == HC_ACTION && lParam != 0) {
		KBDLLHOOKSTRUCT* kbhs = (KBDLLHOOKSTRUCT*)lParam;

		if (!use_alternate_keys) {
			// TODO: Check if Alt/Ctrl/Shift are pressed and ignore then?
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

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	static wchar_t menu_txt[64];
	int ret = 1;

	if (NvInit() < 0 || NvGetGpuCount() < 1) {
		MessageBoxA(NULL, "An nVidiaGPU could not be detected on this system.\n"
			"This application will now exit.", "No nVidia GPU", MB_OK);
		goto out;
	}

	if (ReadNvColorDataFromRegistry() < 0) {
		MessageBoxA(NULL, "Could not read existing nVidia color settings from the registry.\n\n"
			"You may have to adjust desktop color settings at least once, using the nVidia control panel.",
			"No nVidia color settings", MB_OK);
		goto out;
	}

	HHOOK hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(0), 0);
	static struct tray_menu menu[] = {
		{.text = L"About", .cb = AboutCallback },
		{.text = L"Pause", .checked = 0, .cb = PauseCallback },
		{.text = L"Use Internet navigation keys", .checked = 0, .cb = AlternateKeysCallback },
		{.text = L"Exit", .cb = ExitCallback },
		{.text = NULL }
	};
	use_alternate_keys = (ReadRegistryKey32(REGKEY_HKCU, "UseAlternateKeys") != 0);
	menu[2].checked = use_alternate_keys;
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
	NvExit();
	return ret;
}
