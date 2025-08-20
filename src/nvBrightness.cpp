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
#include <initguid.h>
#include <ntddvdeo.h>
#include <powerbase.h>
#include <powrprof.h>
#include <shellscalingapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>

#include <string>
#include <iostream>
#include <iomanip>
#include <format>
#include <thread>
#include <list>
#include <regex>
#include <sstream>
#include <vector>
#include <chrono>

using std::string;
using std::wstring;
using std::list;
using std::format;
using std::thread;
using std::smatch;
using std::regex;
using std::vector;
using std::istringstream;
using std::chrono::steady_clock;
using std::chrono::seconds;

#include "resource.h"
#include "tray.h"
#include "nvapi.h"
#include "registry.h"
#include "DarkTaskDialog.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "version.lib")

// Using a GUID for the tray icon allows the executable to be moved around or change version
// without requiring the user to go through the taskbar settings to re-enable the icon.
#ifdef _DEBUG
// Note that we use two GUIDs due to a Microsoft UTTERLY MADDENING BUG, where Windows silently
// drops the tray icon, for some arcane reason that I really am NOT ready to waste DAYS on.
// And it only happens to some versions of the executable, independently of their location and
// REGARDLESS OF WHETHER THEY WERE RECOMPILED FROM THE SAME SOURCE AS UNAFFECTED EXECUTABLES!!!
// So, Microsoft, you can go screw yourselves on that one, as I have better things to do than
// chase after a stupid Windows bug/limitation/whatever.
#define TRAY_ICON_GUID { 0x64397973, 0xa694, 0x4640, { 0x80, 0x26, 0x96, 0x04, 0x46, 0x75, 0x00, 0x2c } }
#else
#define TRAY_ICON_GUID { 0x64397973, 0xa694, 0x4640, { 0x80, 0x26, 0x96, 0x04, 0x46, 0x75, 0x00, 0x2b } }
#endif

// VCP control code to read/switch a monitor's input source
#define VCP_INPUT_SOURCE            0x60

// Custom VCP input values for previous/next
#define VCP_INPUT_PREVIOUS          0xfe
#define VCP_INPUT_NEXT              0xff

// How long we may retry GetCapabilitiesStringLength(), in seconds
#define VCP_CAPS_MAX_RETRY_TIME     300

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

enum {
	hkDecreaseBrightness = 0,
	hkIncreaseBrightness,
	hkDecreaseBrightness2,
	hkIncreaseBrightness2,
	hkPowerOffMonitor,
	hkRestoreInput,
	hkPreviousInput,
	hkNextInput,
	hkRegisterHotkeys,
	hkMax
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
	bool autostart;
	bool use_alternate_keys;
	bool resume_to_last_input;
	float increment;
} settings_t;

// Classes
class nvMonitor {
	HMONITOR hMonitor = NULL;
	wchar_t displayName[sizeof(NvAPI_ShortString)] = { 0 };
	wchar_t deviceID[128] = { 0 };
	vector<PHYSICAL_MONITOR> physicalMonitors;
	vector<uint8_t> allowedInputs;
	uint8_t lastKnownInput = 0;
	string modelName = "Unknown";
	thread worker;
public:
	~nvMonitor();
	void InitializeMonitor(uint32_t displayId);
	uint8_t GetMonitorLastKnownInput() const { return lastKnownInput; };
	uint8_t GetMonitorInput();
	void SaveMonitorInput() { lastKnownInput = GetMonitorInput(); };
	uint8_t SetMonitorInput(uint8_t);
	PHYSICAL_MONITOR* GetFirstPhysicalMonitor() { return (physicalMonitors.size() == 0) ? NULL : &physicalMonitors[0]; };
	void GetMonitorAllowedInputs();
};

