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
#include <commctrl.h>
#include <shellscalingapi.h>

#include <string>
#include <iostream>
#include <format>
#include <list>
using std::string;
using std::wstring;
using std::list;
using std::format;

#include "resource.h"
#include "tray.h"
#include "nvapi.h"
#include "registry.h"
#include "DarkTaskDialog.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "shcore.lib")

// Using a GUID for the tray icon allows the executable to be moved around or change version
// without requiring the user to go through the taskbar settings to re-enable the icon.
#define TRAY_ICON_GUID { 0x64397973, 0xa694, 0x4640, { 0x80, 0x26, 0x96, 0x04, 0x46, 0x75, 0x00, 0x2b } }

// nVidia Color data definitions
#define NV_COLOR_REGISTRY_INDEX     3538946

enum {
	nvColorRed = 0,
	nvColorGreen,
	nvColorBlue,
	nvColorMax
};

enum {
	nvAttrBrightness = 0,
	nvAttrContrast,
	nvAttrGamma,
	nvAttrMax
};

// Structs
typedef struct {
	void* data;
	VS_FIXEDFILEINFO* fixed;
	wchar_t* ProductName;
	wchar_t* CompanyName;
	wchar_t* LegalCopyright;
	wchar_t* Comments;
} version_t;

typedef struct {
	bool enabled;
	bool use_alternate_keys;
	float increment;
} settings_t;

// Classes
class nvDisplay {
	uint32_t displayId;
	wstring registryKeyString;
	float colorSetting[nvAttrMax][nvColorMax];
	string displayString;
public:
	nvDisplay(uint32_t);
	string GetDisplayString();
	bool UpdateGamma();
	void ChangeBrightness(float delta);
	float GetBrightness();
	void SaveColorSettings(bool ApplyToAll);
	static size_t EnumerateDisplays();
};

// Globals
wchar_t *APPLICATION_NAME = NULL, *COMPANY_NAME = NULL;	// Needed for registry.h
static version_t version = { 0 };
static settings_t settings = { true, false, 0.5f };
static struct tray tray;
static list<nvDisplay> displayList;

// Logging
static void logger(const char* format, ...)
{
	char log_msg[256];

	va_list argp;
	va_start(argp, format);
	vsprintf_s(log_msg, sizeof(log_msg), format, argp);
	va_end(argp);
	OutputDebugStringA(log_msg);
}

static inline char* NvErrStr(NvAPI_Status r)
{
	static NvAPI_ShortString errStr = { 0 };
	NvAPI_GetErrorMessage(r, errStr);
	return errStr;
}

// Helper functions
static bool IsDarkModeEnabled(void)
{
	DWORD data = 0, size = sizeof(data);
	if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &data, &size) == ERROR_SUCCESS)
		return (data == 0);
	return false;
}

static string GetMonitorString(NvU32 displayId)
{
	NvAPI_Status r;
	NvDisplayHandle displayHandle;
	NvAPI_ShortString displayName;
	DISPLAY_DEVICEA displayDevice, monitorDevice;
	displayDevice.cb = sizeof(DISPLAY_DEVICEA);

	r = NvAPI_DISP_GetDisplayHandleFromDisplayId(displayId, &displayHandle);
	if (r != NVAPI_OK) {
		logger("NvAPI_DISP_GetDisplayHandleFromDisplayId(0x%08x): %d %s\n", displayId, r, NvErrStr(r));
		return "";
	}
	r = NvAPI_GetAssociatedNvidiaDisplayName(displayHandle, displayName);
	if (r != NVAPI_OK) {
		logger("NvAPI_GetAssociatedNvidiaDisplayName(0x%08x): %d %s\n", displayId, r, NvErrStr(r));
		return "";
	}

	for (auto i = 0; EnumDisplayDevicesA(NULL, i, &displayDevice, 0); i++) {
		if (_stricmp(displayDevice.DeviceName, displayName) != 0)
			continue;
		monitorDevice.cb = sizeof(DISPLAY_DEVICEA);
		for (auto j = 0; EnumDisplayDevicesA(displayDevice.DeviceName, j, &monitorDevice, 0); ++j) {
			if (monitorDevice.StateFlags & DISPLAY_DEVICE_ACTIVE)
				return monitorDevice.DeviceString;
		}
	}
	return "";
}

