// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Clavier simule : a t = 1 s, espace est enfoncee, A a ete relachee, fleche gauche vient d'etre pressee (apparition sur cette image)
// iChannel0=keyboard
// key=space:0.5-2 ; 65:0-0.5
// key=left:1-3
float k(int code, int row) { return texelFetch(iChannel0, ivec2(code, row), 0).x; }
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c = uv.x < 0.5 ? vec3(k(32, 0), k(37, 1), 0.5 * k(65, 2))
                        : vec3(k(65, 0), k(37, 0), 0.5 * k(32, 2) + 0.25 * k(37, 2));
    fragColor = vec4(c, 1.0);
}
