// Stripped down version of nvapi.h used by nvBrightness
// Original copyright notice below:
/*********************************************************************************************************\
|*                                                                                                        *|
|* SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.  *|
|* SPDX-License-Identifier: MIT                                                                           *|
|*                                                                                                        *|
|* Permission is hereby granted, free of charge, to any person obtaining a                                *|
|* copy of this software and associated documentation files (the "Software"),                             *|
|* to deal in the Software without restriction, including without limitation                              *|
|* the rights to use, copy, modify, merge, publish, distribute, sublicense,                               *|
|* and/or sell copies of the Software, and to permit persons to whom the                                  *|
|* Software is furnished to do so, subject to the following conditions:                                   *|
|*                                                                                                        *|
|* The above copyright notice and this permission notice shall be included in                             *|
|* all copies or substantial portions of the Software.                                                    *|
|*                                                                                                        *|
|* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR                             *|
|* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,                               *|
|* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL                               *|
|* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER                             *|
|* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING                                *|
|* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER                                    *|
|* DEALINGS IN THE SOFTWARE.                                                                              *|
|*                                                                                                        *|
|*                                                                                                        *|
\*********************************************************************************************************/

#pragma once

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef int         NvAPI_Status;
typedef uint8_t     NvU8;
typedef uint16_t    NvU16;
typedef uint32_t    NvU32;
typedef int32_t     NvS32;
typedef uint64_t    NvU64;
typedef float       NvF32;
typedef double      NvF64;

#define NVAPI_INTERFACE extern NvAPI_Status

#define NV_DECLARE_HANDLE(name) struct name##__ { int unused; }; typedef struct name##__ *name

NV_DECLARE_HANDLE(NvPhysicalGpuHandle);
NV_DECLARE_HANDLE(NvDisplayHandle);

typedef NvPhysicalGpuHandle HM_ADAPTER_NVAPI;

#define NVAPI_SHORT_STRING_MAX      64

typedef char NvAPI_ShortString[NVAPI_SHORT_STRING_MAX];

#define MAKE_NVAPI_VERSION(typeName,ver) (NvU32)(sizeof(typeName) | ((ver)<<16))

#define NVAPI_MAX_PHYSICAL_GPUS     64

#define NVAPI_OK                    0

#define NV_GPU_DISPLAYIDS_VER1	    MAKE_NVAPI_VERSION(NV_GPU_DISPLAYIDS,1)
#define NV_GPU_DISPLAYIDS_VER2      MAKE_NVAPI_VERSION(NV_GPU_DISPLAYIDS,3)

#define NV_GPU_DISPLAYIDS_VER NV_GPU_DISPLAYIDS_VER2

typedef struct {
	NvU32    version;
	int      connectorType;	        // Really an enum, which we don't care about
	NvU32    displayId;
	NvU32    isDynamic : 1;
	NvU32    isMultiStreamRootNode : 1;
	NvU32    isActive : 1;
	NvU32    isCluster : 1;
	NvU32    isOSVisible : 1;
	NvU32    isWFD : 1;
	NvU32    isConnected : 1;
	NvU32    reservedInternal : 10;
	NvU32    isPhysicallyConnected : 1;
	NvU32    reserved : 14;
} NV_GPU_DISPLAYIDS;

#define NV_GAMMARAMPEX_NUM_VALUES    1024

typedef struct {
	NvU32 version;
	NvF32 gammaRampEx[3 * NV_GAMMARAMPEX_NUM_VALUES];
	NvU32 unknown;
} NV_GAMMA_CORRECTION_EX;

#define NVGAMMA_CORRECTION_EX_VER   MAKE_NVAPI_VERSION(NV_GAMMA_CORRECTION_EX,1)

#define NVAPI_API_CALL __stdcall

typedef int *(NVAPI_API_CALL *NVAPI_QUERYINTERFACE) (NvU32);
typedef int (NVAPI_API_CALL *NVAPI_INITIALIZE) (void);
typedef int (NVAPI_API_CALL *NVAPI_UNLOAD) (void);
typedef int (NVAPI_API_CALL *NVAPI_GETERRORMESSAGE) (NvAPI_Status, NvAPI_ShortString);
typedef int (NVAPI_API_CALL *NVAPI_ENUMPHYSICALGPUS) (NvPhysicalGpuHandle*, NvU32*);
typedef int (NVAPI_API_CALL *NVAPI_GPU_GETCONNECTEDDISPLAYIDS) (NvPhysicalGpuHandle, NV_GPU_DISPLAYIDS*, NvU32*, NvU32);
// Undocumented by nVidia. Takes a properly formatted NV_GAMMA_CORRECTION_EX* table.
typedef int (NVAPI_API_CALL *NVAPI_DISP_SETTARGETGAMMACORRECTION) (NvU32, NV_GAMMA_CORRECTION_EX*);
// Undocumented by nVidia. Straightforward.
typedef int (NVAPI_API_CALL *NVAPI_DISP_GETDISPLAYHANDLEFROMDISPLAYID) (NvU32, NvDisplayHandle*);
// Undocumented by nVidia. Appears to deal with a GUID internally rather than an LUID. Second parameter must be set to 1.
typedef int (NVAPI_API_CALL* NVAPI_SYS_GETLUIDFROMDISPLAYID) (NvU32, NvU32, GUID*);
typedef int (NVAPI_API_CALL *NVAPI_GETASSOCIATEDNVIDIADISPLAYNAME) (NvDisplayHandle, NvAPI_ShortString);
typedef void (*NvAPI_Logger)(const char*, ...);

