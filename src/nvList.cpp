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

#include "nvList.hpp"

bool nvList::Update()
{
	bool ret = false;
	NvAPI_Status r;
	NvPhysicalGpuHandle gpu_handles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
	NvU32 gpu_count = 0;

	list_mutex.lock();

	// We never clear the list of known displays, but we do clear the active displays
	active.clear();

	r = NvAPI_EnumPhysicalGPUs(gpu_handles, &gpu_count);
	if (r != NVAPI_OK) {
		logger("NvAPI_EnumPhysicalGPUs: %d %s\n", r, NvAPI_GetErrorString(r));
		goto out;
	}

	for (NvU32 i = 0; i < gpu_count; i++) {
		NvU32 display_count = 0;

		r = NvAPI_GPU_GetConnectedDisplayIds(gpu_handles[i], NULL, &display_count, 0);
		if (r != NVAPI_OK) {
			logger("NvAPI_GPU_GetConnectedDisplayIds[%d]: %d %s\n", i, r, NvAPI_GetErrorString(r));
			continue;
		}

		if (display_count == 0)
			continue;

		NV_GPU_DISPLAYIDS* display_ids = (NV_GPU_DISPLAYIDS*)calloc(display_count, sizeof(NV_GPU_DISPLAYIDS));
		if (display_ids == NULL) {
			logger("Could not allocate NV_GPU_DISPLAYIDS array\n");
			goto out;
		}
		display_ids[0].version = NV_GPU_DISPLAYIDS_VER;

		r = NvAPI_GPU_GetConnectedDisplayIds(gpu_handles[i], display_ids, &display_count, 0);
		if (r != NVAPI_OK) {
			logger("NvAPI_GPU_GetConnectedDisplayIds[%d]: %d %s\n", i, r, NvAPI_GetErrorString(r));
		} else for (NvU32 j = 0; j < display_count; j++) {
			bool found = false;
			for (auto& display : displays) {
				if (display.GetDisplayId() == display_ids[j].displayId) {
					// The monitor data may have changed -> update it
					display.GetMonitorData();
					active.push_back(&display);
					found = true;
					break;
				}
			}
			if (!found)
				active.push_back(&displays.emplace_back(display_ids[j].displayId));
			ret = true;
		}
		free(display_ids);
	}

out:
	list_mutex.unlock();
	return ret;
}

nvDisplay* nvList::GetDisplay(size_t index)
{
	nvDisplay* ret = nullptr;

	list_mutex.lock();

	if (active.empty())
		goto out;

	ret = (index >= active.size()) ? nullptr : active[index];

out:
	list_mutex.unlock();
	return ret;
}

nvDisplay* nvList::GetDisplay(const wchar_t* device_id)
{
	nvDisplay* ret = nullptr;

	list_mutex.lock();

	if (active.empty())
		goto out;

	for (auto& display : active)
		if (wcscmp(display->GetDeviceId(), device_id) == 0) {
			ret = display;
			break;
		}

out:
	list_mutex.unlock();
	return ret;
}
nvDisplay* nvList::GetNextDisplay(const wchar_t* device_id)
{
	nvDisplay* ret = nullptr;

	list_mutex.lock();
	for (auto i = 0; i < active.size(); i++) {
		if (wcscmp(active[i]->GetDeviceId(), device_id) == 0) {
			ret = active[(i + 1) % active.size()];
			break;
		}
	}

	list_mutex.unlock();
	return ret;
}

nvDisplay* nvList::GetPrevDisplay(const wchar_t* device_id)
{
	nvDisplay* ret = nullptr;

	list_mutex.lock();
	for (auto i = 0; i < active.size(); i++) {
		if (wcscmp(active[i]->GetDeviceId(), device_id) == 0) {
			ret = active[(i + active.size() - 1) % active.size()];
			break;
		}
	}

	list_mutex.unlock();
	return ret;
}
