// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Parametres animes : a t = 1 s, speed (lineaire de 1 a 3) vaut 2, tint (lissage) vaut (0.5, 0, 0.5), count passe de 2 a 6 en escalier
// uniform float speed = 1.0 [0..5]
// uniform vec3 tint = 1, 0, 0 [0..1]
// uniform color glow = #00ff00
// uniform bool flash = false
// uniform int count = 2 [1..10]
// key speed=0:1.0 ; 2:3.0
// key tint=smooth 0:1,0,0 ; 2:0,0,1
// key flash=true
// key count=constant 0:2 ; 1:6
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 left = vec3(speed / 5.0, glow.g * 0.5 + (flash ? 0.25 : 0.0), tint.b);
    vec3 right = vec3(float(count) / 10.0, glow.r, glow.b);
    fragColor = vec4(uv.x < 0.5 ? left : right, 1.0);
}
