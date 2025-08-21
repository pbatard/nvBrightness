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
const char* nvMonitor::InputToString(uint8_t input)
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

// Ideally, this would be the constructor method, but because C++ is dumb when it comes to
// passing the correct instance of an object, when starting a thread from the constructor
// of an object that isn't the default constructor, we have to use a different method.
void nvMonitor::InitializeMonitor(uint32_t display_id)
{
	NvAPI_Status r;
	NvAPI_ShortString nv_display_name;
	NvDisplayHandle display_handle;
	DWORD num_physical_monitors;
	DISPLAY_DEVICE display_device{ .cb = sizeof(DISPLAY_DEVICE) }, monitor_device{ .cb = sizeof(DISPLAY_DEVICE) };

	// Finish initializing our instance
	monitor_handle = NULL;
	last_known_input = 0;
	memset(display_name, 0, sizeof(display_name));
	memset(device_id, 0, sizeof(device_id));

	// Get the Windows display name
	r = NvAPI_DISP_GetDisplayHandleFromDisplayId(display_id, &display_handle);
	if (r != NVAPI_OK) {
		logger("NvAPI_DISP_GetDisplayHandleFromDisplayId(0x%08x): %d %s\n", display_id, r, NvAPI_GetErrorString(r));
		return;
	}
	r = NvAPI_GetAssociatedNvidiaDisplayName(display_handle, nv_display_name);
	if (r != NVAPI_OK) {
		logger("NvAPI_GetAssociatedNvidiaDisplayName(0x%08x): %d %s\n", display_id, r, NvAPI_GetErrorString(r));
		return;
	}

	for (auto i = 0; i < sizeof(nv_display_name); i++)
		display_name[i] = nv_display_name[i];

	// Get the physical HMONITOR handle associated with the display
	for (auto i = 0; EnumDisplayDevices(NULL, i, &display_device, 0); i++) {
		if (_wcsicmp(display_device.DeviceName, display_name) != 0)
			continue;
		for (auto j = 0; EnumDisplayDevices(display_name, j, &monitor_device, EDD_GET_DEVICE_INTERFACE_NAME); ++j) {
			if (monitor_device.StateFlags & DISPLAY_DEVICE_ACTIVE) {
				DEVMODE dev_mode{ .dmSize = sizeof(dev_mode) };
				if (EnumDisplaySettings(display_name, ENUM_CURRENT_SETTINGS, &dev_mode) != FALSE) {
					// https://stackoverflow.com/a/38380281/1069307
					BOOL b = EnumDisplayMonitors(NULL, NULL,
						[](HMONITOR monitor_handle, HDC hDC, LPRECT rc, LPARAM data) -> BOOL {
							auto& monitor_data = *reinterpret_cast<nvMonitor*>(data);
							MONITORINFOEX monitor_info;
							monitor_info.cbSize = sizeof(monitor_info);
							if (GetMonitorInfo(monitor_handle, &monitor_info) &&
								_wcsicmp(monitor_info.szDevice, monitor_data.display_name) == 0) {
								monitor_data.monitor_handle = monitor_handle;
								return TRUE;
							}
							return FALSE;
						},
						reinterpret_cast<LPARAM>(this));
					if (b && monitor_handle != NULL) {
						wcscpy_s(device_id, ARRAYSIZE(device_id), monitor_device.DeviceID);
						goto have_physical_handle;
					}
				}
			}
		}
	}
	return;

have_physical_handle:
	// With the physical monitor handle, we can look at its VCP features
	if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor_handle, &num_physical_monitors))
		return;

	physical_monitors.resize(num_physical_monitors);

	if (!GetPhysicalMonitorsFromHMONITOR(monitor_handle, num_physical_monitors, physical_monitors.data()))
		physical_monitors.resize(0);

	auto physical_monitor = GetFirstPhysicalMonitor();
	if (physical_monitor != NULL) {
		DWORD input = 0, max = 0;
		steady_clock::time_point begin = steady_clock::now();
		while (!GetVCPFeatureAndVCPFeatureReply(physical_monitor->hPhysicalMonitor, VCP_INPUT_SOURCE, NULL, &input, &max)) {
			auto elapsed = duration_cast<milliseconds>(steady_clock::now() - begin);
			if (elapsed.count() > VCP_FEATURE_MAX_RETRY_TIME) {
				logger("Could not retrieve monitor input: error 0x%X\n", GetLastError());
				return;
			}
		}
		last_known_input = (uint8_t)input;
		if (last_known_input != 0) {
			// If we could read the current input, we assume that VCP is supported
			supports_vcp = true;
			logger("Current monitor input: %s\n", InputToString(last_known_input));
			worker_thread = thread(&nvMonitor::GetMonitorAllowedInputs, this);
			worker_thread.detach();
		}
	}
}

nvMonitor::~nvMonitor()
{
	cancel_worker_thread = true;
	if (worker_thread.joinable())
		worker_thread.join();
	DestroyPhysicalMonitors((DWORD)physical_monitors.size(), physical_monitors.data());
}

