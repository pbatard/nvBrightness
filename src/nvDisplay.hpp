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
#include <string>
#include <future>
#include <set>

#include "nvBrightness.h"
#include "nvMonitor.hpp"

using namespace std;

class nvDisplay : public nvMonitor {
	uint32_t display_id;
	wchar_t display_name[64] = L"Unknown";
	set<uint32_t> luids;
	float color_setting[nvAttrMax][nvColorMax];
	atomic<bool> stop_detect_luid_task = false;
	future<void> detect_luid_task;
	mutex apply_gamma_mutex;
	void DetectLuid();
	void GetFriendlyDisplayName();
public:
	nvDisplay(uint32_t);
	~nvDisplay() { stop_detect_luid_task = true; if (detect_luid_task.valid()) detect_luid_task.get(); };
	uint32_t GetLuid();
	wchar_t* GetDisplayName() { return display_name; };
	bool UpdateGamma();
	void ChangeBrightness(float);
	float GetBrightness();
	void LoadColorSettings();
	void SaveColorSettings();
	static size_t EnumerateDisplays(list<nvDisplay>& display_list);
};
