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
#include <commctrl.h>
#include <initguid.h>
#include <ntddvdeo.h>
#include <powerbase.h>
#include <powrprof.h>
#include <shellscalingapi.h>

#include <string>
#include <list>

using namespace std;

#include "nvBrightness.h"
#include "resource.h"
#include "tray.h"
#include "nvapi.h"
#include "registry.h"
#include "DarkTaskDialog.hpp"
#include "nvMonitor.hpp"
#include "nvDisplay.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "version.lib")

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
	bool autostart;
	bool use_alternate_keys;
	bool resume_to_last_input;
	float increment;
	uint32_t active_display;
} settings_t;

// Globals
GLOBAL_NVAPI_INSTANCE;
GLOBAL_TRAY_INSTANCE;
wchar_t *APPLICATION_NAME = NULL, *COMPANY_NAME = NULL;	// Needed for registry.h

static version_t version = { 0 };
static settings_t settings = { true, false, false, false, 0.5f, 0 };
static vector<struct tray_menu> submenu;
static struct tray tray = { 0 };
static list<nvDisplay> display_list;
// The following string buffers are modified to display the input names
static wchar_t home_input[64] = L"Home input\t［⊞］［Shift］［Home］";
static wchar_t next_input[64] = L"Next input\t［⊞］［Shift］［PgUp］";
static wchar_t prev_input[64] = L"Prev input\t［⊞］［Shift］［PgDn］";
static wchar_t wake_input[64] = L"Wake up to Home";

