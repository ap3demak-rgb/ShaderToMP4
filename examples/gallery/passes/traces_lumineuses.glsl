// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Traces lumineuses — Le tampon A garde l'image précédente multipliée par 0,97 et y ajoute un point mobile : la mémoire d'une image à l'autre fait la traînée.
// en: Light trails — Buffer A keeps the previous frame multiplied by 0.97 and adds a moving dot: memory from one frame to the next makes the trail.

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float v = texture(iChannel0, uv).r;
    vec3 col = 0.5 + 0.5 * cos(6.2831853 * (v * 0.8 + vec3(0.0, 0.33, 0.67)) + iTime);
    fragColor = vec4(col * smoothstep(0.0, 0.3, v), 1.0);
}
