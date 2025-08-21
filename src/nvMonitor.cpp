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

#include "tray.h"
#include "nvapi.h"
#include "nvBrightness.h"
#include "nvMonitor.hpp"

#include <regex>
#include <sstream>

using namespace std::chrono;

static vector<string> SplitByWhitespace(const string& s)
{
	istringstream iss(s);
	vector<string> tokens;
	string token;
	while (iss >> token)
		tokens.push_back(token);
	return tokens;
}

// Using a C++ map would be nice and all, *if* C++ had maps
// that return a default value when a key is not found...
const char* InputToString(uint8_t input)
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
		auto elapsed = duration_cast<seconds>(steady_clock::now() - begin);
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
		auto elapsed = duration_cast<milliseconds>(steady_clock::now() - begin);
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
