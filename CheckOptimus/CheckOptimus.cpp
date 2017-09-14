// CheckOptimus.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <dxgi1_2.h>
#include "nvapi.h"
#include "NvApiDriverSettings.h"
#include <vector>
#include <comdef.h>
#include <Wbemidl.h>
#include <SetupAPI.h>
#include <initguid.h>
#include <devguid.h>
#include <devpkey.h>

template <class T> inline void SafeFree(T*& pT)
{
    if (pT != nullptr)
    {
        free(pT);
        pT = nullptr;
    }
}

template <class T> inline void SafeRelease(T*& pT)
{
    if (pT != nullptr)
    {
        pT->Release();
        pT = nullptr;
    }
}

typedef enum _SYSTEM_TYPE
{
    SYSTEM_TYPE_UNKNOWN = 0,
    SYSTEM_TYPE_LAPTOP = 1,
    SYSTEM_TYPE_DESKTOP = 2,
}SYSTEM_TYPE, *PSYSTEM_TYPE;

typedef enum _GPU_VENDOR_ID
{
    GPU_VENDOR_AMD = 0x1002,
    GPU_VENDOR_NVIDIA = 0x10DE,
    GPU_VENDOR_MICROSOFT = 0x1414,
    GPU_VENDOR_INTEL = 0x8086,
}GPU_VENDOR_ID;

typedef struct _GPU_TOPOLOGY
{
    UINT32 AMDGFX;
    UINT32 INTELGFX;
    UINT32 NVGFX;
    UINT32 total;
}GPU_TOPOLOGY, *PGPU_TOPOLOGY;

// OS configuration information
SYSTEM_TYPE OSGetSystemType()
{
    SYSTEM_TYPE sysType = SYSTEM_TYPE_UNKNOWN;
    
    HRESULT hr = S_OK;

    if (SUCCEEDED(hr))
    {
        hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    }

    if (SUCCEEDED(hr))
    {
        hr = CoInitializeSecurity(
            NULL,
            -1,                          // COM authentication
            NULL,                        // Authentication services
            NULL,                        // Reserved
            RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication 
            RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation  
            NULL,                        // Authentication info
            EOAC_NONE,                   // Additional capabilities 
            NULL                         // Reserved
        );
    }

    IWbemLocator *pLoc = NULL;

    if (SUCCEEDED(hr))
    {
        hr = CoCreateInstance(
            CLSID_WbemLocator,
            0,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, (LPVOID *)&pLoc);
    }

    IWbemServices *pSvc = NULL;

    if (SUCCEEDED(hr))
    {
        hr = pLoc->ConnectServer(
            _bstr_t(L"ROOT\\CIMV2"), // Object path of WMI namespace
            NULL,                    // User name. NULL = current user
            NULL,                    // User password. NULL = current
            0,                       // Locale. NULL indicates current
            NULL,                    // Security flags.
            0,                       // Authority (for example, Kerberos)
            0,                       // Context object 
            &pSvc                    // pointer to IWbemServices proxy
        );
    }

    if (SUCCEEDED(hr))
    {
        hr = CoSetProxyBlanket(
            pSvc,                        // Indicates the proxy to set
            RPC_C_AUTHN_WINNT,           // RPC_C_AUTHN_xxx
            RPC_C_AUTHZ_NONE,            // RPC_C_AUTHZ_xxx
            NULL,                        // Server principal name 
            RPC_C_AUTHN_LEVEL_CALL,      // RPC_C_AUTHN_LEVEL_xxx 
            RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
            NULL,                        // client identity
            EOAC_NONE                    // proxy capabilities 
        );
    }

    IEnumWbemClassObject* pEnumerator = NULL;

    if (SUCCEEDED(hr))
    {
        hr = pSvc->ExecQuery(
            bstr_t("WQL"),
            bstr_t("SELECT * FROM Win32_ComputerSystem"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            NULL,
            &pEnumerator);
    }

    IWbemClassObject *pclsObj = NULL;
    ULONG uReturn = 0;

    while (pEnumerator)
    {
        HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1,
            &pclsObj, &uReturn);

        if (0 == uReturn)
        {
            break;
        }

        VARIANT vtProp = {0};

        hr = pclsObj->Get(L"PCSystemType", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt & VT_I4))
        {
            if (vtProp.intVal == 2)//Mobile (2)
            {
                sysType = SYSTEM_TYPE_LAPTOP;
            }
            else if (vtProp.intVal != 0)//Unspecified(0)
            {
                sysType = SYSTEM_TYPE_DESKTOP;
            }
        }

        VariantClear(&vtProp);
        SafeRelease(pclsObj);
    }

    SafeRelease(pSvc);
    SafeRelease(pLoc);
    SafeRelease(pEnumerator);
    CoUninitialize();

    return sysType;
}