static int GetIconIndex(nvDisplay& display)
{
	int icon_index = (int)(display.GetBrightness() - 80.0f);
	if (icon_index < 0)
		icon_index = 0;
	if (icon_index > 20)
		icon_index = 20;
	return icon_index;
}

NvF32 CalculateGamma(NvS32 index, NvF32 fBrightness, NvF32 fContrast, NvF32 fGamma)
{
	fContrast = (fContrast - 100.0f) / 100.0f;
	if (fContrast <= 0.0f)
		fContrast = (fContrast + 1.0f) * ((NvF32)index / 1023.0f - 0.5f);
	else
		fContrast = ((NvF32)index / 1023.0f - 0.5f) / (1.0f - fContrast);

	fBrightness = (fBrightness - 100.0f) / 100.0f + fContrast + 0.5f;
	if (fBrightness < 0.0f)
		fBrightness = 0.0f;
	if (fBrightness > 1.0f)
		fBrightness = 1.0f;

	fGamma = (NvF32)pow((double)fBrightness, 1.0 / ((double)fGamma / 100.0));
	if (fGamma < 0.0f)
		fGamma = 0.0f;
	if (fGamma > 1.0f)
		fGamma = 1.0f;

	return fGamma;
}

// nvDisplay methods
nvDisplay::nvDisplay(uint32_t displayId)
{
	GUID guid = { 0 };
	NvAPI_Status r;

	this->displayId = displayId;
	this->displayString = GetMonitorString(displayId);

	r = NvAPI_SYS_GetLUIDFromDisplayID(displayId, 1, &guid);
	if (r != NVAPI_OK) {
		logger("NvAPI_SYS_GetLUIDFromDisplayID(0x%08x): %d %s\n", displayId, r, NvErrStr(r));
		for (auto j = 0; j < 9; j++)
			this->colorSetting[j / 3][j % 3] = 100.0f;
	} else {
		// The part we are after is the second DWORD of the GUID, XOR'd with 0xF00000000
		this->registryKeyString = format(L"Software\\NVIDIA Corporation\\Global\\NVTweak\\Devices\\{}-0\\Color",
			((uint32_t*)&guid)[1] ^ 0xf0000000);
		wchar_t RegColorKeyStr[128];
		for (auto i = 0; i < 9; i++) {
			swprintf_s(RegColorKeyStr, ARRAYSIZE(RegColorKeyStr),
				L"%s\\%d", this->registryKeyString.c_str(), NV_COLOR_REGISTRY_INDEX + i);
			this->colorSetting[i / 3][i % 3] = (float)ReadRegistryKey32(HKEY_CURRENT_USER, RegColorKeyStr);
			// Set the default value if we couldn't read the key or it's out of bounds
			if (this->colorSetting[i / 3][i % 3] < 80.0f || this->colorSetting[i / 3][i % 3] > 120.0f)
				this->colorSetting[i / 3][i % 3] = 100.0f;
		}
	}
}

string nvDisplay::GetDisplayString()
{
	return this->displayString;
}

float nvDisplay::GetBrightness()
{
	float brightness = this->colorSetting[nvAttrBrightness][nvColorRed] +
		this->colorSetting[nvAttrBrightness][nvColorGreen] +
		this->colorSetting[nvAttrBrightness][nvColorBlue];
	return brightness / 3.0f;
}

void nvDisplay::ChangeBrightness(float delta)
{
	for (auto Color = 0; Color < nvColorMax; Color++) {
		this->colorSetting[nvAttrBrightness][Color] += delta;
		if (this->colorSetting[nvAttrBrightness][Color] < 80.0f)
			this->colorSetting[nvAttrBrightness][Color] = 80.0f;
		if (this->colorSetting[nvAttrBrightness][Color] > 100.0f)
			this->colorSetting[nvAttrBrightness][Color] = 100.0f;
	}
}

