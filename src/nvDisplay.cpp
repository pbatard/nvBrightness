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
#include <math.h>

#include "nvapi.h"
#include "registry.h"

#include <format>
#include <list>
#include <numeric>

#include "nvDisplay.hpp"

using namespace std::chrono;

#pragma comment(lib, "synchronization.lib")

// Calculates a Gamma Ramp value, for a specific color, at an index in range [0-1023], for
// use with NvAPI_DISP_SetTargetGammaCorrection() in the same way nVidia does.
static NvF32 CalculateGamma(NvS32 index, NvF32 brightness, NvF32 contrast, NvF32 gamma)
{
	contrast = (contrast - 100.0f) / 100.0f;
	if (contrast <= 0.0f)
		contrast = (contrast + 1.0f) * ((NvF32)index / 1023.0f - 0.5f);
	else
		contrast = ((NvF32)index / 1023.0f - 0.5f) / (1.0f - contrast);

	brightness = (brightness - 100.0f) / 100.0f + contrast + 0.5f;
	if (brightness < 0.0f)
		brightness = 0.0f;
	if (brightness > 1.0f)
		brightness = 1.0f;

	gamma = (NvF32)pow((double)brightness, 1.0 / ((double)gamma / 100.0));
	if (gamma < 0.0f)
		gamma = 0.0f;
	if (gamma > 1.0f)
		gamma = 1.0f;

	return gamma;
}

nvDisplay::nvDisplay(uint32_t display_id)
:nvMonitor(display_id)
{
	this->display_id = display_id;
	GetFriendlyDisplayName();

	// TODO: Do we want to report the GPU name/number and GPU output port here as well?
	logger("Detected '%S' [%S]", device_name[0] != 0 ? device_name : L"Unknown", display_name);
	if (home_input != 0)
		logger(" using input %s\n", InputToString(home_input));
	else
		logger("\n");
	logger("nVidia display ID: 0x%08x, nVidia LUID: %u\n", display_id, GetLuid());

	// Retrieve the list of LUIDs we have found to be associated with this display.
	wchar_t* multi_sz = ReadRegistryKeyMultiStr(HKEY_CURRENT_USER, format(L"NVID_{:#08x}", display_id).c_str());
	const wchar_t* p = multi_sz;
	while (*p != L'\0') {
		luids.insert(static_cast<uint32_t>(stoul(p)));
		p += wcslen(p) + 1;
	}
	LoadColorSettings();
	detect_luid_task = async(launch::async, &nvDisplay::DetectLuid, this);
}

void nvDisplay::GetFriendlyDisplayName()
{
	DISPLAY_DEVICE dd = { .cb = sizeof(dd) };

	for (int i = 0; EnumDisplayDevices(NULL, i, &dd, 0); i++) {
		DISPLAY_DEVICE dd_monitor = { .cb = sizeof(dd_monitor) };

		if (!EnumDisplayDevices(dd.DeviceName, 0, &dd_monitor, 0))
			continue;

		UINT32 path_count, mode_count;

		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS)
			continue;

		DISPLAYCONFIG_PATH_INFO* paths = (DISPLAYCONFIG_PATH_INFO*)malloc(sizeof(DISPLAYCONFIG_PATH_INFO) * path_count);
		DISPLAYCONFIG_MODE_INFO* modes = (DISPLAYCONFIG_MODE_INFO*)malloc(sizeof(DISPLAYCONFIG_MODE_INFO) * mode_count);

		if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths, &mode_count, modes, NULL) != ERROR_SUCCESS)
			continue;

		for (UINT32 p = 0; p < path_count; p++) {
			DISPLAYCONFIG_TARGET_DEVICE_NAME targetName;
			memset(&targetName, 0, sizeof(targetName));
			targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
			targetName.header.size = sizeof(targetName);
			targetName.header.adapterId = paths[p].targetInfo.adapterId;
			targetName.header.id = paths[p].targetInfo.id;

			if (DisplayConfigGetDeviceInfo((DISPLAYCONFIG_DEVICE_INFO_HEADER*)&targetName) == ERROR_SUCCESS) {
				if (wcsstr(targetName.monitorDevicePath, device_id) != NULL) {
					wcscpy_s(display_name, 64, targetName.monitorFriendlyDeviceName);
					break;
				}
			}
		}

		free(paths);
		free(modes);
	}
}

