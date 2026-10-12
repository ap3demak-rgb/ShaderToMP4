// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Calcul en virgule flottante : un gris de 2.0 passe par un contraste de -0.5 avant la conversion en 8 bits.
// En flottant : (2.0 - 0.5) * 0.5 + 0.5 = 1.25, donc blanc. S'il etait ecrete a 1.0 d'abord, on obtiendrait 0.75.
// post=contrast:-0.5
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(vec3(2.0), 1.0);
}