class nvDisplay {
	uint32_t displayId;
	wstring registryKeyString;
	float colorSetting[nvAttrMax][nvColorMax];
	nvMonitor monitor;
public:
	nvDisplay(uint32_t);
	uint8_t GetMonitorLastKnownInput() { return monitor.GetMonitorLastKnownInput(); };
	uint8_t GetMonitorInput() { return monitor.GetMonitorInput(); };
	void SaveMonitorInput() { monitor.SaveMonitorInput(); };
	uint8_t SetMonitorInput(uint8_t input) { return monitor.SetMonitorInput(input); };
	bool UpdateGamma();
	void ChangeBrightness(float delta);
	float GetBrightness();
	void SaveColorSettings(bool ApplyToAll);
	static size_t EnumerateDisplays();
};

// Globals
wchar_t *APPLICATION_NAME = NULL, *COMPANY_NAME = NULL;	// Needed for registry.h
static version_t version = { 0 };
static settings_t settings = { true, false, false, false, 0.5f };
static struct tray tray = { 0 };
static list<nvDisplay> displayList;
static bool input_switching_supported = false, cancel_thread = false;

// Logging
static void logger(const char* format, ...)
{
	static char log_msg[512];

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

// Using a C++ map would be nice and all, *if* C++ had maps
// that return a default value when a key is not found...
static const char* InputToString(uint8_t input)
{
	switch (input) {
	case 0x01: return "VGA 1";
	case 0x02: return "VGA 2";
	case 0x03: return "DVI 1";
	case 0x04: return "DVI 2";
	case 0x05: return "Composite 1";
	case 0x06: return "Composite 2";
	case 0x07: return "S-Video 1";
	case 0x08: return "S-Video 2";
	case 0x09: return "Tuner 1";
	case 0x0a: return "Tuner 2";
	case 0x0b: return "Tuner 3";
	case 0x0c: return "Component 1";
	case 0x0d: return "Component 2";
	case 0x0e: return "Component 3";
	case 0x0f: return "DP 1";
	case 0x10: return "DP 2";
	case 0x11: return "HDMI 1";
	case 0x12: return "HDMI 2";
	// Yeah, someone, SOMEWHERE, has this info, but they are hoarding
	// it to themselves. So have fun dealing with an educated guess,
	// that's going to pollute the internet forever as it becomes the
	// prime reference.
	// That'll teach you NOT to disclose what SHOULD be public data!
	case 0x13: return "HDMI 3";
	case 0x14: return "HDMI 4";
	case 0x15: return "Thunderbolt 1";
	case 0x16: return "Thunderbolt 2";
	case 0x17: return "USB-C 1";
	case 0x18: return "USB-C 2";
	case 0x19: return "HDMI over USB-C 1";
	case 0x1a: return "HDMI over USB-C 2";
	case 0x1b: return "DP over USB-C 1";
	case 0x1c: return "DP over USB-C 2";
	default: return "Unknown";
	}
};

static vector<string> SplitByWhitespace(const string& s)
{
	istringstream iss(s);
	vector<string> tokens;
	string token;
	while (iss >> token)
		tokens.push_back(token);
	return tokens;
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
	if (input_switching_supported) {
		b &= tray_register_hotkey(hkNextInput, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, VK_OEM_PERIOD);
		b &= tray_register_hotkey(hkPreviousInput, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, VK_OEM_COMMA);
	}

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
		b &= tray_register_hotkey(hkIncreaseBrightness, MOD_WIN | MOD_SHIFT, VK_PRIOR);
		b &= tray_register_hotkey(hkDecreaseBrightness, MOD_WIN | MOD_SHIFT, VK_NEXT);
	}
	return b;
}

// nvMonitor methods

