#include "gpu_pref.h"

bool applyGpuPreference(const std::string& gpu, const std::wstring& exe) {
    if (gpu != "high" && gpu != "low") return false;  // "auto" : on ne touche pas aux reglages de Windows
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, nullptr, 0,
                        KEY_READ | KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    wchar_t cur[64] = {0};
    DWORD sz = sizeof(cur) - 2, type = 0;
    const bool has = RegQueryValueExW(k, exe.c_str(), nullptr, &type, (BYTE*)cur, &sz) == ERROR_SUCCESS;
    const std::wstring want = gpu == "high" ? L"GpuPreference=2;" : L"GpuPreference=1;";
    bool changed = false;
    if (!has || want != cur)
        changed = RegSetValueExW(k, exe.c_str(), 0, REG_SZ, (const BYTE*)want.c_str(), (DWORD)((want.size() + 1) * 2)) ==
                  ERROR_SUCCESS;
    RegCloseKey(k);
    return changed;
}
