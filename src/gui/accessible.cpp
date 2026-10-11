// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Noms accessibles (lecteurs d'ecran) : les champs, listes et curseurs sont annonces par leur libelle. Un fournisseur UI
// Automation minimal ne donne que le nom ; tout le reste (valeur, motifs, etat) vient du fournisseur systeme du controle.
#include "accessible.h"
#include "core/i18n.h"
#include <commctrl.h>
#include <uiautomation.h>

#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "oleaut32.lib")

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

}  // namespace

void setAccessibleName(HWND control, const char* langKey) {
    if (control) SetWindowSubclass(control, accProc, 77, (DWORD_PTR)langKey);
}