// Logging
void logger(const char* format, ...)
{
	static char log_msg[512];

	va_list argp;
	va_start(argp, format);
	vsnprintf_s(log_msg, sizeof(log_msg), _TRUNCATE, format, argp);
	va_end(argp);
	OutputDebugStringA(log_msg);
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

// I sure wouldn't mind using a vector instead of a list and not having to do this,
// if C++ didn't throw a hissy fit about emplace and future in constructors.
static nvDisplay& GetDisplayAt(int index)
{
	int i = 0;
	for (auto& display : display_list)
		if (i++ == index)
			return display;
	return display_list.front();
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

static void UnRegisterHotKeys(void)
{
	for (int hk = 0; hk < hkMax; hk++)
		tray_unregister_hotkkey(hk);
}

static bool RegisterHotKeys(void)
{
	UnRegisterHotKeys();
	bool b = true;
	b &= tray_register_hotkey(hkPowerOffMonitor, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, VK_END);
	b &= tray_register_hotkey(hkRestoreInput, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, VK_HOME);
	b &= tray_register_hotkey(hkNextInput, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, VK_NEXT);
	b &= tray_register_hotkey(hkPreviousInput, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, VK_PRIOR);
	b &= tray_register_hotkey(hkNextMonitor, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, VK_OEM_PERIOD);
	b &= tray_register_hotkey(hkPreviousMonitor, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, VK_OEM_COMMA);

	if (settings.use_alternate_keys) {
		// Allegedly, per https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey#remarks
		// "If a hot key already exists with the same hWnd and id parameters, it is maintained along with the new hot key"
		// so, we should be able to register VK_BROWSER_FORWARD and Alt → with the same ID.
		// In practice however, THIS DOES NOT WORK for some combinations, and it causes the hotkeys to be ignored.
		// So we have to use different IDs even if we want to map 2 keys to the same action...
		b &= tray_register_hotkey(hkIncreaseBrightness, MOD_ALT, VK_RIGHT);
		b &= tray_register_hotkey(hkDecreaseBrightness, MOD_ALT, VK_LEFT);
		b &= tray_register_hotkey(hkIncreaseBrightness2, 0, VK_BROWSER_FORWARD);
		b &= tray_register_hotkey(hkDecreaseBrightness2, 0, VK_BROWSER_BACK);
	} else {
		b &= tray_register_hotkey(hkIncreaseBrightness, MOD_WIN | MOD_SHIFT, VK_ADD);
		b &= tray_register_hotkey(hkDecreaseBrightness, MOD_WIN | MOD_SHIFT, VK_SUBTRACT);
	}
	return b;
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
	_vsnwprintf_s(msg, ARRAYSIZE(msg), _TRUNCATE, format, argp);
	va_end(argp);

	TASKDIALOGCONFIG config = { 0 };
	config.dwFlags = TDF_SIZE_TO_CONTENT;
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
	wchar_t title[64] = L"";
	wchar_t header[128] = L"";
	wchar_t footer[128] = L"";
	wchar_t project[128] = L"";
	wchar_t release_url[64] = L"";

	(void)item;

	_snwprintf_s(title, ARRAYSIZE(title), _TRUNCATE, L"About %s", version.ProductName);
	_snwprintf_s(header, ARRAYSIZE(header), _TRUNCATE, L"%s v%d.%d", version.ProductName,
		version.fixed->dwProductVersionMS >> 16, version.fixed->dwProductVersionMS & 0xffff);
	const wchar_t* szContent = L"Increase/decrease display brightness using nVidia controls.";
	_snwprintf_s(footer, ARRAYSIZE(footer), _TRUNCATE, L"%s, <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GPLv3</a>",
		version.LegalCopyright);
	_snwprintf_s(project, ARRAYSIZE(project), _TRUNCATE, L"Project page\n%s", version.Comments);
	_snwprintf_s(release_url, ARRAYSIZE(release_url), _TRUNCATE, L"%s/releases/latest", version.Comments);
	TASKDIALOG_BUTTON custom_buttons[] = {
		{ 1001, project },
		{ 1002, L"Latest release" },
	};
	TASKDIALOGCONFIG config = { 0 };
	config.cbSize = sizeof(config);
	config.dwFlags = TDF_USE_HICON_MAIN | TDF_USE_COMMAND_LINKS | TDF_ENABLE_HYPERLINKS | TDF_EXPANDED_BY_DEFAULT | TDF_EXPAND_FOOTER_AREA | TDF_ALLOW_DIALOG_CANCELLATION;
	config.pButtons = custom_buttons;
	config.cButtons = sizeof(custom_buttons) / sizeof(custom_buttons[0]);
	config.pszWindowTitle = title;
	config.nDefaultButton = IDOK;
	config.hMainIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON));
	config.pszMainInstruction = header;
	config.pszContent = szContent;
	config.pszFooter = footer;
	config.pszFooterIcon = TD_INFORMATION_ICON;
	config.dwCommonButtons = TDCBF_OK_BUTTON;
	int clicked_button;
	if (SUCCEEDED(ProperTaskDialogIndirect(&config, &clicked_button, NULL, NULL))) {
		if (clicked_button == 1001)
			ShellExecute(NULL, L"Open", version.Comments, NULL, NULL, SW_SHOW);
		if (clicked_button == 1002)
			ShellExecute(NULL, L"Open", release_url, NULL, NULL, SW_SHOW);
	}
}

static void AlternateKeysCallback(struct tray_menu* item)
{
	settings.use_alternate_keys = !settings.use_alternate_keys;
	RegisterHotKeys();
	item->checked = !item->checked;
	WriteRegistryKey32(HKEY_CURRENT_USER, L"UseAlternateKeys", item->checked);
	if (settings.use_alternate_keys) {
		tray.menu[0].text = L"Brightness +\t［Internet Fwd］ or ［Alt］［→］";
		tray.menu[1].text = L"Brightness −\t［Internet Back］ or ［Alt］［←］";
	} else {
		tray.menu[0].text = L"Brightness +\t［⊞］［Shift］［Num +］";
		tray.menu[1].text = L"Brightness −\t［⊞］［Shift］［Num −］";
	}
	tray_update(&tray);
}

static void PauseCallback(struct tray_menu* item)
{
	settings.enabled = !settings.enabled;
	item->checked = !item->checked;
	if (settings.enabled)
		RegisterHotKeys();
	else
		UnRegisterHotKeys();
	tray_update(&tray);
}

