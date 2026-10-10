#include "ui_render.h"
#include <d3d11.h>
#include <dwrite.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include "../../res/resource.h"

#ifdef _MSC_VER
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

using Microsoft::WRL::ComPtr;

namespace ui {
namespace {

struct Surface {
    ComPtr<ID2D1Bitmap1> target, cpu;
};

struct Svg {
    ComPtr<ID2D1SvgDocument> doc;
    float vb = 24;  // taille de la viewBox (carree)
};

struct State {
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID2D1Factory5> factory;
    ComPtr<ID2D1Device4> dev;
    ComPtr<ID2D1DeviceContext5> dc;
    ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1SolidColorBrush> brush;
    std::map<std::pair<int, int>, Surface> surfaces;
    std::map<int, ComPtr<IDWriteTextFormat>> formats;
    std::map<int, Svg> svgs;
    bool ok = false;
} S;

bool createDevice(D3D_DRIVER_TYPE type) {
    D3D_FEATURE_LEVEL fl;
    return SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                       D3D11_SDK_VERSION, &S.d3d, &fl, nullptr));
}

Surface* surface(int w, int h) {
    auto key = std::make_pair(w, h);
    auto it = S.surfaces.find(key);
    if (it != S.surfaces.end()) return &it->second;
    // Les petits elements (boutons...) gardent leur tampon ; les grands (fenetre entiere, redimensionnee en continu)
    // ne conservent que le dernier, pour borner la memoire.
    if (S.surfaces.size() > 48) S.surfaces.clear();
    if ((long long)w * h > 400000) {
        for (auto i = S.surfaces.begin(); i != S.surfaces.end();) {
            if ((long long)i->first.first * i->first.second > 400000) i = S.surfaces.erase(i);
            else ++i;
        }
    }
    Surface s;
    D2D1_BITMAP_PROPERTIES1 tp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    D2D1_BITMAP_PROPERTIES1 cp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(S.dc->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, tp, &s.target))) return nullptr;
    if (FAILED(S.dc->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, cp, &s.cpu))) return nullptr;
    return &(S.surfaces[key] = s);
}

IDWriteTextFormat* format(float size, bool bold, Align align, bool vcenter) {
    int key = ((int)(size * 10) << 4) | (bold ? 8 : 0) | ((int)align << 1) | (vcenter ? 1 : 0);
    auto it = S.formats.find(key);
    if (it != S.formats.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(S.dw->CreateTextFormat(L"Segoe UI", nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"fr-FR", &f)))
        return nullptr;
    f->SetTextAlignment(align == Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                        : align == Right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    f->SetParagraphAlignment(vcenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<IDWriteInlineObject> ell;
    if (SUCCEEDED(S.dw->CreateEllipsisTrimmingSign(f.Get(), &ell))) {
        DWRITE_TRIMMING t{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        f->SetTrimming(&t, ell.Get());
    }
    return (S.formats[key] = f).Get();
}

Svg* svg(int id) {
    auto it = S.svgs.find(id);
    if (it != S.svgs.end()) return it->second.doc ? &it->second : nullptr;
    Svg sv;
    sv.vb = id == IC_DROP ? 96.0f : id == IC_LOGO ? 64.0f : 24.0f;
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (r) {
        HGLOBAL g = LoadResource(nullptr, r);
        const void* data = g ? LockResource(g) : nullptr;
        DWORD size = SizeofResource(nullptr, r);
        if (data && size) {
            ComPtr<IStream> stream;
            stream.Attach(SHCreateMemStream((const BYTE*)data, size));
            if (stream) S.dc->CreateSvgDocument(stream.Get(), D2D1::SizeF(sv.vb, sv.vb), &sv.doc);
        }
    }
    S.svgs[id] = sv;
    return sv.doc ? &S.svgs[id] : nullptr;
}

}  // namespace

bool init() {
    if (S.ok) return true;
    if (!createDevice(D3D_DRIVER_TYPE_HARDWARE) && !createDevice(D3D_DRIVER_TYPE_WARP)) return false;
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(S.d3d.As(&dxgi))) return false;
    D2D1_FACTORY_OPTIONS opt{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory5), &opt, (void**)S.factory.GetAddressOf())))
        return false;
    if (FAILED(S.factory->CreateDevice(dxgi.Get(), &S.dev))) return false;
    ComPtr<ID2D1DeviceContext4> dc4;
    if (FAILED(S.dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc4))) return false;
    if (FAILED(dc4.As(&S.dc))) return false;  // SVG : Windows 10 1703 minimum
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)S.dw.GetAddressOf())))
        return false;
    if (FAILED(S.dc->CreateSolidColorBrush(D2D1::ColorF(0), &S.brush))) return false;
    S.ok = true;
    return true;
}