bool nvDisplay::UpdateGamma()
{
	NV_GAMMA_CORRECTION_EX gammaCorrection;
	NvAPI_Status r;

	gammaCorrection.version = NVGAMMA_CORRECTION_EX_VER;
	gammaCorrection.unknown = 1;

	for (NvS32 Index = 0; Index < NV_GAMMARAMPEX_NUM_VALUES; Index++) {
		for (auto Color = 0; Color < nvColorMax; Color++) {
			gammaCorrection.gammaRampEx[nvColorMax * Index + Color] = CalculateGamma(
				Index,
				this->colorSetting[nvAttrBrightness][Color],
				this->colorSetting[nvAttrContrast][Color],
				this->colorSetting[nvAttrGamma][Color]
			);
		}
	}

	r = NvAPI_DISP_SetTargetGammaCorrection(this->displayId, &gammaCorrection);
	if (r != NVAPI_OK)
		logger("NvAPI_DISP_SetTargetGammaCorrection failed for display 0x%08x: %d %s\n", this->displayId, r, NvErrStr(r));

	return r == NVAPI_OK;
}

void nvDisplay::SaveColorSettings(bool ApplyToAll)
{
	wchar_t RegColorKeyStr[128];

	for (auto i = 0; i < 9; i++) {
		swprintf_s(RegColorKeyStr, ARRAYSIZE(RegColorKeyStr),
			L"%s\\%d", this->registryKeyString.c_str(), NV_COLOR_REGISTRY_INDEX + i);
		WriteRegistryKey32(HKEY_CURRENT_USER, RegColorKeyStr, (uint32_t)this->colorSetting[i / 3][i % 3]);
	}
	// Add the NvCplGammaSet key to indicate that Gamma should be restored by the nVidia driver
	swprintf_s(RegColorKeyStr, ARRAYSIZE(RegColorKeyStr), L"%s\\NvCplGammaSet", this->registryKeyString.c_str());
	WriteRegistryKey32(HKEY_CURRENT_USER, RegColorKeyStr, 1);

	// TODO: Add an option to apply to all displays
}

size_t nvDisplay::EnumerateDisplays()
{
	NvAPI_Status r;
	NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
	NvU32 gpuCount = 0;

	displayList.clear();

	r = NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
	if (r != NVAPI_OK) {
		logger("NvAPI_EnumPhysicalGPUs: %d %s\n", r, NvErrStr(r));
		return -1;
	}

	for (NvU32 i = 0; i < gpuCount; i++) {
		NvU32 displayCount = 0;

		r = NvAPI_GPU_GetConnectedDisplayIds(gpuHandles[i], NULL, &displayCount, 0);
		if (r != NVAPI_OK) {
			logger("NvAPI_GPU_GetConnectedDisplayIds[%d]: %d %s\n", i, r, NvErrStr(r));
			continue;
		}

		if (displayCount == 0)
			continue;

		NV_GPU_DISPLAYIDS* displayIds = (NV_GPU_DISPLAYIDS*)calloc(displayCount, sizeof(NV_GPU_DISPLAYIDS));
		if (displayIds == NULL) {
			logger("Could not allocate NV_GPU_DISPLAYIDS array\n");
			return -1;
		}
		displayIds[0].version = NV_GPU_DISPLAYIDS_VER;

		r = NvAPI_GPU_GetConnectedDisplayIds(gpuHandles[i], displayIds, &displayCount, 0);
		if (r != NVAPI_OK) {
			logger("NvAPI_GPU_GetConnectedDisplayIds[%d]: %d %s\n", i, r, NvErrStr(r));
		} else {
			for (NvU32 j = 0; j < displayCount; j++)
				displayList.emplace_back(displayIds[j].displayId); // new nvDisplay(displayIds[j].displayId));
		}
		free(displayIds);
	}

	return displayList.size();
}