static void PowerOffCallback(struct tray_menu* item)
{
	(void)item;
	SendMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
}

static void RestoreInputCallback(struct tray_menu* item)
{
	(void)item;
	tray_simulate_hottkey(hkRestoreInput);
}

static void ResumeToLastInputCallback(struct tray_menu* item)
{
	settings.resume_to_last_input = !settings.resume_to_last_input;
	item->checked = !item->checked;
	WriteRegistryKey32(HKEY_CURRENT_USER, L"ResumeToLastInput", item->checked);
	tray_update(&tray);
}

static void ActiveDisplayCallback(struct tray_menu* item)
{
	uint32_t i, selected_index = (uint32_t)(uintptr_t)item->context;

	if (item->checked || display_list.size() == 0)
		return;

	for (i = 1; i <= display_list.size(); i++)
		submenu[i].checked = false;
	item->checked = true;

	settings.active_display = selected_index;
	WriteRegistryKey32(HKEY_CURRENT_USER, L"ActiveDisplay", settings.active_display);

	auto& display = GetDisplayAt(settings.active_display);
	logger("Active display: %S\n", display.GetDisplayName());
	tray.icon = LoadIcon(GetModuleHandle(NULL),
		MAKEINTRESOURCE(IDI_ICON_00 + GetIconIndex(display)));
	tray_simulate_hottkey(hkUpdateSubmenu);
}