// Ideally, this would be the constructor method, but because C++ is dumb when it comes to
// passing the correct instance of an object, when starting a thread from the constructor
// of an object that isn't the default constructor, we have to use a different method.
void nvMonitor::InitializeMonitor(uint32_t displayId)
{
	NvAPI_Status r;
	NvAPI_ShortString nvDisplayName;
	NvDisplayHandle displayHandle;
	DWORD numPhysicalMonitors;
	DISPLAY_DEVICE displayDevice{ .cb = sizeof(DISPLAY_DEVICE) }, monitorDevice{ .cb = sizeof(DISPLAY_DEVICE) };

	// Finish initializing our instance
	hMonitor = NULL;
	lastKnownInput = 0;
	memset(displayName, 0, sizeof(displayName));
	memset(deviceID, 0, sizeof(deviceID));

	// Get the Windows display name
	r = NvAPI_DISP_GetDisplayHandleFromDisplayId(displayId, &displayHandle);
	if (r != NVAPI_OK) {
		logger("NvAPI_DISP_GetDisplayHandleFromDisplayId(0x%08x): %d %s\n", displayId, r, NvErrStr(r));
		return;
	}
	r = NvAPI_GetAssociatedNvidiaDisplayName(displayHandle, nvDisplayName);
	if (r != NVAPI_OK) {
		logger("NvAPI_GetAssociatedNvidiaDisplayName(0x%08x): %d %s\n", displayId, r, NvErrStr(r));
		return;
	}

	for (auto i = 0; i < sizeof(nvDisplayName); i++)
		displayName[i] = nvDisplayName[i];

	// Get the physical HMONITOR handle associated with the display
	for (auto i = 0; EnumDisplayDevices(NULL, i, &displayDevice, 0); i++) {
		if (_wcsicmp(displayDevice.DeviceName, displayName) != 0)
			continue;
		for (auto j = 0; EnumDisplayDevices(displayName, j, &monitorDevice, EDD_GET_DEVICE_INTERFACE_NAME); ++j) {
			if (monitorDevice.StateFlags & DISPLAY_DEVICE_ACTIVE) {
				DEVMODE devMode{ .dmSize = sizeof(devMode) };
				if (EnumDisplaySettings(displayName, ENUM_CURRENT_SETTINGS, &devMode) != FALSE) {
					// https://stackoverflow.com/a/38380281/1069307
					BOOL b = EnumDisplayMonitors(NULL, NULL,
						[](HMONITOR hMonitor, HDC hDC, LPRECT rc, LPARAM data) -> BOOL {
							auto& monitorData = *reinterpret_cast<nvMonitor*>(data);
							MONITORINFOEX monitorInfo;
							monitorInfo.cbSize = sizeof(monitorInfo);
							if (GetMonitorInfo(hMonitor, &monitorInfo) &&
								_wcsicmp(monitorInfo.szDevice, monitorData.displayName) == 0) {
								monitorData.hMonitor = hMonitor;
								return TRUE;
							}
							return FALSE;
						},
						reinterpret_cast<LPARAM>(this));
					if (b && hMonitor != NULL) {
						wcscpy_s(deviceID, ARRAYSIZE(deviceID), monitorDevice.DeviceID);
						goto have_physical_handle;
					}
				}
			}
		}
	}
	return;

have_physical_handle:
	// With the physical monitor handle, we can look at its VCP features
	if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &numPhysicalMonitors))
		return;

	physicalMonitors.resize(numPhysicalMonitors);

	if (!GetPhysicalMonitorsFromHMONITOR(hMonitor, numPhysicalMonitors, physicalMonitors.data()))
		physicalMonitors.resize(0);

	auto physicalMonitor = GetFirstPhysicalMonitor();
	if (physicalMonitor != NULL) {
		DWORD i, input = 0, max = 0;
		for (i = 0; i < 10 && !GetVCPFeatureAndVCPFeatureReply(physicalMonitor->hPhysicalMonitor, VCP_INPUT_SOURCE, NULL, &input, &max); i++)
			Sleep(100);
		if (i == 10)
			logger("Could not retrieve monitor input: error 0x%X\n", GetLastError());
		lastKnownInput = (uint8_t)input;
		if (lastKnownInput != 0x00) {
			// If we didn't get the current input, the monitor is unlikely to honour VCP
			logger("Current monitor input: %s\n", InputToString(lastKnownInput));
			worker = thread(&nvMonitor::GetMonitorAllowedInputs, this);
			worker.detach();
		}
	} else {
		lastKnownInput = 0;
	}
}

nvMonitor::~nvMonitor()
{
	if (worker.joinable())
		worker.join();
	DestroyPhysicalMonitors((DWORD)physicalMonitors.size(), physicalMonitors.data());
}

