// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// LUT 1D a trois points 0, 0.25, 1 (lut_curve1d.cube) : 0.5 devient 0.25, 0.25 devient 0.125, 1 reste 1
// lut=lut_curve1d.cube
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(0.5, 0.25, 1.0, 1.0);
}