// Issuing CapabilitiesRequestAndCapabilitiesReply() can be a lengthy process and may need
// to be reiterated multiple times before we get a valid answer. So use a thread.
void nvMonitor::GetMonitorAllowedInputs()
{
	char* capabilities_string = NULL;
	DWORD i = 1, size = 0;

	auto physical_monitor = GetFirstPhysicalMonitor();
	if (physical_monitor == NULL)
		return;

	// GetCapabilitiesStringLength() is *VERY* temperamental, so we retry up to VCP_CAPS_MAX_RETRY_TIME
	steady_clock::time_point begin = steady_clock::now();
	for (i = 1; !GetCapabilitiesStringLength(physical_monitor->hPhysicalMonitor, &size); i++) {
		if (cancel_worker_thread)
			return;
		auto elapsed = duration_cast<seconds>(steady_clock::now() - begin);
		if (elapsed.count() > VCP_CAPS_MAX_RETRY_TIME) {
			logger("failed to get VCP capabilities after %d attempts: %x\n", i, GetLastError());
			return;
		}
	}

	// If GetCapabilitiesStringLength() succeeded then the subsequent call to
	// CapabilitiesRequestAndCapabilitiesReply() usually doesn't fail, so no need for retries there.
	capabilities_string = (char*)malloc(size);
	if (capabilities_string == NULL)
		goto out;

	if (CapabilitiesRequestAndCapabilitiesReply(physical_monitor->hPhysicalMonitor, capabilities_string, size)) {
		if (cancel_worker_thread)
			goto out;
		string capabilities = capabilities_string;
		auto elapsed = duration_cast<milliseconds>(steady_clock::now() - begin);
		logger("Retrieved monitor VCP capabilities in %u.%03u seconds (%d %s)\n",
			(unsigned)(elapsed.count() / 1000), (unsigned)(elapsed.count() % 1000), i, (i == 1) ? "try" : "tries");

		// Get the model name while we're here
		regex model_regex(R"(model\(([^)]+)\))");
		smatch match;
		if (regex_search(capabilities, match, model_regex))
			model_name = match[1];

		// Get the allowed inputs for VCP code 0x60
		regex inputs_regex(R"(60\(([^)]*)\))");
		if (regex_search(capabilities, match, inputs_regex)) {
			string inner = match[1];
			auto tokens = SplitByWhitespace(inner);
			for (auto& t : tokens) {
				try {
					allowed_inputs.push_back((uint8_t)stoi(t, NULL, 16));
				} catch (...) {
					logger("Invalid monitor input value found in %s\n", inner.c_str());
				}
			}
		}
		// No points in allowing input switching if there's only one
		if (allowed_inputs.size() > 1 && tray.menu[4].disabled) {
			tray.menu[4].disabled = false;
			tray.menu[5].disabled = false;
			tray_update(&tray);
			// So, the problem with Windows hot keys is that they are registered for a specific
			// thread rather than globally. Which means that if we just call RegisterHotKeys()
			// from this thread, we are going to have an issue.
			// Long story short, we simulate a fake hotkey press, to re-register the hotkeys.
			tray_simulate_hottkey(hkRegisterHotkeys);
		}

		string separator, inputs;
		for (const auto& input : allowed_inputs) {
			inputs += separator + InputToString(input);
			separator = ", ";
		}
		logger("%s valid input(s): %s\n", model_name.c_str(), inputs.c_str());
	} else {
		logger("Could not get monitor VCP capabilities: %x\n", GetLastError());
	}

out:
	free(capabilities_string);
}

uint8_t nvMonitor::GetMonitorInput()
{
	DWORD current = 0, max = 0;
	auto physical_monitor = GetFirstPhysicalMonitor();
	if (physical_monitor == NULL)
		return 0;

	// Read the current input (with a few retries)
	steady_clock::time_point begin = steady_clock::now();
	while (!GetVCPFeatureAndVCPFeatureReply(physical_monitor->hPhysicalMonitor, VCP_INPUT_SOURCE, NULL, &current, &max)) {
		auto elapsed = duration_cast<milliseconds>(steady_clock::now() - begin);
		if (elapsed.count() > VCP_FEATURE_MAX_RETRY_TIME) {
			logger("Could not get current input: error %X\n", GetLastError());
			return 0;
		}
	}

	return (uint8_t)current;
}

uint8_t nvMonitor::SetMonitorInput(uint8_t requested)
{
	uint8_t ret = 0, current = GetMonitorInput();

	auto physical_monitor = GetFirstPhysicalMonitor();
	if (physical_monitor == NULL)
		return 0;

	// If input is 0, reselect the last known input
	if (requested == 0)
		requested = last_known_input;
	if (current == 0 || requested == 0)
		return 0;

	if (requested == VCP_INPUT_NEXT || requested == VCP_INPUT_PREVIOUS) {
		if (allowed_inputs.size() == 0)
			return 0;
		auto pos = lower_bound(allowed_inputs.begin(), allowed_inputs.end(), current) - allowed_inputs.begin();
		pos += allowed_inputs.size();	// Prevent an underflow when subtracting -1 from 0
		pos = (pos + ((requested == VCP_INPUT_NEXT) ? +1 : -1)) % allowed_inputs.size();
		requested = allowed_inputs.at(pos);
	}

	if (current == requested) {
		logger("Current monitor input is the same as requested - not switching inputs\n");
		ret = requested;
	} else {
		if (!SetVCPFeature(physical_monitor->hPhysicalMonitor, VCP_INPUT_SOURCE, requested))
			logger("Could not set input: error %X\n", GetLastError());
		else
			ret = requested;
	}
	last_known_input = ret;

	return ret;
}