static void AutoStartCallback(struct tray_menu* item)
{
	wchar_t key_name[128], exe_path[MAX_PATH + 2] = { 0 };

	settings.autostart = !settings.autostart;
	item->checked = !item->checked;
	_snwprintf_s(key_name, ARRAYSIZE(key_name), _TRUNCATE,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Run\\%s", version.ProductName);
	GetModuleFileName(NULL, &exe_path[1], MAX_PATH);
	// Quote the executable path
	exe_path[0] = L'"';
	exe_path[wcslen(exe_path)] = L'"';

	if (settings.autostart) {
		WriteRegistryKeyStr(HKEY_CURRENT_USER, key_name, exe_path);
	} else {
		DeleteRegistryValue(HKEY_CURRENT_USER, key_name);
	}
	tray_update(&tray);
}

static void IncreaseBrightnessCallback(struct tray_menu* item)
{
	(void)item;
	tray_simulate_hottkey(hkIncreaseBrightness);
}

static void DecreaseBrightnessCallback(struct tray_menu* item)
{
	(void)item;
	tray_simulate_hottkey(hkDecreaseBrightness);
}

static void ExitCallback(struct tray_menu* item)
{
	(void)item;
	tray_exit();
}

// Callback for keyboard hotkeys
static bool HotkeyCallback(WPARAM wparam, LPARAM lparam)
{
	float delta = 0.0f;
	uint8_t input = 0, i;

	if (wparam < 0 || wparam >= hkMax)
		return false;
	switch (wparam) {
	case hkDecreaseBrightness:
	case hkDecreaseBrightness2:
		delta = -2.0f * settings.increment;
		[[fallthrough]];
	case hkIncreaseBrightness:
	case hkIncreaseBrightness2:
		if (display_list.size() >= 1) {
			delta += settings.increment;
			auto& display = GetDisplayAt(settings.active_display);
			display.ChangeBrightness(delta);
			display.UpdateGamma();
			display.SaveColorSettings();
			tray.icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_00 + GetIconIndex(display)));
			tray_update(&tray);
		}
		break;
	case hkPowerOffMonitor:
		SendMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
		break;
	case hkRestoreInput:
		if (display_list.size() >= 1) {
			auto& display = GetDisplayAt(settings.active_display);
			display.SetMonitorInput(VCP_INPUT_HOME);
		}
		break;
	case hkNextInput:
	case hkPreviousInput:
		if (display_list.size() >= 1) {
			auto& display = GetDisplayAt(settings.active_display);
			if (display.SupportsVCP()) {
				input = display.SetMonitorInput((wparam == hkNextInput) ? VCP_INPUT_NEXT : VCP_INPUT_PREVIOUS);
				if (input != 0)
					logger("Switched to input: %s\n", nvDisplay::InputToString(input));
				else
					logger("Failed to switch inputs\n");
			} else
				logger("Display %S does not support input switching\n", display.GetDisplayName());
		}
		break;
	case hkNextMonitor:
	case hkPreviousMonitor:
		if (display_list.size() >= 1) {
			settings.active_display += display_list.size() + (wparam == hkNextMonitor) ? 1 : -1;
			settings.active_display %= display_list.size();
			for (size_t i = 1; i <= display_list.size(); i++)
				submenu[i].checked = (i == settings.active_display + 1);
			WriteRegistryKey32(HKEY_CURRENT_USER, L"ActiveDisplay", settings.active_display);
			auto& display = GetDisplayAt(settings.active_display);
			logger("Active display: %S\n", display.GetDisplayName());
			tray.icon = LoadIcon(GetModuleHandle(NULL),	MAKEINTRESOURCE(IDI_ICON_00 + GetIconIndex(display)));
		}
		[[fallthrough]];
	case hkUpdateSubmenu:
		// This fake hotkey message is issued when we want to update the Input Control submenu
		if (display_list.size() >= 1) {
			if (tray.menu[4].submenu == NULL) {
				logger("CODE: Submenu index!\n");
				break;
			}
			for (i = 0; tray.menu[4].submenu[i].text != NULL; i++);
			if (i < 4) {
				logger("CODE: Submenu items!\n");
				break;
			}
			auto& display = GetDisplayAt(settings.active_display);
			// Set the enabled/disabled status
			tray.menu[4].submenu[i - 3].disabled = (display.GetHomeInput() == 0);
			tray.menu[4].submenu[i - 2].disabled = (display.GetNumberOfInputs() <= 1);
			tray.menu[4].submenu[i - 1].disabled = (display.GetNumberOfInputs() <= 1);
			// Update the menu entries to add/remove the input names
			if (display.GetHomeInput() == 0) {
				_snwprintf_s(home_input, ARRAYSIZE(home_input), _TRUNCATE, L"Home input\t［⊞］［Shift］［Home］");
				_snwprintf_s(wake_input, ARRAYSIZE(wake_input), _TRUNCATE, L"Wake up to home");
			} else {
				_snwprintf_s(home_input, ARRAYSIZE(home_input), _TRUNCATE,
					L"Home input  (%hs)\t［⊞］［Shift］［Home］", nvDisplay::InputToString(display.GetHomeInput()));
				_snwprintf_s(wake_input, ARRAYSIZE(wake_input), _TRUNCATE,
					L"Wake up to home (%hs)", nvDisplay::InputToString(display.GetHomeInput()));
			}
			if (display.GetNumberOfInputs() <= 1) {
				_snwprintf_s(next_input, ARRAYSIZE(next_input), _TRUNCATE, L"Next input\t［⊞］［Shift］［PgUp］");
				_snwprintf_s(prev_input, ARRAYSIZE(prev_input), _TRUNCATE, L"Prev input\t［⊞］［Shift］［PgDn］");
			} else {
				_snwprintf_s(next_input, ARRAYSIZE(next_input), _TRUNCATE,
					L"Next input    (%hs)\t［⊞］［Shift］［PgUp］", nvDisplay::InputToString(display.GetNextInput()));
				_snwprintf_s(prev_input, ARRAYSIZE(prev_input), _TRUNCATE,
					L"Prev input    (%hs)\t［⊞］［Shift］［PgDn］", nvDisplay::InputToString(display.GetPrevInput()));
			}
			tray_update(&tray);
		}
		break;
	default:
		logger("Unhandled Hot Key!\n");
		break;
	}

	return true;
}

