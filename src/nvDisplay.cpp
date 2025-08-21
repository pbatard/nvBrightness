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

extern list<nvDisplay> displayList;

static NvF32 CalculateGamma(NvS32 index, NvF32 fBrightness, NvF32 fContrast, NvF32 fGamma)
{
	fContrast = (fContrast - 100.0f) / 100.0f;
	if (fContrast <= 0.0f)
		fContrast = (fContrast + 1.0f) * ((NvF32)index / 1023.0f - 0.5f);
	else
		fContrast = ((NvF32)index / 1023.0f - 0.5f) / (1.0f - fContrast);

	fBrightness = (fBrightness - 100.0f) / 100.0f + fContrast + 0.5f;
	if (fBrightness < 0.0f)
		fBrightness = 0.0f;
	if (fBrightness > 1.0f)
		fBrightness = 1.0f;

	fGamma = (NvF32)pow((double)fBrightness, 1.0 / ((double)fGamma / 100.0));
	if (fGamma < 0.0f)
		fGamma = 0.0f;
	if (fGamma > 1.0f)
		fGamma = 1.0f;

	return fGamma;
}

// nvDisplay methods
nvDisplay::nvDisplay(uint32_t displayId)
{
	GUID guid = { 0 };
	NvAPI_Status r;

	this->displayId = displayId;
	monitor.InitializeMonitor(displayId);

	r = NvAPI_SYS_GetLUIDFromDisplayID(displayId, 1, &guid);
	if (r != NVAPI_OK) {
		logger("NvAPI_SYS_GetLUIDFromDisplayID(0x%08x): %d %s\n", displayId, r, NvErrStr(r));
		for (auto j = 0; j < 9; j++)
			this->colorSetting[j / 3][j % 3] = 100.0f;
	} else {
		// The part we are after is the second DWORD of the GUID, XOR'd with 0xF00000000
		this->registryKeyString = format(L"Software\\NVIDIA Corporation\\Global\\NVTweak\\Devices\\{}-0\\Color",
			((uint32_t*)&guid)[1] ^ 0xf0000000);
		wchar_t RegColorKeyStr[128];
		for (auto i = 0; i < 9; i++) {
			swprintf_s(RegColorKeyStr, ARRAYSIZE(RegColorKeyStr),
				L"%s\\%d", this->registryKeyString.c_str(), NV_COLOR_REGISTRY_INDEX + i);
			this->colorSetting[i / 3][i % 3] = (float)ReadRegistryKey32(HKEY_CURRENT_USER, RegColorKeyStr);
			// Set the default value if we couldn't read the key or it's out of bounds
			if (this->colorSetting[i / 3][i % 3] < 80.0f || this->colorSetting[i / 3][i % 3] > 120.0f)
				this->colorSetting[i / 3][i % 3] = 100.0f;
		}
	}
}

float nvDisplay::GetBrightness()
{
	float brightness = this->colorSetting[nvAttrBrightness][nvColorRed] +
		this->colorSetting[nvAttrBrightness][nvColorGreen] +
		this->colorSetting[nvAttrBrightness][nvColorBlue];
	return brightness / 3.0f;
}

void nvDisplay::ChangeBrightness(float delta)
{
	for (auto Color = 0; Color < nvColorMax; Color++) {
		this->colorSetting[nvAttrBrightness][Color] += delta;
		if (this->colorSetting[nvAttrBrightness][Color] < 80.0f)
			this->colorSetting[nvAttrBrightness][Color] = 80.0f;
		if (this->colorSetting[nvAttrBrightness][Color] > 100.0f)
			this->colorSetting[nvAttrBrightness][Color] = 100.0f;
	}
}

bool nvDisplay::UpdateGamma()
{
	NV_GAMMA_CORRECTION_EX gammaCorrection;
	NvAPI_Status r;

	gammaCorrection.version = NVGAMMA_CORRECTION_EX_VER;
	gammaCorrection.unknown = 1;

	for (NvS32 Index = 0; Index < NV_GAMMARAMPEX_NUM_VALUES; Index++) {
		for (auto Color = 0; Color < nvColorMax; Color++) {
			gammaCorrection.gammaRampEx[nvColorMax * Index + Color] = CalculateGamma(
				Index,
				this->colorSetting[nvAttrBrightness][Color],
				this->colorSetting[nvAttrContrast][Color],
				this->colorSetting[nvAttrGamma][Color]
			);
		}
	}

	r = NvAPI_DISP_SetTargetGammaCorrection(this->displayId, &gammaCorrection);
	if (r != NVAPI_OK)
		logger("NvAPI_DISP_SetTargetGammaCorrection failed for display 0x%08x: %d %s\n", this->displayId, r, NvErrStr(r));

	return r == NVAPI_OK;
}

void nvDisplay::SaveColorSettings(bool ApplyToAll)
{
	wchar_t RegColorKeyStr[128];

	for (auto i = 0; i < 9; i++) {
		swprintf_s(RegColorKeyStr, ARRAYSIZE(RegColorKeyStr),
			L"%s\\%d", this->registryKeyString.c_str(), NV_COLOR_REGISTRY_INDEX + i);
		WriteRegistryKey32(HKEY_CURRENT_USER, RegColorKeyStr, (uint32_t)this->colorSetting[i / 3][i % 3]);
	}
	// Add the NvCplGammaSet key to indicate that Gamma should be restored by the nVidia driver
	swprintf_s(RegColorKeyStr, ARRAYSIZE(RegColorKeyStr), L"%s\\NvCplGammaSet", this->registryKeyString.c_str());
	WriteRegistryKey32(HKEY_CURRENT_USER, RegColorKeyStr, 1);

	// TODO: Add an option to apply to all displays
}

size_t nvDisplay::EnumerateDisplays()
{
	NvAPI_Status r;
	NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
	NvU32 gpuCount = 0;

	displayList.clear();

	r = NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
	if (r != NVAPI_OK) {
		logger("NvAPI_EnumPhysicalGPUs: %d %s\n", r, NvErrStr(r));
		return -1;
	}

	for (NvU32 i = 0; i < gpuCount; i++) {
		NvU32 displayCount = 0;

		r = NvAPI_GPU_GetConnectedDisplayIds(gpuHandles[i], NULL, &displayCount, 0);
		if (r != NVAPI_OK) {
			logger("NvAPI_GPU_GetConnectedDisplayIds[%d]: %d %s\n", i, r, NvErrStr(r));
			continue;
		}

		if (displayCount == 0)
			continue;

		NV_GPU_DISPLAYIDS* displayIds = (NV_GPU_DISPLAYIDS*)calloc(displayCount, sizeof(NV_GPU_DISPLAYIDS));
		if (displayIds == NULL) {
			logger("Could not allocate NV_GPU_DISPLAYIDS array\n");
			return -1;
		}
		displayIds[0].version = NV_GPU_DISPLAYIDS_VER;

		r = NvAPI_GPU_GetConnectedDisplayIds(gpuHandles[i], displayIds, &displayCount, 0);
		if (r != NVAPI_OK) {
			logger("NvAPI_GPU_GetConnectedDisplayIds[%d]: %d %s\n", i, r, NvErrStr(r));
		} else {
			for (NvU32 j = 0; j < displayCount; j++)
				displayList.emplace_back(displayIds[j].displayId);
		}
		free(displayIds);
	}

	return displayList.size();
}
