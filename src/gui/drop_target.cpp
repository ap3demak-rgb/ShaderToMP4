#include "drop_target.h"
#include "app.h"
#include <oleidl.h>
#include <shellapi.h>

namespace {

class DropTarget : public IDropTarget {
public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --refs;
        if (!r) delete this;
        return r;
    }

    // IDropTarget
    STDMETHODIMP DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        accept = !g.running && hasFiles(data);
        *effect = accept ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (accept) setDropHot(true);
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD, POINTL, DWORD* effect) override {
        *effect = accept ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    STDMETHODIMP DragLeave() override {
        setDropHot(false);
        return S_OK;
    }
    STDMETHODIMP Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        setDropHot(false);
        *effect = DROPEFFECT_NONE;
        if (!accept) return S_OK;
        FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM sm{};
        if (SUCCEEDED(data->GetData(&fe, &sm))) {
            HDROP d = (HDROP)sm.hGlobal;
            UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> files;
            for (UINT i = 0; i < n; i++) {
                wchar_t p[MAX_PATH * 2];
                if (DragQueryFileW(d, i, p, MAX_PATH * 2)) files.push_back(p);
            }
            ReleaseStgMedium(&sm);
            addFiles(files);
            *effect = DROPEFFECT_COPY;
        }
        return S_OK;
    }

private:
    static bool hasFiles(IDataObject* data) {
        FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        return data && data->QueryGetData(&fe) == S_OK;
    }
    ULONG refs = 1;
    bool accept = false;
};

}  // namespace

bool registerDropTarget(HWND wnd) {
    DropTarget* t = new DropTarget();
    HRESULT hr = RegisterDragDrop(wnd, t);
    t->Release();  // le systeme garde sa propre reference
    return SUCCEEDED(hr);
}

void revokeDropTarget(HWND wnd) { RevokeDragDrop(wnd); }