// Callback for power events
static ULONG CALLBACK PowerEventCallback(PVOID Context, ULONG Type, PVOID Setting)
{
	if (!settings.enabled || !settings.resume_to_last_input)
		return 0;

	switch (Type) {
	case PBT_APMSUSPEND:
	case PBT_APMSTANDBY:
		logger("Suspending system - saving monitor inputs\n");
		// User may have switched inputs manually so save the current one
		for (auto& display : display_list)
			display.SaveHomeInput();
		break;
	case PBT_APMRESUMESUSPEND:
		logger("Resume from suspend - restoring monitor inputs\n");
		for (auto& display : display_list)
			display.SetMonitorInput(VCP_INPUT_HOME);
		break;
	default:
		break;
	}
	return 0;
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
		logger("NvAPI_Initialize: %d %s\n", r, NvAPI_GetErrorString(r));
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
	NvPhysicalGpuHandle gpu_handles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
	NvU32 gpu_count = 0;

	if (NvAPI_EnumPhysicalGPUs == NULL)
		return -1;
	r = NvAPI_EnumPhysicalGPUs(gpu_handles, &gpu_count);
	if (r != NVAPI_OK) {
		logger("NvAPI_EnumPhysicalGPUs: %d %s\n", r, NvAPI_GetErrorString(r));
		return -1;
	}

	return (int)gpu_count;
}