void DetectGPUCount(GPU_TOPOLOGY &gpuCount)
{
    RtlZeroMemory(&gpuCount, sizeof(gpuCount));

    HDEVINFO hGfxDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_DISPLAY, 0, 0, DIGCF_PRESENT);
    if (INVALID_HANDLE_VALUE != hGfxDevInfo)
    {
        SP_DEVINFO_DATA gfxDevInfoData = { 0 };
        gfxDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        PBYTE pDescBuf = nullptr;
        DWORD bufSize = 0;
        for (DWORD devIdx = 0; SetupDiEnumDeviceInfo(hGfxDevInfo, devIdx, &gfxDevInfoData); devIdx++)
        {
            BOOL isSuccess = FALSE;
            DWORD err;
            DEVPROPTYPE PropType;

            // Get device instance path, vendor ID, device ID
            SafeFree(pDescBuf);
            bufSize = 0;
            while (!SetupDiGetDeviceProperty(
                hGfxDevInfo,
                &gfxDevInfoData,
                &DEVPKEY_Device_InstanceId,
                &PropType,
                pDescBuf,
                bufSize,
                &bufSize,
                0))
            {
                err = GetLastError();
                if (err == ERROR_INSUFFICIENT_BUFFER)
                {
                    SafeFree(pDescBuf);
                    pDescBuf = (PBYTE)malloc(bufSize);
                    RtlZeroMemory(pDescBuf, bufSize);
                }
                else
                {
                    break;
                }
            }
            if (wcslen((PWCHAR)pDescBuf))
            {
                WCHAR idBuf[5] = { 0 };
                wcsncpy_s(idBuf, 5, (PWCHAR)pDescBuf + 8, 4);
                UINT venId = wcstol(idBuf, nullptr, 16);
                if (venId == GPU_VENDOR_AMD)
                {
                    gpuCount.AMDGFX++;
                }
                else if (venId == GPU_VENDOR_NVIDIA)
                {
                    gpuCount.NVGFX++;
                }
                else if (venId == GPU_VENDOR_INTEL)
                {
                    gpuCount.INTELGFX++;
                }
                else if (venId == GPU_VENDOR_MICROSOFT)
                {
                    // Don't care basic driver
                }
            }

            SafeFree(pDescBuf);
        }

        SetupDiDestroyDeviceInfoList(hGfxDevInfo);
    }

    gpuCount.total = gpuCount.AMDGFX + gpuCount.NVGFX + gpuCount.INTELGFX;
}

// AMD application profile
void ConfigAMDMGPUApplicationProfile()
{}

// NVIDIA application profile
typedef struct _NV_DRS_SETTING_ENTRY
{
    NvU32 settingId;
    NVDRS_SETTING_TYPE settingType;
    NvU32 u32CurrentValue;
    NVDRS_BINARY_SETTING binaryCurrentValue;
    NvAPI_UnicodeString wszCurrentValue;
}NV_DRS_SETTING_ENTRY, *PNV_DRS_SETTING_ENTRY;

void NVInitProfile(NvAPI_UnicodeString &nvProfile, BOOL isDGPU)
{
    std::vector<std::wstring> nvProfileList;
    nvProfileList.push_back(std::wstring(TEXT("Hypereal HyperScope IGPU")));
    nvProfileList.push_back(std::wstring(TEXT("Hypereal HyperScope DGPU")));
    UINT profileIdx = isDGPU ? 1 : 0;
    UINT len = wcslen(nvProfileList.at(profileIdx).c_str());
    memcpy_s(nvProfile, sizeof(nvProfile), nvProfileList.at(profileIdx).c_str(), wcslen(nvProfileList.at(profileIdx).c_str()) * sizeof(WCHAR));
}

NvAPI_Status NVValidateProfile(NvDRSSessionHandle hSession, NvDRSProfileHandle &hProfile, BOOL isDGPU)
{
    NvAPI_Status nvStatus = NVAPI_INVALID_HANDLE;

    if (hSession)
    {
        nvStatus = NVAPI_OK;
    }

    if (NVAPI_OK == nvStatus)
    {
        NvAPI_UnicodeString nvProfileName;
        RtlZeroMemory(nvProfileName, sizeof(nvProfileName));
        NVInitProfile(nvProfileName, isDGPU);
        nvStatus = NvAPI_DRS_FindProfileByName(hSession, nvProfileName, &hProfile);
        if (NVAPI_PROFILE_NOT_FOUND == nvStatus)
        {
            NVDRS_PROFILE profileInfo = { 0 };
            RtlZeroMemory(&profileInfo, sizeof(profileInfo));
            RtlZeroMemory(&profileInfo.profileName, sizeof(profileInfo.profileName));
            profileInfo.version = NVDRS_PROFILE_VER;
            memcpy_s(profileInfo.profileName, sizeof(profileInfo.profileName), nvProfileName, sizeof(nvProfileName));
            profileInfo.gpuSupport.geforce = 1;
            profileInfo.isPredefined = 0;
            nvStatus = NvAPI_DRS_CreateProfile(hSession, &profileInfo, &hProfile);
        }
    }

    return nvStatus;
}

