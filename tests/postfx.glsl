// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Effets de post-traitement : degrade avec un point tres lumineux (valeurs au-dessus de 1, pour que le bloom les voie)
// post=sharpen:0.5,chroma:0.02,bloom:0.6,contrast:0.3,saturation:-0.5,hue:0.1,vignette:0.8
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 base = vec3(0.2 + 0.5 * uv.x, 0.3, 0.7 - 0.5 * uv.y);
    vec3 spot = vec3(4.0, 3.0, 1.0) * smoothstep(0.12, 0.08, distance(uv, vec2(0.4, 0.6)));
    fragColor = vec4(base + spot, 1.0);
}
