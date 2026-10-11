// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Shader ecrit pour GLSL ES : #version, #extension, precision et texture2D sont acceptes
#version 300 es
#extension GL_OES_standard_derivatives : enable
precision highp float;
// iChannel0=noise
// iChannel0.filter=nearest
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(texture2D(iChannel0, uv).rgb * 0.7 + vec3(0.0, uv.y * 0.3, 0.0), 1.0);
}