void NVInitProfileApplication(std::vector<std::wstring> &appList, BOOL isDGPU)
{
    std::vector<std::wstring> appListIGPU;
    appListIGPU.clear();
    appListIGPU.push_back(std::wstring(TEXT("VDHost.exe")));
    appListIGPU.push_back(std::wstring(TEXT("VDTest.exe")));

    std::vector<std::wstring> appListDGPU;
    appListDGPU.clear();
    appListDGPU.push_back(std::wstring(TEXT("HyperScope.exe")));

    appList = isDGPU ? appListDGPU : appListIGPU;
}

NvAPI_Status NVValidateProfileApplication(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, BOOL isDGPU)
{
    NvAPI_Status nvStatus = NVAPI_INVALID_HANDLE;

    if (hSession && hProfile)
    {
        nvStatus = NVAPI_OK;
    }

    if (NVAPI_OK == nvStatus)
    {
        std::vector<std::wstring> applicationList;
        applicationList.clear();
        NVInitProfileApplication(applicationList, isDGPU);

        NVDRS_APPLICATION appInfo = { 0 };
        RtlZeroMemory(&appInfo, sizeof(appInfo));
        appInfo.version = NVDRS_APPLICATION_VER;

        NVDRS_PROFILE profileInformation = { 0 };
        profileInformation.version = NVDRS_PROFILE_VER;
        nvStatus = NvAPI_DRS_GetProfileInfo(hSession, hProfile, &profileInformation);

        BOOL doUpdateApp = FALSE;
        NvU32 startIndex = 0;
        NvU32 appCount = profileInformation.numOfApps;
        NVDRS_APPLICATION *pPrevApps = NULL;
        if (appCount > 0)
        {
            pPrevApps = (NVDRS_APPLICATION*)malloc(appCount * sizeof(NVDRS_APPLICATION));
        }
        else
        {
            doUpdateApp = TRUE;
        }

        if (pPrevApps)
        {
            RtlZeroMemory(pPrevApps, appCount * sizeof(NVDRS_APPLICATION));
            pPrevApps[0].version = NVDRS_APPLICATION_VER;
            nvStatus = NvAPI_DRS_EnumApplications(hSession, hProfile, startIndex, &appCount, pPrevApps);

            if (appCount != applicationList.size())
            {
                doUpdateApp = TRUE;
            }
            else
            {
                for (NvU32 appIdx = 0; appIdx < appCount; appIdx++)
                {
                    std::vector<std::wstring>::iterator app = applicationList.begin();
                    for (; app != applicationList.end(); app++)
                    {
                        if (!_wcsicmp((PWCHAR)pPrevApps[appIdx].appName, app->c_str()))
                        {
                            break;
                        }
                    }
                    if (app == applicationList.end())
                    {
                        doUpdateApp = TRUE;
                        break;
                    }
                }
            }
            if (doUpdateApp)
            {
                for (NvU32 appIdx = 0; appIdx < appCount; appIdx++)
                {
                    nvStatus = NvAPI_DRS_DeleteApplicationEx(hSession, hProfile, &pPrevApps[appIdx]);
                }
            }
            SafeFree(pPrevApps);
        }

        if (doUpdateApp)
        {
            for (std::vector<std::wstring>::iterator app = applicationList.begin(); app != applicationList.end(); app++)
            {
                RtlZeroMemory(&appInfo, sizeof(appInfo));
                RtlZeroMemory(&appInfo.appName, sizeof(appInfo.appName));
                appInfo.version = NVDRS_APPLICATION_VER;
                memcpy_s(appInfo.appName, sizeof(appInfo.appName), app->c_str(), wcslen(app->c_str()) * sizeof(WCHAR));
                nvStatus = NvAPI_DRS_CreateApplication(hSession, hProfile, &appInfo);
            }
        }
    }

    return nvStatus;
}

