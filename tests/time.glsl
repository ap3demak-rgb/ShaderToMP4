// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Depend du temps : verifie iTime (l'auto-test rend a t = 1 s)
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(0.5 + 0.5 * sin(uv.x * 6.0 + iTime * 2.0), 0.5 + 0.5 * cos(uv.y * 5.0 - iTime), uv.x * uv.y, 1.0);
}
