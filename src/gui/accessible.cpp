// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Noms accessibles (lecteurs d'ecran) : les champs, listes et curseurs sont annonces par leur libelle. Un fournisseur UI
// Automation minimal ne donne que le nom ; tout le reste (valeur, motifs, etat) vient du fournisseur systeme du controle.
#include "accessible.h"
#include "core/i18n.h"
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

class NameProvider : public IRawElementProviderSimple {
public:
    NameProvider(HWND h, const char* key) : hwnd_(h), key_(key) {}
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
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override {
        *o = (ProviderOptions)(ProviderOptions_ServerSideProvider | ProviderOptions_OverrideProvider);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** p) override {
        *p = nullptr;  // les motifs viennent du fournisseur systeme
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* v) override {
        VariantInit(v);
        if (id == UIA_NamePropertyId) {
            const std::wstring name = TX(key_);
            v->vt = VT_BSTR;
            v->bstrVal = SysAllocString(name.c_str());
        }
        return S_OK;  // VT_EMPTY : le fournisseur systeme repond
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** p) override {
        return UiaHostProviderFromHwnd(hwnd_, p);
    }

private:
    long refs_ = 1;
    HWND hwnd_;
    const char* key_;
};

LRESULT CALLBACK accProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    if (msg == WM_GETOBJECT && (LONG)lp == (LONG)UiaRootObjectId) {
        NameProvider* p = new NameProvider(h, (const char*)data);
        const LRESULT r = UiaReturnRawElementProvider(h, wp, lp, p);
        p->Release();
        return r;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, accProc, 77);
    return DefSubclassProc(h, msg, wp, lp);
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
    SetWindowSubclass(control, accProc, 77, (DWORD_PTR)langKey);
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