void shutdown() {
    S.svgs.clear();
    S.formats.clear();
    S.surfaces.clear();
    S.brush.Reset();
    S.dc.Reset();
    S.dev.Reset();
    S.factory.Reset();
    S.dw.Reset();
    S.d3d.Reset();
    S.ok = false;
}

Frame::Frame(int w_, int h_, D2D1_COLOR_F bg) : w(std::max(1, w_)), h(std::max(1, h_)) {
    if (!S.ok) return;
    Surface* s = surface(w, h);
    if (!s) return;
    dc = S.dc.Get();
    dc->SetTarget(s->target.Get());
    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    dc->Clear(bg);
}

Frame::~Frame() {
    if (dc && !done) dc->EndDraw();
}

void Frame::present(HDC hdc, int x, int y) {
    if (!dc || done) return;
    done = true;
    dc->EndDraw();
    Surface* s = surface(w, h);
    if (!s) return;
    if (FAILED(s->cpu->CopyFromBitmap(nullptr, s->target.Get(), nullptr))) return;
    D2D1_MAPPED_RECT m{};
    if (FAILED(s->cpu->Map(D2D1_MAP_OPTIONS_READ, &m))) return;
    std::vector<uint8_t> px((size_t)w * h * 4);
    for (int row = 0; row < h; row++) memcpy(&px[(size_t)row * w * 4], m.bits + (size_t)row * m.pitch, (size_t)w * 4);
    s->cpu->Unmap();
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // haut en bas
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(hdc, x, y, w, h, 0, 0, 0, h, px.data(), &bi, DIB_RGB_COLORS);
}

void Frame::fillRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) {
    if (!dc) return;
    S.brush->SetColor(c);
    dc->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), S.brush.Get());
}

void Frame::strokeRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float width) {
    if (!dc) return;
    float i = width / 2;
    r = D2D1::RectF(r.left + i, r.top + i, r.right - i, r.bottom - i);
    S.brush->SetColor(c);
    dc->DrawRoundedRectangle(D2D1::RoundedRect(r, std::max(0.0f, radius - i), std::max(0.0f, radius - i)), S.brush.Get(), width);
}

void Frame::fillRoundGradient(D2D1_RECT_F r, float radius, D2D1_COLOR_F a, D2D1_COLOR_F b) {
    if (!dc) return;
    D2D1_GRADIENT_STOP stops[2] = {{0.0f, a}, {1.0f, b}};
    ComPtr<ID2D1GradientStopCollection> coll;
    if (FAILED(dc->CreateGradientStopCollection(stops, 2, &coll))) return;
    ComPtr<ID2D1LinearGradientBrush> gb;
    if (FAILED(dc->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.right, r.bottom)), coll.Get(), &gb)))
        return;
    dc->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), gb.Get());
}

void Frame::fillCircle(float cx, float cy, float radius, D2D1_COLOR_F c) {
    if (!dc) return;
    S.brush->SetColor(c);
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), S.brush.Get());
}

void Frame::text(const std::wstring& s, D2D1_RECT_F r, float size, bool bold, D2D1_COLOR_F c, Align align, bool vcenter) {
    if (!dc || s.empty()) return;
    IDWriteTextFormat* f = format(size, bold, align, vcenter);
    if (!f) return;
    S.brush->SetColor(c);
    dc->DrawText(s.c_str(), (UINT32)s.size(), f, r, S.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}

void Frame::icon(int resId, D2D1_RECT_F r, bool tint, D2D1_COLOR_F c) {
    if (!dc) return;
    Svg* sv = svg(resId);
    if (!sv) return;
    ComPtr<ID2D1SvgElement> root;
    sv->doc->GetRoot(&root);
    if (root && tint) {
        D2D1_COLOR_F col = D2D1::ColorF(c.r, c.g, c.b, 1.0f);
        root->SetAttributeValue(L"fill", D2D1_SVG_ATTRIBUTE_POD_TYPE_COLOR, &col, sizeof(col));
        root->SetAttributeValue(L"stroke", D2D1_SVG_ATTRIBUTE_POD_TYPE_COLOR, &col, sizeof(col));
        float op = c.a;
        root->SetAttributeValue(L"opacity", D2D1_SVG_ATTRIBUTE_POD_TYPE_FLOAT, &op, sizeof(op));
    }
    float side = std::min(r.right - r.left, r.bottom - r.top);
    float sc = side / sv->vb;
    float ox = r.left + ((r.right - r.left) - side) / 2, oy = r.top + ((r.bottom - r.top) - side) / 2;
    dc->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc) * D2D1::Matrix3x2F::Translation(ox, oy));
    dc->DrawSvgDocument(sv->doc.Get());
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
}

float measureText(const std::wstring& s, float size, bool bold) {
    if (!S.ok || s.empty()) return 0;
    IDWriteTextFormat* f = format(size, bold, Left, true);
    if (!f) return 0;
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(S.dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 4000, 100, &l))) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

}  // namespace ui