// The nVidia driver is crap when it comes to re-applying color settings on display update
// because it can apply them before the display is fully ready, especially if a display uses
// HDR or Dolby-Vision. This can result in the display jumping to 100% brightness if you
// happen to turn your eARC amp on or off. We compensate for that by having our own thread
// that detects LUID updates, and that applies the gamma settings with a sensible delay.
void nvDisplay::DetectLuid()
{
	atomic<bool> false_value = false;
	uint32_t last_luid = GetLuid();

	// Of course, this would be much nicer if C++ had wait_until() on std::atomics, which
	// is such a BASIC synchronisation feature that one would have expected not to take
	// 15 bloody years (and counting) to be implemented. Or do you expect me to use a
	// conditional_variable WITH ITS COMPLETELY UNWARRANTED MUTEX with a stupid boolean?
	// And don't get me started on std::thread::join() which is a FUCKING LIE when invoked
	// from a destructor!! C++ thread synchronisation is a joke...
	while (!WaitOnAddress(&stop_detect_luid_task, &false_value, sizeof(atomic<bool>), 1000)
		&& GetLastError() == ERROR_TIMEOUT) {
		uint32_t cur_luid = GetLuid();
		if (cur_luid != 0 && cur_luid != last_luid) {
			last_luid = cur_luid;
			logger("Display %S switched to LUID: %u\n", display_name, last_luid);
			Sleep(1000);
			UpdateGamma();
		}
	}
}

uint32_t nvDisplay::GetLuid()
{
	GUID guid = { 0 };
	NvAPI_Status r = NvAPI_SYS_GetLUIDFromDisplayID(display_id, 1, &guid);
	if (r != NVAPI_OK)
		return 0;
	// The part we are after is the second DWORD of the GUID, XOR'd with 0xF00000000
	return ((uint32_t*)&guid)[1] ^ 0xf0000000;
}

float nvDisplay::GetBrightness()
{
	float brightness = color_setting[nvAttrBrightness][nvColorRed] +
		color_setting[nvAttrBrightness][nvColorGreen] +
		color_setting[nvAttrBrightness][nvColorBlue];
	return brightness / 3.0f;
}

void nvDisplay::ChangeBrightness(float delta)
{
	for (auto Color = 0; Color < nvColorMax; Color++) {
		color_setting[nvAttrBrightness][Color] += delta;
		if (color_setting[nvAttrBrightness][Color] < 80.0f)
			color_setting[nvAttrBrightness][Color] = 80.0f;
		if (color_setting[nvAttrBrightness][Color] > 100.0f)
			color_setting[nvAttrBrightness][Color] = 100.0f;
	}
}

bool nvDisplay::UpdateGamma()
{
	NV_GAMMA_CORRECTION_EX gamma_correction;
	NvAPI_Status r;

	// The mutex is likely not needed but it can't hurt...
	apply_gamma_mutex.lock();
	gamma_correction.version = NVGAMMA_CORRECTION_EX_VER;
	gamma_correction.unknown = 1;

	for (NvS32 Index = 0; Index < NV_GAMMARAMPEX_NUM_VALUES; Index++) {
		for (auto Color = 0; Color < nvColorMax; Color++) {
			gamma_correction.gammaRampEx[nvColorMax * Index + Color] = CalculateGamma(
				Index,
				color_setting[nvAttrBrightness][Color],
				color_setting[nvAttrContrast][Color],
				color_setting[nvAttrGamma][Color]
			);
		}
	}

	r = NvAPI_DISP_SetTargetGammaCorrection(display_id, &gamma_correction);
	if (r != NVAPI_OK)
		logger("NvAPI_DISP_SetTargetGammaCorrection failed for display 0x%08x: %d %s\n", display_id, r, NvAPI_GetErrorString(r));

	apply_gamma_mutex.unlock();
	return (r == NVAPI_OK);
}