// Task dialogs and message boxes that do respect the user Dark Mode settings
static __inline HRESULT ProperTaskDialogIndirect(const TASKDIALOGCONFIG* pTaskConfig, int* pnButton,
	int* pnRadioButton, BOOL* pfVerificationFlagChecked)
{
	SFTRS::DarkTaskDialog::setTheme(IsDarkModeEnabled() ? SFTRS::DarkTaskDialog::dark : SFTRS::DarkTaskDialog::light);
	return TaskDialogIndirect(pTaskConfig, pnButton, pnRadioButton, pfVerificationFlagChecked);
}

static __inline void ProperMessageBox(wchar_t* icon, const wchar_t* title, const wchar_t* format, ...)
{
	wchar_t msg[512];

	va_list argp;
	va_start(argp, format);
	vswprintf_s(msg, ARRAYSIZE(msg), format, argp);
	va_end(argp);

	TASKDIALOGCONFIG config = { 0 };
	config.hMainIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON));
	config.cbSize = sizeof(config);
	config.pszMainIcon = icon;
	config.pszWindowTitle = title;
	config.pszContent = msg;
	ProperTaskDialogIndirect(&config, NULL, NULL, NULL);
}

// Open hyperlink from TaskDialog
static HRESULT CALLBACK TaskDialogCallback(HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData)
{
	if (uNotification == TDN_HYPERLINK_CLICKED) {
		ShellExecute(hwnd, TEXT("open"), (TCHAR*)lParam, NULL, NULL, SW_SHOW);
	}
	return 0;
}

// Callbacks for Tray
static void AboutCallback(struct tray_menu* item)
{
	wchar_t szTitle[64] = L"";
	wchar_t szHeader[128] = L"";
	wchar_t szFooter[128] = L"";
	wchar_t szProject[128] = L"";
	wchar_t szReleaseUrl[64] = L"";

	(void)item;

	swprintf_s(szTitle, ARRAYSIZE(szTitle), L"About %s", version.ProductName);
	swprintf_s(szHeader, ARRAYSIZE(szHeader), L"%s v%d.%d", version.ProductName,
		version.fixed->dwProductVersionMS >> 16, version.fixed->dwProductVersionMS & 0xffff);
	const wchar_t* szContent = L"Increase/decrease display brightness using nVidia controls.\nKeyboard shortcuts: Scroll Lock / Pause|Break";
	swprintf_s(szFooter, ARRAYSIZE(szFooter), L"Licensed under <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GPLv3</a>, %s",
		version.LegalCopyright);
	swprintf_s(szProject, ARRAYSIZE(szProject), L"Project page\n%s", version.Comments);
	swprintf_s(szReleaseUrl, ARRAYSIZE(szReleaseUrl), L"%s/releases/latest", version.Comments);
	TASKDIALOG_BUTTON aCustomButtons[] = {
		{ 1001, szProject },
		{ 1002, L"Latest release" },
	};
	TASKDIALOGCONFIG config = { 0 };
	config.cbSize = sizeof(config);
	config.dwFlags = TDF_USE_HICON_MAIN | TDF_USE_COMMAND_LINKS | TDF_ENABLE_HYPERLINKS | TDF_EXPANDED_BY_DEFAULT | TDF_EXPAND_FOOTER_AREA | TDF_ALLOW_DIALOG_CANCELLATION;
	config.pButtons = aCustomButtons;
	config.cButtons = sizeof(aCustomButtons) / sizeof(aCustomButtons[0]);
	config.pszWindowTitle = szTitle;
	config.nDefaultButton = IDOK;
	config.hMainIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON));
	config.pszMainInstruction = szHeader;
	config.pszContent = szContent;
	config.pszFooter = szFooter;
	config.pszFooterIcon = TD_INFORMATION_ICON;
	config.dwCommonButtons = TDCBF_OK_BUTTON;
	int nClickedBtn;
	if (SUCCEEDED(ProperTaskDialogIndirect(&config, &nClickedBtn, NULL, NULL))) {
		if (nClickedBtn == 1001)
			ShellExecute(NULL, L"Open", version.Comments, NULL, NULL, SW_SHOW);
		if (nClickedBtn == 1002)
			ShellExecute(NULL, L"Open", szReleaseUrl, NULL, NULL, SW_SHOW);
	}
}

