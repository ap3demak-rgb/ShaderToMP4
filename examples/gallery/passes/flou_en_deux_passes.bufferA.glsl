// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    fragColor = vec4(scene((fragCoord - 0.5 * iResolution.xy) / iResolution.y), 1.0);
}
