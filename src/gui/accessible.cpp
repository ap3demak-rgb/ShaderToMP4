// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Noms accessibles (lecteurs d'ecran) : les champs, listes et curseurs sont annonces par leur libelle. Un fournisseur UI
// Automation minimal ne donne que le nom ; tout le reste (valeur, motifs, etat) vient du fournisseur systeme du controle.
#include "accessible.h"
#include "core/i18n.h"
#include <map>
#include <commctrl.h>
#include <initguid.h>  // definit les GUID de oleacc.h (CLSID_AccPropServices, PROPID_ACC_*) dans ce fichier
#include <oleacc.h>
#include <uiautomation.h>

#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// Fournisseur de substitution d'un controle : son nom (cle de langue ou texte de la fenetre) et, pour un bouton dessine par le programme, son type
// « bouton » avec le motif Invoke (le controle systeme BS_OWNERDRAW se presente sinon comme un simple panneau, non activable)
class NameProvider : public IRawElementProviderSimple, public IInvokeProvider {
public:
    NameProvider(HWND h, const char* key, bool button) : hwnd_(h), key_(key), button_(button) {}
    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const long n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) {
            *ppv = static_cast<IRawElementProviderSimple*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IInvokeProvider)) {
            *ppv = static_cast<IInvokeProvider*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override {
        *o = (ProviderOptions)(ProviderOptions_ServerSideProvider | ProviderOptions_OverrideProvider);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** p) override {
        *p = nullptr;  // les autres motifs viennent du fournisseur systeme
        if (button_ && id == UIA_InvokePatternId) {
            *p = static_cast<IInvokeProvider*>(this);
            AddRef();
        }
        return S_OK;
    }
    // IInvokeProvider : un clic du bouton (le parent recoit BN_CLICKED comme pour un clic de souris)
    HRESULT STDMETHODCALLTYPE Invoke() override {
        if (!IsWindowEnabled(hwnd_)) return UIA_E_ELEMENTNOTENABLED;
        PostMessageW(GetParent(hwnd_), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd_), BN_CLICKED), (LPARAM)hwnd_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* v) override {
        VariantInit(v);
        if (id == UIA_NamePropertyId) {
            std::wstring name;
            if (key_) {
                name = TX(key_);
            } else {
                wchar_t buf[256] = L"";
                GetWindowTextW(hwnd_, buf, 256);
                name = buf;
            }
            if (!name.empty()) {
                v->vt = VT_BSTR;
                v->bstrVal = SysAllocString(name.c_str());
            }
        } else if (id == UIA_ControlTypePropertyId) {  // le fournisseur de substitution doit dire le type : sinon l'element n'est qu'un panneau
            const int type = controlType();
            if (type) {
                v->vt = VT_I4;
                v->lVal = type;
            }
        } else if (id == UIA_IsKeyboardFocusablePropertyId && controlType() && controlType() != UIA_TextControlTypeId) {
            v->vt = VT_BOOL;
            v->boolVal = (GetWindowLongW(hwnd_, GWL_STYLE) & WS_TABSTOP) ? VARIANT_TRUE : VARIANT_FALSE;
        }
        return S_OK;  // VT_EMPTY : le fournisseur systeme repond
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** p) override {
        return UiaHostProviderFromHwnd(hwnd_, p);
    }

private:
    // Type UI Automation d'apres la classe Windows du controle (0 : on laisse le systeme repondre)
    int controlType() const {
        if (button_) return UIA_ButtonControlTypeId;
        wchar_t cls[48] = L"";
        GetClassNameW(hwnd_, cls, 48);
        const std::wstring c = cls;
        if (c == L"Edit") return UIA_EditControlTypeId;
        if (c == L"ComboBox") return UIA_ComboBoxControlTypeId;
        if (c == L"ListBox" || c == L"SysListView32") return UIA_ListControlTypeId;
        if (c == L"STSlider" || c == L"msctls_trackbar32") return UIA_SliderControlTypeId;
        if (c == L"STProgress" || c == L"msctls_progress32") return UIA_ProgressBarControlTypeId;
        if (c == L"RICHEDIT50W") return UIA_DocumentControlTypeId;
        if (c == L"Static") return UIA_TextControlTypeId;
        if (c == L"Button") return UIA_ButtonControlTypeId;
        return 0;
    }
    long refs_ = 1;
    HWND hwnd_;
    const char* key_;
    bool button_;
};

struct Info {
    const char* key = nullptr;  // nom donne par une cle de langue ; nullptr : texte de la fenetre
    bool button = false;
};
std::map<HWND, Info> g_info;

LRESULT CALLBACK accProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_GETOBJECT && (LONG)lp == (LONG)UiaRootObjectId) {
        const auto it = g_info.find(h);
        NameProvider* p = new NameProvider(h, it == g_info.end() ? nullptr : it->second.key, it != g_info.end() && it->second.button);
        const LRESULT r = UiaReturnRawElementProvider(h, wp, lp, p);
        p->Release();
        return r;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(h, accProc, 77);
        g_info.erase(h);
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void ensureSubclass(HWND h) {
    if (g_info.find(h) == g_info.end()) SetWindowSubclass(h, accProc, 77, 0);
}

IAccPropServices* propServices() {
    static IAccPropServices* svc = nullptr;
    if (!svc) CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_IAccPropServices, (void**)&svc);
    return svc;
}

std::vector<std::pair<HWND, const char*>> g_named;  // controles nommes par une cle de langue

void applyMsaaName(HWND h, const char* key) {
    if (IAccPropServices* s = propServices()) {
        const std::wstring name = TX(key);
        s->SetHwndPropStr(h, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_NAME, name.c_str());
    }
}

}  // namespace

void setAccessibleName(HWND control, const char* langKey) {
    if (!control) return;
    ensureSubclass(control);
    g_info[control].key = langKey;
    g_named.push_back({control, langKey});
    applyMsaaName(control, langKey);
}

void refreshAccessibleNames() {
    g_named.erase(std::remove_if(g_named.begin(), g_named.end(), [](const std::pair<HWND, const char*>& n) { return !IsWindow(n.first); }), g_named.end());
    for (const auto& n : g_named) applyMsaaName(n.first, n.second);
}

void setAccessibleRole(HWND control, long msaaRole) {
    if (IAccPropServices* s = propServices()) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = msaaRole;
        s->SetHwndProp(control, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_ROLE, v);
    }
}

void setAccessibleValue(HWND control, const std::wstring& value) {
    if (IAccPropServices* s = propServices()) s->SetHwndPropStr(control, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_VALUE, value.c_str());
}

void setAccessibleButton(HWND control) {
    if (!control) return;
    ensureSubclass(control);
    g_info[control].button = true;
}
