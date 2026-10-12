// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Chauffe : 10 images simulees avant la premiere ; a t = 1 s (image 60) iFrame vaut 70 (rouge) et le tampon A a ete
// execute 71 fois (vert), de l'image -10 a l'image 60
// warmup=10
// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(float(iFrame) / 255.0, texture(iChannel0, vec2(0.5)).r, 0.0, 1.0);
}
