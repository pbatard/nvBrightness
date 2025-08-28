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
#include "registry.h"
#include "nvBrightness.h"
#include "nvMonitor.hpp"
#include "vendors.hpp"

#include <regex>
#include <format>
#include <sstream>

#pragma comment(lib, "dxva2.lib")

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

nvMonitor::nvMonitor(uint32_t display_id)
{
	NvAPI_Status r;
	NvAPI_ShortString nv_display_name;
	NvDisplayHandle display_handle;
	DWORD num_physical_monitors;
	DISPLAY_DEVICE display_device{ .cb = sizeof(DISPLAY_DEVICE) }, monitor_device{ .cb = sizeof(DISPLAY_DEVICE) };

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
								_wcsicmp(monitor_info.szDevice, monitor_data.display_name) == 0)
								monitor_data.monitor_handle = monitor_handle;
							return TRUE;
						},
						reinterpret_cast<LPARAM>(this));
					if (b && monitor_handle != NULL) {
						wcscpy_s(device_name, ARRAYSIZE(device_name), monitor_device.DeviceString);
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

	if (physical_monitors.size() != 0) {
		DWORD input = 0, max = 0;
		steady_clock::time_point begin = steady_clock::now();
		while (!GetVCPFeatureAndVCPFeatureReply(physical_monitors.at(0).hPhysicalMonitor, VCP_INPUT_SOURCE, NULL, &input, &max)) {
			auto elapsed = duration_cast<milliseconds>(steady_clock::now() - begin);
			if (elapsed.count() > VCP_FEATURE_MAX_RETRY_TIME) {
				logger("Could not retrieve monitor input: error 0x%X\n", GetLastError());
				goto parse_edid;
			}
		}
		// Store the "home" input, i.e. the input the monitor was using when we started the app
		home_input = (uint8_t)input;
		if (home_input != 0) {
			// If we could read the current input, we assume that VCP is supported
			supports_vcp = true;
			// Start a an asynchronous task to get this monitor's available inputs
			allowed_inputs_task = async(launch::async, &nvMonitor::GetAllowedInputs, this);
		}
	}

parse_edid:
	ParseEdid();
}

nvMonitor::~nvMonitor()
{
	cancel_allowed_inputs_task = true;
	// Wait for the GetAllowedInputs() task to finish
	if (allowed_inputs_task.valid())
		allowed_inputs_task.get();
	DestroyPhysicalMonitors((DWORD)physical_monitors.size(), physical_monitors.data());
}

bool nvMonitor::ParseEdid()
{
	size_t k, edid_size;
	wchar_t edid_registry_path[150] = L"SYSTEM\\CurrentControlSet\\Enum";

	// We *could* do the whole SetupDi enumeration & lookup dance... Or we can just craft
	// the registry path for the EDID ourselves, since it amounts to the same thing.
	wcscat_s(edid_registry_path, ARRAYSIZE(edid_registry_path), &device_id[3]);
	for (k = wcslen(edid_registry_path); k > 0 && edid_registry_path[k] != L'#'; k--);
	edid_registry_path[k] = L'\0';
	wcscat_s(edid_registry_path, ARRAYSIZE(edid_registry_path), L"\\Device Parameters\\EDID");
	for (k = 0; k < wcslen(edid_registry_path); k++)
		if (edid_registry_path[k] == L'#')
			edid_registry_path[k] = L'\\';
	edid_size = (size_t)GetRegistryKeySize(HKEY_LOCAL_MACHINE, edid_registry_path, REG_BINARY);
	if (edid_size == 0) {
		logger("Failed to read EDID for %S\n", device_id);
		return false;
	}
	uint8_t* edid = (uint8_t*)malloc(edid_size);
	if (edid == NULL || !GetRegistryKey(HKEY_LOCAL_MACHINE, edid_registry_path, REG_BINARY, edid, (DWORD)edid_size))
		return false;
	char model[14] = "", serial[14] = "";
	vendor_code = (edid[8] << 8) | edid[9];	// Big endian
	product_code = ((uint16_t*)edid)[5];		// Little endian. Great consistency there!
	serial_number = format("{:08x}", ((uint32_t*)edid)[3]);
	// If the serial is alphanum, print it as alphanum (little endian, which is what Dell uses)
	if (isalnum(edid[12]) && isalnum(edid[13]) && isalnum(edid[14]) && isalnum(edid[15]))
		serial_number = format("{:c}{:c}{:c}{:c}", edid[15], edid[14], edid[13], edid[12]);
	mfg_date = format("{:d}", edid[17] + 1990);
	if (edid[16] != 0)
		mfg_date += format("Q{:d}", (edid[16] / 13) + 1);

	for (auto i = 0; i < 4; i++) {
		// EDID detailed timing descriptors start at offset 54, four blocks of 18 bytes
		const uint8_t* desc = edid + 54 + i * 18;
		if (desc[0] != 0x00 || desc[1] != 0x00 || desc[2] != 0x00)
			continue;
		switch (desc[3]) {
		case 0xfc:
			memcpy(model, desc + 5, 13);
			model[13] = '\0';
			for (auto j = strlen(model) - 1; j >= 0 && isspace((unsigned char)model[j]); j--)
				model[j] = '\0';
			break;
		case 0xff:
			memcpy(serial, desc + 5, 13);
			serial[13] = '\0';
			for (auto j = (int)strlen(serial) - 1; j >= 0 && isspace((unsigned char)serial[j]); j--)
				serial[j] = '\0';
			serial_number += "/";
			serial_number += serial;
			break;
		default:
			break;
		}
	}

	vendor_name = GetVendorName(vendor_code);
	string test_str = vendor_name + " ";
	// Some manufacturers (Dell yet again) inconstently prefix or don't prefix
	// their name into the model string. If that's the case, remove it.
	if (_strnicmp(model, test_str.c_str(), test_str.size()) == 0)
		model_name = &model[test_str.size()];
	else
		model_name = model;

	free(edid);
	return true;
}