static void AlternateKeysCallback(struct tray_menu* item)
{
	settings.use_alternate_keys = !settings.use_alternate_keys;
	logger("Alternate Keys: %s\n", settings.use_alternate_keys ? "Enabled" : "Disabled");
	item->checked = !item->checked;
	WriteRegistryKey32(HKEY_CURRENT_USER, L"UseAlternateKeys", item->checked);
	tray_update(&tray);
}

static void PauseCallback(struct tray_menu* item)
{
	settings.enabled = !settings.enabled;
	item->checked = !item->checked;
	tray_update(&tray);
}

static void ExitCallback(struct tray_menu* item)
{
	(void)item;
	tray_exit();
}

// nVidia API Procs
static int NvInit(void)
{
	NvAPI_Status r;

	if (NvAPI_Init(logger) != 0) {
		logger("Failed to init NvAPI\n");
		return -1;
	}
	if (NvAPI_Initialize == NULL)
		return -1;
	r = NvAPI_Initialize();
	if (r != NVAPI_OK) {
		logger("NvAPI_Initialize: %d %s\n", r, NvErrStr(r));
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
	NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
	NvU32 gpuCount = 0;

	if (NvAPI_EnumPhysicalGPUs == NULL)
		return -1;
	r = NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
	if (r != NVAPI_OK) {
		logger("NvAPI_EnumPhysicalGPUs: %d %s\n", r, NvErrStr(r));
		return -1;
	}

	return (int)gpuCount;
}

// Keyboard procs
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (settings.enabled && nCode == HC_ACTION && lParam != 0) {
		KBDLLHOOKSTRUCT* kbhs = (KBDLLHOOKSTRUCT*)lParam;

		if (!settings.use_alternate_keys) {
			// TODO: Check if Alt/Ctrl/Shift are pressed and ignore then?
			if ((kbhs->vkCode == VK_SCROLL || kbhs->vkCode == VK_PAUSE) && (kbhs->flags == 0x0)) {
				for (auto& display : displayList) {
					display.ChangeBrightness((kbhs->vkCode == VK_PAUSE) ? settings.increment : -settings.increment);
					display.UpdateGamma();
					display.SaveColorSettings(false);
				}
				if (displayList.size() >= 1) {
					tray.icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_00 + GetIconIndex(displayList.front())));
					tray_update(&tray);
				}
				return 1;
			}
		} else {
			// Alternate way, that uses the Internet navigation keys
			if ((kbhs->vkCode == VK_LEFT || kbhs->vkCode == VK_RIGHT) && ((kbhs->flags & 0xA1) == 0x21)) {
				for (auto& display : displayList) {
					display.ChangeBrightness((kbhs->vkCode == VK_RIGHT) ? settings.increment : -settings.increment);
					display.UpdateGamma();
					display.SaveColorSettings(false);
				}
				if (displayList.size() >= 1) {
					tray.icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_00 + GetIconIndex(displayList.front())));
					tray_update(&tray);
				}
				return 1;
			}
		}
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// I've said it before and I'll say it again:
// Retrieving versioning and file information on Windows is a COMPLETE SHIT SHOW!!!
#define GET_VERSION_INFO(name) do { swprintf_s(SubBlock, ARRAYSIZE(SubBlock), \
	L"\\StringFileInfo\\%04x%04x\\" #name, lpTranslate[0].wLanguage, lpTranslate[0].wCodePage); \
	VerQueryValue(version.data, SubBlock, (LPVOID*)&version.name, (PUINT)&size); } while(0)

