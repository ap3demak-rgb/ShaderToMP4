// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// LUT 3D d'inversion (lut_invert3d.cube) : (0.2, 0.4, 0.6) devient (0.8, 0.6, 0.4)
// lut=lut_invert3d.cube
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(0.2, 0.4, 0.6, 1.0);
}
