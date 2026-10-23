// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Spirale sans fin — Une spirale logarithmique dont l'échelle est multipliée par 2 pendant la boucle : le dernier état est le premier, à l'échelle près.
// en: Endless spiral — A logarithmic spiral whose scale is doubled during the loop: the last state is the first, up to scale.

// duration=8
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float ph = fract(iTime / 8.0);
    float r = log2(length(uv) + 1e-4) - ph;
    float a = atan(uv.y, uv.x) / 6.2831853;
    float v = fract(r * 1.0 + a * 3.0);
    vec3 col = mix(vec3(0.05, 0.05, 0.15), vec3(1.0, 0.7, 0.2), smoothstep(0.45, 0.5, abs(v - 0.5) * 2.0 * 0.5 + 0.25 - 0.0));
    col = mix(col, vec3(0.9, 0.3, 0.5), step(0.5, v) * 0.5);
    fragColor = vec4(col, 1.0);
}