// Issuing CapabilitiesRequestAndCapabilitiesReply() can be a lengthy process and may need
// to be reiterated multiple times before we get a valid answer. So use a thread.
void nvMonitor::GetMonitorAllowedInputs()
{
	char* szCapabilitiesString = NULL;
	DWORD i = 1, size = 0;

	auto physicalMonitor = GetFirstPhysicalMonitor();
	if (physicalMonitor == NULL)
		return;

	// GetCapabilitiesStringLength() is *VERY* temperamental, so we retry up to VCP_CAPS_MAX_RETRY_TIME
	steady_clock::time_point begin = steady_clock::now();
	for (i = 1; !GetCapabilitiesStringLength(physicalMonitor->hPhysicalMonitor, &size); i++) {
		if (cancel_thread)
			return;
		auto elapsed = std::chrono::duration_cast<seconds>(steady_clock::now() - begin);
		if (elapsed.count() > VCP_CAPS_MAX_RETRY_TIME) {
			logger("failed to get VCP capabilities after %d attempts: %x\n", i, GetLastError());
			return;
		}
	}

	// If GetCapabilitiesStringLength() succeeded then the subsequent call to
	// CapabilitiesRequestAndCapabilitiesReply() usually doesn't fail, so no need for retries there.
	szCapabilitiesString = (char*)malloc(size);
	if (szCapabilitiesString == NULL)
		goto out;

	if (CapabilitiesRequestAndCapabilitiesReply(physicalMonitor->hPhysicalMonitor, szCapabilitiesString, size)) {
		string capabilities = szCapabilitiesString;
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(steady_clock::now() - begin);
		logger("Retrieved monitor VCP capabilities in %u.%03u seconds (%d %s)\n",
			(unsigned)(elapsed.count() / 1000), (unsigned)(elapsed.count() % 1000), i, (i == 1) ? "try" : "tries");

		// Get the model name while we're here
		regex modelRegex(R"(model\(([^)]+)\))");
		smatch match;
		if (regex_search(capabilities, match, modelRegex))
			modelName = match[1];

		// Get the allowed inputs for VCP code 0x60
		regex inputsRegex(R"(60\(([^)]*)\))");
		if (regex_search(capabilities, match, inputsRegex)) {
			string inner = match[1];
			auto tokens = SplitByWhitespace(inner);
			for (auto& t : tokens) {
				try {
					allowedInputs.push_back((uint8_t)stoi(t, nullptr, 16));
				}
				catch (...) {
					logger("Invalid monitor input value found in %s\n", inner.c_str());
				}
			}
		}
		// No points in adding input switching if there's only one
		if (!input_switching_supported && allowedInputs.size() > 1) {
			// Wait for the menu to have been created if that's not the case
			tray.menu[4].disabled = false;
			tray.menu[5].disabled = false;
			tray_update(&tray);
			input_switching_supported = true;
			// So, the problem with Windows hot keys is that they are registered for a specific
			// thread rather than globally. Which means that if we just call RegisterHotKeys()
			// from this thread, we are going to have an issue.
			// Long story short, we simulate a fake hotkey press, to re-register the hotkeys.
			tray_simulate_hottkey(hkRegisterHotkeys);
		}

		string separator, inputs;
		for (const auto& input : allowedInputs) {
			inputs += separator + InputToString(input);
			separator = ", ";
		}
		logger("%s valid input(s): %s\n", modelName.c_str(), inputs.c_str());
	} else {
		logger("Could not get monitor VCP capabilities: %x\n", GetLastError());
	}

out:
	free(szCapabilitiesString);
}

uint8_t nvMonitor::GetMonitorInput()
{
	DWORD i, current = 0, max = 0;
	auto physicalMonitor = GetFirstPhysicalMonitor();
	if (physicalMonitor == NULL)
		return 0;

	// Read the current input (with a few retries)
	for (i = 0; i < 10 && !GetVCPFeatureAndVCPFeatureReply(physicalMonitor->hPhysicalMonitor, VCP_INPUT_SOURCE, NULL, &current, &max); i++)
		Sleep(100);
	if (i == 10 || (uint8_t)lastKnownInput == 0) {
		logger("Could not get current input: error %X\n", GetLastError());
		return 0;
	}
	return (uint8_t)current;
}

