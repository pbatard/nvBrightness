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

#include <list>
#include <mutex>
#include <vector>

#include "nvDisplay.hpp"

using namespace std;

class nvList {
private:
	mutex list_mutex;
	list<nvDisplay> displays;
	vector<nvDisplay*> active;
public:
	void Clear() { active.clear(); displays.clear(); }
	bool Update();
	nvDisplay* GetDisplay(size_t index);
	nvDisplay* GetDisplay(const wchar_t* device_id);
	nvDisplay* GetNextDisplay(const wchar_t* device_id);
	nvDisplay* GetPrevDisplay(const wchar_t* device_id);
};