// I've said it before and I'll say it again:
// Retrieving versioning and file information on Windows is a COMPLETE SHIT SHOW!!!
#define GET_VERSION_INFO(name) do { _snwprintf_s(SubBlock, ARRAYSIZE(SubBlock), _TRUNCATE, \
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
	BOOL b;

	GetModuleFileName(NULL, exe_path, ARRAYSIZE(exe_path));
	size = GetFileVersionInfoSize(exe_path, &dummy);
	if (size == 0)
		return false;
	version.data = malloc(size);	// freed on app exit
	if (version.data == NULL)
		return false;

	if (!GetFileVersionInfo(exe_path, 0, size, version.data))
		return false;

	b = VerQueryValue(version.data, L"\\", (void**)&version.fixed, (PUINT)&size);
	if (!b || version.fixed == NULL || size != sizeof(VS_FIXEDFILEINFO))
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
	static wchar_t mutex_name[64];
	int ret = 1, icon_index = 20, i = 0;
	wchar_t key_name[128];
	bool enable_home = false;
	GUID guid = TRAY_ICON_GUID;
	HANDLE mutex = NULL, power_handle = NULL;
	DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS power_params;

	SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

	if (!PopulateVersionData()) {
		ProperMessageBox(TD_ERROR_ICON, L"No version information",
			L"Version information could not be read from the executable.");
		goto out;
	}

	_snwprintf_s(mutex_name, ARRAYSIZE(mutex_name), _TRUNCATE, L"Global/%s", version.ProductName);
	// No need to explicitly close/release the mutex
	// Per https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexa#remarks:
	// "The system closes the handle automatically when the process terminates."
	mutex = CreateMutex(NULL, TRUE, mutex_name);
	if ((mutex == NULL) || (GetLastError() == ERROR_ALREADY_EXISTS)) {
		ProperMessageBox(TD_ERROR_ICON, L"Other instance detected",
			L"An instance of %s is already running.\n", version.ProductName);
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
	nvDisplay::EnumerateDisplays(display_list);

	// Update settings
	settings.use_alternate_keys = (ReadRegistryKey32(HKEY_CURRENT_USER, L"UseAlternateKeys") != 0);
	settings.resume_to_last_input = (ReadRegistryKey32(HKEY_CURRENT_USER, L"ResumeToLastInput") != 0);
	_snwprintf_s(key_name, ARRAYSIZE(key_name), _TRUNCATE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run\\%s", version.ProductName);
	settings.autostart = (ReadRegistryKeyStr(HKEY_CURRENT_USER, key_name)[0] != 0);
	settings.active_display = ReadRegistryKey32(HKEY_CURRENT_USER, L"ActiveDisplay");
	// Make sure the active display index is within our list boundaries. If not, reset to 0.
	if (settings.active_display >= display_list.size())
		settings.active_display = 0;

	// Create the Input controls submenu
	submenu.push_back({ .text = L"Display to apply shortcuts:\t［⊞］［Shift］［,］ / ［.］" });
	// The active display selection assumes that Windows keeps the display order between sessions
	for (auto& display : display_list) {
		submenu.push_back({ .text = display.GetDisplayName(), .checked = (i == settings.active_display),
			.cb = ActiveDisplayCallback, .context = (void*)(uintptr_t)i });
		if (i == settings.active_display && display.GetHomeInput() != 0)
			enable_home = true;
		i++;
	}
	submenu.push_back({ .text = L"-" });
	submenu.push_back({ .text = home_input, .disabled = !enable_home, .cb = RestoreInputCallback });
	submenu.push_back({ .text = next_input, .disabled = true, });
	submenu.push_back({ .text = prev_input, .disabled = true });
	submenu.push_back({ .text = NULL });

	// Create the main menu
	static struct tray_menu menu[] = {
		{ .text = L"Brightness +\t［⊞］［Shift］［Num +］", .cb = IncreaseBrightnessCallback },
		{ .text = L"Brightness −\t［⊞］［Shift］［Num −］", .cb = DecreaseBrightnessCallback },
		{ .text = L"No signal\t［⊞］［Shift］［End］", .cb = PowerOffCallback },
		{ .text = L"-" },
		{ .text = L"Input controls", .submenu = submenu.data()},
		{ .text = wake_input, .disabled = (display_list.front().GetHomeInput() == 0),
			.checked = settings.resume_to_last_input, .cb = ResumeToLastInputCallback },
		{ .text = L"-" },
		{ .text = L"Auto Start", .checked = settings.autostart, .cb = AutoStartCallback },
		{ .text = L"Pause", .checked = 0, .cb = PauseCallback },
		{ .text = L"Use Internet keys", .checked = settings.use_alternate_keys, .cb = AlternateKeysCallback, },
		{ .text = L"About", .cb = AboutCallback },
		{ .text = L"-" },
		{ .text = L"Exit", .cb = ExitCallback },
		{ .text = NULL }
	};
	if (settings.use_alternate_keys) {
		menu[0].text = L"Brightness +\t［Internet Fwd］ or ［Alt］［→］";
		menu[1].text = L"Brightness −\t［Internet Back］ or ［Alt］［←］";
	}

	if (display_list.size() >= 1)
		icon_index = GetIconIndex(GetDisplayAt(settings.active_display));
	tray.icon =	LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_00 + icon_index));
	tray.menu = menu;

	if (tray_init(&tray, version.ProductName, guid, HotkeyCallback) < 0) {
		ProperMessageBox(TD_ERROR_ICON, L"Failed to create tray application",
			L"There was an error registering the tray application.\n"
			"%s will now exit.\n", version.ProductName);
		return 1;
	}

	// Register the keyboard shortcuts
	if (!RegisterHotKeys()) {
		ProperMessageBox(TD_WARNING_ICON, L"Failed to register keyboard shortcut",
			L"There was an error registering some of the keyboard shortcuts.\n"
			"%s is running but some of its shortcuts may not work.\n", version.ProductName);
	}

	// Register a callback for resume from sleep
	power_params.Callback = PowerEventCallback;
	power_params.Context = NULL;
	PowerRegisterSuspendResumeNotification(DEVICE_NOTIFY_CALLBACK, &power_params, &power_handle);

	// Process tray application messages
	while (tray_loop(1) == 0);

	ret = 0;

out:
	PowerUnregisterSuspendResumeNotification(power_handle);
	UnRegisterHotKeys();
	display_list.clear();
	NvExit();
	free(version.data);
#ifdef _DEBUG
	// NB: You don't want to use _CrtDumpMemoryLeaks() with C++.
	// See: https://stackoverflow.com/a/5266164/1069307
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	return ret;
}
