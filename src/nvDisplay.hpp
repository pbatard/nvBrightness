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
	vector<wchar_t> display_name;
	set<uint32_t> known_luids;
	uint32_t active_luid;
	float color_setting[nvAttrMax][nvColorMax];
	void PopulateDisplayName();
public:
	nvDisplay(uint32_t);
	uint32_t GetDisplayId() { return display_id; };
	uint32_t GetLuid();
	wchar_t* GetDisplayName() { return display_name.data(); };
	float GetBrightness();
	bool UpdateGamma();
	bool UpdateLuids();
	void ChangeBrightness(float);
	void LoadColorSettings();
	void SaveColorSettings();
};