bool PopulateVersionData(void)
{
	struct LANGANDCODEPAGE {
		WORD wLanguage;
		WORD wCodePage;
	} *lpTranslate;
	wchar_t exe_path[MAX_PATH], SubBlock[256];
	DWORD size, dummy;
	BOOL br;

	GetModuleFileName(NULL, exe_path, ARRAYSIZE(exe_path));
	size = GetFileVersionInfoSize(exe_path, &dummy);
	if (size == 0)
		return false;
	version.data = malloc(size);	// freed on app exit
	if (version.data == NULL)
		return false;

	if (!GetFileVersionInfo(exe_path, 0, size, version.data))
		return false;

	br = VerQueryValue(version.data, L"\\", (void**)&version.fixed, (PUINT)&size);
	if (!br || version.fixed == NULL || size != sizeof(VS_FIXEDFILEINFO))
		return false;

	if (!VerQueryValue(version.data, L"\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, (PUINT)&size) ||
		size < sizeof(struct LANGANDCODEPAGE))
		return false;

	GET_VERSION_INFO(ProductName);
	GET_VERSION_INFO(CompanyName);
	GET_VERSION_INFO(LegalCopyright);
	GET_VERSION_INFO(Comments);

	APPLICATION_NAME = version.ProductName;
	COMPANY_NAME = version.CompanyName;

	return (version.ProductName != NULL && version.CompanyName != NULL &&
		version.LegalCopyright != NULL && version.Comments != NULL);
}

// Main proc
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	static wchar_t menu_txt[64];
	int ret = 1;
	int icon_index = 20;
	GUID guid = TRAY_ICON_GUID;
	HHOOK hHook = NULL;

	SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

	if (!PopulateVersionData()) {
		ProperMessageBox(TD_ERROR_ICON, L"No version information",
			L"Version information could not be read from the executable.");
		goto out;
	}

	// Technically, someone might have an nVidia eGPU and want to run our app before they
	// hotplug it, but I'd rather make it explicit for people who won't have an nVidia GPU
	// anywhere near their system that the app will not be working for them
	if (NvInit() < 0 || NvGetGpuCount() < 1) {
		ProperMessageBox(TD_WARNING_ICON, L"No nVidia GPU",
			L"An nVidia GPU could not be detected on this system.\n"
			"%s will now exit.\n", version.ProductName);
		goto out;
	}

	// Build the display list
	nvDisplay::EnumerateDisplays();

	for (auto& display : displayList)
		logger("Found display: %s\n", display.GetDisplayString().c_str());

	hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(0), 0);
	static struct tray_menu menu[] = {
		{ .text = L"About", .cb = AboutCallback },
		{ .text = L"Pause", .checked = 0, .cb = PauseCallback },
		{ .text = L"Use Internet navigation keys", .checked = 0, .cb = AlternateKeysCallback },
		{ .text = L"Exit", .cb = ExitCallback },
		{ .text = NULL }
	};
	settings.use_alternate_keys = (ReadRegistryKey32(HKEY_CURRENT_USER, L"UseAlternateKeys") != 0);
	menu[2].checked = settings.use_alternate_keys;

	if (displayList.size() >= 1)
		icon_index = GetIconIndex(displayList.front());
	tray.icon =	LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_00 + icon_index));
	tray.menu = menu;

	if (tray_init(&tray, version.ProductName, guid) < 0) {
		ProperMessageBox(TD_ERROR_ICON, L"Failed to create tray application",
			L"There was an error registering the tray application.\n"
			"%s will now exit.\n", version.ProductName);
		return 1;
	}
	while (tray_loop(1) == 0);

	UnhookWindowsHookEx(hHook);
	ret = 0;

out:
	displayList.clear();
	NvExit();
	free(version.data);
#ifdef _DEBUG
	// NB: You don't want to use _CrtDumpMemoryLeaks() with C++.
	// See: https://stackoverflow.com/a/5266164/1069307
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	return ret;
}
