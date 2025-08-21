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

using namespace std;

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
