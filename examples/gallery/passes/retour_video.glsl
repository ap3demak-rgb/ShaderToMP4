// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Boucle de rétroaction vidéo — Le tampon A se relit lui-même légèrement tourné et agrandi, comme une caméra qui filme son propre écran.
// en: Video feedback loop — Buffer A reads itself slightly rotated and enlarged, like a camera filming its own screen.

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    fragColor = vec4(texture(iChannel0, fragCoord / iResolution.xy).rgb, 1.0);
}