// Issuing CapabilitiesRequestAndCapabilitiesReply() can be a lengthy process and may need
// to be reiterated multiple times before we get a valid answer. So use an async task.
void nvMonitor::GetAllowedInputs()
{
	char* capabilities_string = NULL;
	DWORD i = 1, size = 0;

	auto physical_monitor = GetFirstPhysicalMonitor();
	if (physical_monitor == NULL)
		return;

	// GetCapabilitiesStringLength() is *VERY* temperamental, so we retry up to VCP_CAPS_MAX_RETRY_TIME
	steady_clock::time_point begin = steady_clock::now();
	for (i = 1; !GetCapabilitiesStringLength(physical_monitor->hPhysicalMonitor, &size); i++) {
		if (cancel_allowed_inputs_task)
			return;
		auto elapsed = duration_cast<seconds>(steady_clock::now() - begin);
		if (elapsed.count() > VCP_CAPS_MAX_RETRY_TIME) {
			logger("failed to get VCP capabilities for %s after %d attempts: %x\n", display_name, i, GetLastError());
			return;
		}
	}

	// If GetCapabilitiesStringLength() succeeded then the subsequent call to
	// CapabilitiesRequestAndCapabilitiesReply() usually doesn't fail, so no need for retries there.
	capabilities_string = (char*)malloc(size);
	if (capabilities_string == NULL)
		goto out;

	if (CapabilitiesRequestAndCapabilitiesReply(physical_monitor->hPhysicalMonitor, capabilities_string, size)) {
		if (cancel_allowed_inputs_task)
			goto out;
		string capabilities = capabilities_string;
		auto elapsed = duration_cast<milliseconds>(steady_clock::now() - begin);

		// Get the model name while we're here
		regex model_regex(R"(model\(([^)]+)\))");
		smatch match;
		if (model_name == "" && regex_search(capabilities, match, model_regex))
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


		// Oh, and you'd think *serious* display manufacturers, like Dell, would
		// report available inputs in the proper order. But you'd think wrong...
		stable_sort(allowed_inputs.begin(), allowed_inputs.end());

		if (!cancel_allowed_inputs_task)
			tray_simulate_hottkey(hkUpdateSubmenu);

		string separator, inputs;
		for (const auto& input : allowed_inputs) {
			inputs += separator + InputToString(input);
			separator = ", ";
		}
		logger("Retrieved %S %s VCP capabilities in %u.%03u seconds (%d %s)\n", display_name, model_name.c_str(),
			(unsigned)(elapsed.count() / 1000), (unsigned)(elapsed.count() % 1000), i, (i == 1) ? "try" : "tries");
		logger("Valid input(s): %s\n", inputs.c_str());

	} else {
		logger("Could not get VCP capabilities for %S: %x\n", display_name, GetLastError());
	}

out:
	free(capabilities_string);
}

uint8_t nvMonitor::GetMonitorInput()
{
	if (!supports_vcp)
		return 0;

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
	if (!supports_vcp)
		return 0;

	uint8_t ret = 0, current = GetMonitorInput();

	auto physical_monitor = GetFirstPhysicalMonitor();
	if (physical_monitor == NULL)
		return 0;

	if (requested == VCP_INPUT_HOME)
		requested = home_input;
	if (requested == 0)
		return 0;

	if (requested == VCP_INPUT_NEXT || requested == VCP_INPUT_PREVIOUS) {
		if (allowed_inputs.size() == 0 || current == 0)
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

	return ret;
}