// The following works as long as the header is only ever used once
static HINSTANCE NvAPI_Library = NULL;
static NVAPI_QUERYINTERFACE nvapi_QueryInterface = NULL;
static NVAPI_INITIALIZE NvAPI_Initialize = NULL;
static NVAPI_UNLOAD NvAPI_Unload = NULL;
static NVAPI_GETERRORMESSAGE NvAPI_GetErrorMessage = NULL;
static NVAPI_ENUMPHYSICALGPUS NvAPI_EnumPhysicalGPUs = NULL;
static NVAPI_GPU_GETCONNECTEDDISPLAYIDS NvAPI_GPU_GetConnectedDisplayIds = NULL;
static NVAPI_DISP_SETTARGETGAMMACORRECTION NvAPI_DISP_SetTargetGammaCorrection = NULL;
static NVAPI_SYS_GETLUIDFROMDISPLAYID NvAPI_SYS_GetLUIDFromDisplayID = NULL;
static NVAPI_DISP_GETDISPLAYHANDLEFROMDISPLAYID NvAPI_DISP_GetDisplayHandleFromDisplayId = NULL;
static NVAPI_GETASSOCIATEDNVIDIADISPLAYNAME NvAPI_GetAssociatedNvidiaDisplayName = NULL;

#define NV_LOAD_FUNC(name, type, libname, logger) \
  name = (type) GetProcAddress(NvAPI_Library, #name); \
  if (name == NULL) { \
    logger("ERROR: %s is missing from %s shared library.\n", #name, #libname); \
    return -1; \
  }

#define NV_LOAD_ADDR(name, type, func, addr, libname, logger) \
  name = (type) func(addr); \
  if (name == NULL) { \
    logger("ERROR: %s at address 0x%08x is missing from %s shared library.\n", #name, addr, #libname); \
    return -1; \
  }

static inline int NvAPI_Init(NvAPI_Logger logger)
{
	if (NvAPI_Library != NULL)
		return 0;

#ifdef _WIN64
	NvAPI_Library = LoadLibraryA("nvapi64.dll");
#else
	NvAPI_Library = LoadLibraryA("nvapi.dll");
#endif

	if (NvAPI_Library == NULL)
		return -1;

	// Yes, "nvapi_QueryInterface" is case sensitive. Nice consistency, nVidia!
	NV_LOAD_FUNC(nvapi_QueryInterface, NVAPI_QUERYINTERFACE, NVAPI, logger);
	NV_LOAD_ADDR(NvAPI_Initialize, NVAPI_INITIALIZE, nvapi_QueryInterface, 0x0150E828, NVAPI, logger);
	NV_LOAD_ADDR(NvAPI_Unload, NVAPI_UNLOAD, nvapi_QueryInterface, 0xD22BDD7E, NVAPI, logger);
	NV_LOAD_ADDR(NvAPI_GetErrorMessage, NVAPI_GETERRORMESSAGE, nvapi_QueryInterface, 0x6C2D048C, NVAPI, logger);
	NV_LOAD_ADDR(NvAPI_EnumPhysicalGPUs, NVAPI_ENUMPHYSICALGPUS, nvapi_QueryInterface, 0xE5AC921F, NVAPI, logger);
	NV_LOAD_ADDR(NvAPI_GPU_GetConnectedDisplayIds, NVAPI_GPU_GETCONNECTEDDISPLAYIDS, nvapi_QueryInterface, 0x0078DBA2, NVAPI, logger);
	NV_LOAD_ADDR(NvAPI_DISP_SetTargetGammaCorrection, NVAPI_DISP_SETTARGETGAMMACORRECTION, nvapi_QueryInterface, 0x7082A053, NVAPI, logger);
	NV_LOAD_ADDR(NvAPI_DISP_GetDisplayHandleFromDisplayId, NVAPI_DISP_GETDISPLAYHANDLEFROMDISPLAYID, nvapi_QueryInterface, 0x96437923, NVAPI, logger);
	NV_LOAD_ADDR(NvAPI_SYS_GetLUIDFromDisplayID, NVAPI_SYS_GETLUIDFROMDISPLAYID, nvapi_QueryInterface, 0xD4A859F2, NVAPI, logger);
	NV_LOAD_ADDR(NvAPI_GetAssociatedNvidiaDisplayName, NVAPI_GETASSOCIATEDNVIDIADISPLAYNAME, nvapi_QueryInterface, 0x22A78B05, NVAPI, logger);

	return 0;
}

static inline void NvAPI_Exit(void)
{
	if (NvAPI_Unload != NULL)
		NvAPI_Unload();
	if (NvAPI_Library != NULL)
		FreeLibrary(NvAPI_Library);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
