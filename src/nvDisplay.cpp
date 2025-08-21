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

#include "nvDisplay.hpp"

extern list<nvDisplay> display_list;

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

// nvDisplay methods
nvDisplay::nvDisplay(uint32_t display_id)
{
	GUID guid = { 0 };
	NvAPI_Status r;

	this->display_id = display_id;
	InitializeMonitor(display_id);

	r = NvAPI_SYS_GetLUIDFromDisplayID(display_id, 1, &guid);
	if (r != NVAPI_OK) {
		logger("NvAPI_SYS_GetLUIDFromDisplayID(0x%08x): %d %s\n", display_id, r, NvAPI_GetErrorString(r));
		for (auto j = 0; j < 9; j++)
			color_setting[j / 3][j % 3] = 100.0f;
	} else {
		// The part we are after is the second DWORD of the GUID, XOR'd with 0xF00000000
		registry_key_string = format(L"Software\\NVIDIA Corporation\\Global\\NVTweak\\Devices\\{}-0\\Color",
			((uint32_t*)&guid)[1] ^ 0xf0000000);
		wchar_t reg_color_key_str[128];
		for (auto i = 0; i < 9; i++) {
			swprintf_s(reg_color_key_str, ARRAYSIZE(reg_color_key_str),
				L"%s\\%d", registry_key_string.c_str(), NV_COLOR_REGISTRY_INDEX + i);
			color_setting[i / 3][i % 3] = (float)ReadRegistryKey32(HKEY_CURRENT_USER, reg_color_key_str);
			// Set the default value if we couldn't read the key or it's out of bounds
			if (color_setting[i / 3][i % 3] < 80.0f || color_setting[i / 3][i % 3] > 120.0f)
				color_setting[i / 3][i % 3] = 100.0f;
		}
	}
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

	return r == NVAPI_OK;
}

void nvDisplay::SaveColorSettings(bool apply_to_all)
{
	wchar_t reg_color_key_str[128];

	for (auto i = 0; i < 9; i++) {
		swprintf_s(reg_color_key_str, ARRAYSIZE(reg_color_key_str),
			L"%s\\%d", registry_key_string.c_str(), NV_COLOR_REGISTRY_INDEX + i);
		WriteRegistryKey32(HKEY_CURRENT_USER, reg_color_key_str, (uint32_t)color_setting[i / 3][i % 3]);
	}
	// Add the NvCplGammaSet key to indicate that Gamma should be restored by the nVidia driver
	swprintf_s(reg_color_key_str, ARRAYSIZE(reg_color_key_str), L"%s\\NvCplGammaSet", registry_key_string.c_str());
	WriteRegistryKey32(HKEY_CURRENT_USER, reg_color_key_str, 1);

	// TODO: Add an option to apply to all displays
}

size_t nvDisplay::EnumerateDisplays()
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