void NVInitProfileSettings(std::vector<NV_DRS_SETTING_ENTRY> &nvDrsUpdateList, BOOL isDGPU)
{
    NV_DRS_SETTING_ENTRY nvOptimusSettingIGPU[] =
    {
        { SHIM_MCCOMPAT_ID, NVDRS_DWORD_TYPE, SHIM_MCCOMPAT_INTEGRATED,{ 0 },{ 0 } },
        { SHIM_RENDERING_MODE_ID, NVDRS_DWORD_TYPE, SHIM_RENDERING_MODE_INTEGRATED,{ 0 },{ 0 } },
        { SHIM_RENDERING_OPTIONS_ID, NVDRS_DWORD_TYPE, SHIM_RENDERING_OPTIONS_DEFAULT_RENDERING_MODE | SHIM_RENDERING_OPTIONS_IGPU_TRANSCODING,{ 0 },{ 0 } },
    };

    NV_DRS_SETTING_ENTRY nvOptimusSettingDGPU[] =
    {
        { SHIM_MCCOMPAT_ID, NVDRS_DWORD_TYPE, SHIM_MCCOMPAT_ENABLE,{ 0 },{ 0 } },
        { SHIM_RENDERING_MODE_ID, NVDRS_DWORD_TYPE, SHIM_RENDERING_MODE_ENABLE,{ 0 },{ 0 } },
        { SHIM_RENDERING_OPTIONS_ID, NVDRS_DWORD_TYPE, SHIM_RENDERING_OPTIONS_DEFAULT_RENDERING_MODE,{ 0 },{ 0 } },
    };

    PNV_DRS_SETTING_ENTRY pDrsSettingsList[] = { nvOptimusSettingIGPU , nvOptimusSettingDGPU };
    PNV_DRS_SETTING_ENTRY pDrsSetting = isDGPU ? nvOptimusSettingDGPU : nvOptimusSettingIGPU;
    UINT numDrsSetting = isDGPU ? ARRAYSIZE(nvOptimusSettingDGPU) : ARRAYSIZE(nvOptimusSettingIGPU);
    for (UINT idxEntry = 0; idxEntry < numDrsSetting; idxEntry++)
    {
        nvDrsUpdateList.push_back(pDrsSetting[idxEntry]);
    }
}

NvAPI_Status NVUpdateDrsSetting(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, PNV_DRS_SETTING_ENTRY pDrsSet)
{
    NvAPI_Status nvStatus = NVAPI_INVALID_HANDLE;

    if (hSession && hProfile && pDrsSet)
    {
        nvStatus = NVAPI_OK;
    }

    BOOL doUpdate = FALSE;

    NVDRS_SETTING nvDrsSetting = { 0 };
    RtlZeroMemory(&nvDrsSetting, sizeof(nvDrsSetting));
    if (NVAPI_OK == nvStatus)
    {
        nvDrsSetting.version = NVDRS_SETTING_VER;
        nvDrsSetting.settingId = pDrsSet->settingId;
        nvDrsSetting.settingType = pDrsSet->settingType;
        nvStatus = NvAPI_DRS_GetSetting(hSession, hProfile, pDrsSet->settingId, &nvDrsSetting);

        if (nvStatus == NVAPI_OK)
        {
            switch (pDrsSet->settingType)
            {
            case NVDRS_DWORD_TYPE:
            {
                if (nvDrsSetting.u32CurrentValue != pDrsSet->u32CurrentValue)
                {
                    nvDrsSetting.u32CurrentValue = pDrsSet->u32CurrentValue;
                    doUpdate = TRUE;
                }
                break;
            }
            case NVDRS_BINARY_TYPE:
            {
                // TODO
                break;
            }
            case NVDRS_STRING_TYPE:
            {
                // TODO
                break;
            }
            case NVDRS_WSTRING_TYPE:
            {
                // TODO
                break;
            }
            }
        }
        else
        {
            doUpdate = TRUE;
        }
    }

    if (doUpdate)
    {
        nvStatus = NvAPI_DRS_SetSetting(hSession, hProfile, &nvDrsSetting);
    }

    return nvStatus;
}

NvAPI_Status NVValidateProfileSettings(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, BOOL isDGPU)
{
    NvAPI_Status nvStatus = NVAPI_INVALID_HANDLE;

    if (hSession && hProfile)
    {
        nvStatus = NVAPI_OK;
    }

    if (NVAPI_OK == nvStatus)
    {
        std::vector<NV_DRS_SETTING_ENTRY> nvDrsUpdateList;
        NVInitProfileSettings(nvDrsUpdateList, isDGPU);
        for (std::vector<NV_DRS_SETTING_ENTRY>::iterator nvDrsEntry = nvDrsUpdateList.begin(); nvDrsEntry != nvDrsUpdateList.end(); nvDrsEntry++)
        {
            nvStatus = NVUpdateDrsSetting(hSession, hProfile, &(*nvDrsEntry));
            if (nvStatus != NVAPI_OK)
            {
                break;
            }
        }
    }

    return nvStatus;
}