uint8_t nvMonitor::SetMonitorInput(uint8_t requested)
{
	uint8_t ret = 0, current = GetMonitorInput();

	auto physicalMonitor = GetFirstPhysicalMonitor();
	if (physicalMonitor == NULL)
		return 0;

	// If input is 0, reselect the last known input
	if (requested == 0)
		requested = lastKnownInput;
	if (current == 0 || requested == 0)
		return 0;

	if (requested == VCP_INPUT_NEXT || requested == VCP_INPUT_PREVIOUS) {
		auto pos = lower_bound(allowedInputs.begin(), allowedInputs.end(), current) - allowedInputs.begin();
		pos += allowedInputs.size();	// Prevent an underflow when subtracting -1 from 0
		pos = (pos + ((requested == VCP_INPUT_NEXT) ? +1 : -1)) % allowedInputs.size();
		requested = allowedInputs.at(pos);
	}

	if (current == requested) {
		logger("Current monitor input is the same as requested - not switching inputs\n");
		ret = requested;
	} else {
		if (!SetVCPFeature(physicalMonitor->hPhysicalMonitor, VCP_INPUT_SOURCE, requested))
			logger("Could not set input: error %X\n", GetLastError());
		else
			ret = requested;
	}
	lastKnownInput = ret;

	return ret;
}

