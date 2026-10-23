// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

vec3 scene(vec2 uv) {
    vec3 col = vec3(0.02, 0.03, 0.08);
    for (int i = 0; i < 6; i++) {
        float f = float(i);
        vec2 c = 0.35 * vec2(cos(iTime * 0.7 + f * 1.05), sin(iTime * 0.9 + f * 2.1));
        col += (0.5 + 0.5 * cos(f + vec3(0.0, 2.0, 4.0))) * smoothstep(0.08, 0.0, length(uv - c));
    }
    return col;
}
