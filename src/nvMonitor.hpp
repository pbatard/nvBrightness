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

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>

#include "nvapi.h"

#include <string>
#include <vector>
#include <thread>

// How long we may retry GetVCPFeatureAndVCPFeatureReply(), in ms
#define VCP_FEATURE_MAX_RETRY_TIME      500

using namespace std;

class nvMonitor {
	HMONITOR monitor_handle = NULL;
	wchar_t display_name[sizeof(NvAPI_ShortString)] = { 0 };
	wchar_t device_id[128] = { 0 };
	vector<PHYSICAL_MONITOR> physical_monitors;
	vector<uint8_t> allowed_inputs;
	uint8_t last_known_input = 0;
	string model_name = "Unknown";
	thread worker_thread;
	bool cancel_worker_thread = false;
	bool supports_vcp = false;
public:
	~nvMonitor();
	void InitializeMonitor(uint32_t);
	uint8_t GetMonitorLastKnownInput() const { return last_known_input; };
	uint8_t GetMonitorInput();
	void SaveMonitorInput() { last_known_input = GetMonitorInput(); };
	uint8_t SetMonitorInput(uint8_t);
	PHYSICAL_MONITOR* GetFirstPhysicalMonitor() { return (physical_monitors.size() == 0) ? NULL : &physical_monitors[0]; };
	void GetMonitorAllowedInputs();
	bool SupportsVCP() { return supports_vcp; };
	static const char* InputToString(uint8_t input);
};