// nvDisplay methods
nvDisplay::nvDisplay(uint32_t displayId)
{
	GUID guid = { 0 };
	NvAPI_Status r;

	this->displayId = displayId;
	monitor.InitializeMonitor(displayId);

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
				displayList.emplace_back(displayIds[j].displayId);
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
	wchar_t szTitle[64] = L"";
	wchar_t szHeader[128] = L"";
	wchar_t szFooter[128] = L"";
	wchar_t szProject[128] = L"";
	wchar_t szReleaseUrl[64] = L"";

	(void)item;

	swprintf_s(szTitle, ARRAYSIZE(szTitle), L"About %s", version.ProductName);
	swprintf_s(szHeader, ARRAYSIZE(szHeader), L"%s v%d.%d", version.ProductName,
		version.fixed->dwProductVersionMS >> 16, version.fixed->dwProductVersionMS & 0xffff);
	const wchar_t* szContent = L"Increase/decrease display brightness using nVidia controls.";
	swprintf_s(szFooter, ARRAYSIZE(szFooter), L"%s, <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GPLv3</a>",
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
	RegisterHotKeys();
	item->checked = !item->checked;
	WriteRegistryKey32(HKEY_CURRENT_USER, L"UseAlternateKeys", item->checked);
	if (settings.use_alternate_keys) {
		tray.menu[0].text = L"Brightness +\t［Internet Fwd］ or ［Alt］［→］";
		tray.menu[1].text = L"Brightness −\t［Internet Back］ or ［Alt］［←］";
	} else {
		tray.menu[0].text = L"Brightness +\t［⊞］［Shift］［PgUp］";
		tray.menu[1].text = L"Brightness −\t［⊞］［Shift］［PgDn］";
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

static void AutoStartCallback(struct tray_menu* item)
{
	wchar_t key_name[128], exe_path[MAX_PATH + 2] = { 0 };

	settings.autostart = !settings.autostart;
	item->checked = !item->checked;
	swprintf_s(key_name, ARRAYSIZE(key_name), L"Software\\Microsoft\\Windows\\CurrentVersion\\Run\\%s", version.ProductName);
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
	uint8_t input = 0;

	if (wparam < 0 || wparam >= hkMax)
		return false;
	switch (wparam) {
	case hkDecreaseBrightness:
	case hkDecreaseBrightness2:
		delta = -2.0f * settings.increment;
		[[fallthrough]];
	case hkIncreaseBrightness:
	case hkIncreaseBrightness2:
		delta += settings.increment;

		for (auto& display : displayList) {
			display.ChangeBrightness(delta);
			display.UpdateGamma();
			display.SaveColorSettings(false);
		}
		if (displayList.size() >= 1) {
			tray.icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_00 + GetIconIndex(displayList.front())));
			tray_update(&tray);
		}
		break;
	case hkPowerOffMonitor:
		SendMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
		break;
	case hkRestoreInput:
		// Apply to all displays
		for (auto& display : displayList)
			display.SetMonitorInput(0);
		break;
	case hkNextInput:
	case hkPreviousInput:
		// Only apply to first display
		input = displayList.front().SetMonitorInput((wparam == hkNextInput) ? VCP_INPUT_NEXT : VCP_INPUT_PREVIOUS);
		logger("Switched to input: %s\n", InputToString(input));
		break;
	case hkRegisterHotkeys:
		RegisterHotKeys();
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
		for (auto& display : displayList)
			display.SaveMonitorInput();
		break;
	case PBT_APMRESUMESUSPEND:
		logger("Resume from suspend - restoring monitor inputs\n");
		for (auto& display : displayList)
			display.SetMonitorInput(0);
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
	static wchar_t mutex_name[64];
	int ret = 1, icon_index = 20;
	wchar_t key_name[128];
	GUID guid = TRAY_ICON_GUID;
	HANDLE mutex = NULL, power_handle = NULL;
	DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS power_params;

	SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

	if (!PopulateVersionData()) {
		ProperMessageBox(TD_ERROR_ICON, L"No version information",
			L"Version information could not be read from the executable.");
		goto out;
	}

	swprintf_s(mutex_name, ARRAYSIZE(mutex_name), L"Global/%s", version.ProductName);
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

	// Update settings
	settings.use_alternate_keys = (ReadRegistryKey32(HKEY_CURRENT_USER, L"UseAlternateKeys") != 0);
	settings.resume_to_last_input = (ReadRegistryKey32(HKEY_CURRENT_USER, L"ResumeToLastInput") != 0);
	swprintf_s(key_name, ARRAYSIZE(key_name), L"Software\\Microsoft\\Windows\\CurrentVersion\\Run\\%s", version.ProductName);
	settings.autostart = (ReadRegistryKeyStr(HKEY_CURRENT_USER, key_name)[0] != 0);

	// Build the display list
	nvDisplay::EnumerateDisplays();

	// Create the tray menu
	static struct tray_menu menu[] = {
		{ .text = L"Brightness +\t［⊞］［Shift］［PgUp］", .cb = IncreaseBrightnessCallback },
		{ .text = L"Brightness −\t［⊞］［Shift］［PgDn］", .cb = DecreaseBrightnessCallback },
		{ .text = L"Power off display\t［⊞］［Shift］［End］", .cb = PowerOffCallback },
		{ .text = L"Reselect monitor input\t［⊞］［Shift］［Home］", .disabled = (displayList.front().GetMonitorLastKnownInput() == 0),
			.cb = RestoreInputCallback },
		{ .text = L"Next monitor input\t［⊞］［Shift］［.］", .disabled = true, },
		{ .text = L"Previous monitor input\t［⊞］［Shift］［,］", .disabled = true },
		{ .text = L"-" },
		{ .text = L"Auto Start", .checked = settings.autostart, .cb = AutoStartCallback },
		{ .text = L"Pause", .checked = 0, .cb = PauseCallback },
		{ .text = L"Use Internet keys", .checked = settings.use_alternate_keys, .cb = AlternateKeysCallback, },
		{ .text = L"Reselect input after sleep", .disabled = (displayList.front().GetMonitorLastKnownInput() == 0),
			.checked = settings.resume_to_last_input, .cb = ResumeToLastInputCallback },
		{ .text = L"About", .cb = AboutCallback },
		{ .text = L"-" },
		{ .text = L"Exit", .cb = ExitCallback },
		{ .text = NULL }
	};
	if (settings.use_alternate_keys) {
		menu[0].text = L"Brightness +\t［Internet Fwd］ or ［Alt］［→］";
		menu[1].text = L"Brightness −\t［Internet Back］ or ［Alt］［←］";
	}

	if (displayList.size() >= 1)
		icon_index = GetIconIndex(displayList.front());
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
	cancel_thread = true;
	PowerUnregisterSuspendResumeNotification(power_handle);
	UnRegisterHotKeys();
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
