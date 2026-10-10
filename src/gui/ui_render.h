// Rendu anti-crenele de l'interface : Direct2D (formes, degrades, icones SVG) et DirectWrite (texte).
// Chaque element dessine dans un tampon (Frame) qui est ensuite copie vers la fenetre.
#pragma once
#include "common.h"
#include <d2d1_3.h>

namespace ui {

bool init();      // cree le peripherique Direct2D (GPU, sinon WARP) ; false si indisponible
void shutdown();

inline D2D1_COLOR_F rgb(unsigned hex, float a = 1.0f) {
    return D2D1::ColorF(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, a);
}
inline D2D1_RECT_F rectf(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
inline D2D1_RECT_F rectf(const RECT& r) { return D2D1::RectF((float)r.left, (float)r.top, (float)r.right, (float)r.bottom); }

enum Align { Left = 0, Center = 1, Right = 2 };

// Image w x h : fond opaque `bg`, on dessine, puis present() la copie vers un HDC.
struct Frame {
    int w, h;
    ID2D1DeviceContext5* dc = nullptr;
    Frame(int w, int h, D2D1_COLOR_F bg);
    ~Frame();
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    void present(HDC hdc, int x = 0, int y = 0);

    void fillRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c);
    void strokeRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float width = 1.0f);
    void fillRoundGradient(D2D1_RECT_F r, float radius, D2D1_COLOR_F a, D2D1_COLOR_F b);  // diagonale a -> b
    void fillCircle(float cx, float cy, float radius, D2D1_COLOR_F c);
    void text(const std::wstring& s, D2D1_RECT_F r, float size, bool bold, D2D1_COLOR_F c, Align align = Left,
              bool vcenter = true);
    // Icone SVG : tint = true recolore (fill et stroke de la racine) avec la couleur c ; sinon couleurs d'origine.
    void icon(int resId, D2D1_RECT_F r, bool tint, D2D1_COLOR_F c);

private:
    bool done = false;
};

float measureText(const std::wstring& s, float size, bool bold);

}  // namespace ui
