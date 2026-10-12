// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iMouse anime par images cles : position interpolee (t = 1 s entre deux cles), clic commence a 0.5 s (zw)
// mouse=0:0.1,0.2 ; 0.5:0.25,0.5,down ; 2:0.75,0.5,down
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float pos = smoothstep(10.0, 6.0, distance(fragCoord, iMouse.xy));
    float click = smoothstep(10.0, 6.0, distance(fragCoord, abs(iMouse.zw)));
    float held = iMouse.z > 0.0 ? 1.0 : 0.0;
    float fresh = iMouse.w > 0.0 ? 1.0 : 0.0;
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(pos + 0.15 * held, click + 0.15 * fresh, uv.x * 0.2, 1.0);
}