void ConfigNVMGPUApplicationProfile()
{
    NvAPI_Status nvStatus = NVAPI_OK;
    NvDRSSessionHandle hSession = NULL;
    NvDRSProfileHandle hProfile = NULL;
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NvAPI_Initialize();
    }
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NvAPI_DRS_CreateSession(&hSession);
    }
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NvAPI_DRS_LoadSettings(hSession);
    }
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NVValidateProfile(hSession, hProfile, false);
    }
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NVValidateProfileApplication(hSession, hProfile, false);
    }
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NVValidateProfileSettings(hSession, hProfile, false);
    }
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NVValidateProfile(hSession, hProfile, true);
    }
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NVValidateProfileApplication(hSession, hProfile, true);
    }
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NVValidateProfileSettings(hSession, hProfile, true);
    }
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NvAPI_DRS_SaveSettings(hSession);
    }
    if (NVAPI_OK == nvStatus)
    {
        nvStatus = NvAPI_DRS_DestroySession(hSession);
    }
}

void PrepareMGPUEnvironment()
{
    GPU_TOPOLOGY gpuList = { 0 };
    DetectGPUCount(gpuList);

    // MGPU
    if (gpuList.total > 1)
    {
        // Config application profile for mobile MGPU
        if (SYSTEM_TYPE_LAPTOP == OSGetSystemType())
        {
            if (gpuList.total != 2)
            {
                // Should not happen
            }
            else
            {
                if ((1 == gpuList.AMDGFX) && (1 == gpuList.INTELGFX) && (0 == gpuList.NVGFX))
                {
                    // A+I
                    ConfigAMDMGPUApplicationProfile();
                }
                else if ((2 == gpuList.AMDGFX) && (0 == gpuList.INTELGFX) && (0 == gpuList.NVGFX))
                {
                    // A+A
                    ConfigAMDMGPUApplicationProfile();
                }
                else if ((0 == gpuList.AMDGFX) && (1 == gpuList.INTELGFX) && (1 == gpuList.NVGFX))
                {
                    // N+I
                    ConfigNVMGPUApplicationProfile();
                }
                else if ((0 == gpuList.AMDGFX) && (0 == gpuList.INTELGFX) && (2 == gpuList.NVGFX))
                {
                    // Same as SLI
                }
                else
                {
                    // Should not happen
                }
            }
        }
    }
}

int main()
{
    PrepareMGPUEnvironment();

    // Dxgi test
    HRESULT hr = S_OK;
    IDXGIFactory1 *pDxgiFactory = nullptr;
    IDXGIAdapter1 *pDxgiAdapter = nullptr;
    IDXGIOutput *pDxgiOutput = nullptr;
    UINT iAdapter = 0;
    UINT iOutputOnAdapter = 0;
    DXGI_ADAPTER_DESC1 adapterDesc = { 0 };
    DXGI_OUTPUT_DESC outputDesc = { 0 };

    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pDxgiFactory));

    while (SUCCEEDED(hr))
    {
        SafeRelease(pDxgiAdapter);
        hr = pDxgiFactory->EnumAdapters1(iAdapter, &pDxgiAdapter);
        if (SUCCEEDED(hr))
        {
            iOutputOnAdapter = 0;
            pDxgiAdapter->GetDesc1(&adapterDesc);
            wprintf(TEXT("Adapter %d, %s, luid %08x%08x\n"), iAdapter, adapterDesc.Description, adapterDesc.AdapterLuid.HighPart, adapterDesc.AdapterLuid.LowPart);
            while (SUCCEEDED(hr))
            {
                SafeRelease(pDxgiOutput);
                hr = pDxgiAdapter->EnumOutputs(iOutputOnAdapter, &pDxgiOutput);
                if (SUCCEEDED(hr))
                {
                    pDxgiOutput->GetDesc(&outputDesc);
                    wprintf(TEXT("    Output %d, %s\n"), iOutputOnAdapter, outputDesc.DeviceName);
                }
                else if (hr == DXGI_ERROR_NOT_FOUND)
                {
                    break;
                }
                iOutputOnAdapter++;
            }
        }
        else if (hr == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        iAdapter++;
        hr = S_OK;
    }

    SafeRelease(pDxgiFactory);

    return 0;
}