void nvDisplay::LoadColorSettings()
{
	uint32_t luid = GetLuid();
	wchar_t reg_color_key_str[128];

	if (luid == 0) {
		for (auto i = 0; i < 9; i++)
			color_setting[i / 3][i % 3] = 100.0f;
	} else {
		for (auto i = 0; i < 9; i++) {
			_snwprintf_s(reg_color_key_str, ARRAYSIZE(reg_color_key_str), _TRUNCATE,
				L"Software\\NVIDIA Corporation\\Global\\NVTweak\\Devices\\%u-0\\Color\\%u",
				luid, NV_COLOR_REGISTRY_INDEX + i);
			color_setting[i / 3][i % 3] = (float)ReadRegistryKey32(HKEY_CURRENT_USER, reg_color_key_str);
			// Set the default value if we couldn't read the key or it's out of bounds
			if (color_setting[i / 3][i % 3] < 80.0f || color_setting[i / 3][i % 3] > 120.0f)
				color_setting[i / 3][i % 3] = 100.0f;
		}
	}
}

void nvDisplay::SaveColorSettings()
{
	uint32_t current_luid = GetLuid();
	wchar_t reg_color_key_str[128];

	// Update and save the LUID list if needed
	if (!luids.contains(current_luid)) {
		luids.insert(current_luid);
		wstring multi_sz = accumulate(
			luids.begin(), luids.end(), wstring{},
			[](wstring acc, uint32_t value) {
				acc += to_wstring(value);
				acc.push_back(L'\0');
				return acc;
			}
		);
		multi_sz.push_back(L'\0');
		WriteRegistryKeyMultiStr(HKEY_CURRENT_USER, format(L"NVID_{:#08x}", display_id).c_str(), multi_sz.c_str());
	}

	// Update all the LUIDs known for this display
	for (auto& luid : luids) {
		for (auto i = 0; i < 9; i++) {
			_snwprintf_s(reg_color_key_str, ARRAYSIZE(reg_color_key_str), _TRUNCATE,
				L"Software\\NVIDIA Corporation\\Global\\NVTweak\\Devices\\%u-0\\Color\\%u",
				luid, NV_COLOR_REGISTRY_INDEX + i);
			WriteRegistryKey32(HKEY_CURRENT_USER, reg_color_key_str, (uint32_t)color_setting[i / 3][i % 3]);
		}
		// Add the NvCplGammaSet key to indicate that Gamma should be restored by the nVidia driver
		_snwprintf_s(reg_color_key_str, ARRAYSIZE(reg_color_key_str), _TRUNCATE,
			L"Software\\NVIDIA Corporation\\Global\\NVTweak\\Devices\\%u-0\\Color\\NvCplGammaSet", luid);
		WriteRegistryKey32(HKEY_CURRENT_USER, reg_color_key_str, 1);
	}
}

size_t nvDisplay::EnumerateDisplays(list<nvDisplay>& display_list)
{
	NvAPI_Status r;
	NvPhysicalGpuHandle gpu_handles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
	NvU32 gpu_count = 0;

	display_list.clear();

	r = NvAPI_EnumPhysicalGPUs(gpu_handles, &gpu_count);
	if (r != NVAPI_OK) {
		logger("NvAPI_EnumPhysicalGPUs: %d %s\n", r, NvAPI_GetErrorString(r));
		return -1;
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
			return -1;
		}
		display_ids[0].version = NV_GPU_DISPLAYIDS_VER;

		r = NvAPI_GPU_GetConnectedDisplayIds(gpu_handles[i], display_ids, &display_count, 0);
		if (r != NVAPI_OK) {
			logger("NvAPI_GPU_GetConnectedDisplayIds[%d]: %d %s\n", i, r, NvAPI_GetErrorString(r));
		} else {
			for (NvU32 j = 0; j < display_count; j++)
				display_list.emplace_back(display_ids[j].displayId);
		}
		free(display_ids);
	}

	return display_list.size();
}
