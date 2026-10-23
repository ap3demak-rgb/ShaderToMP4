// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Grille néon — Un sol quadrillé qui fuit vers l'horizon, un ciel violet et un soleil rayé ; le sol est calculé par intersection du rayon avec un plan.
// en: Neon grid — A gridded floor fleeing to the horizon, a purple sky and a striped sun; the floor comes from ray-plane intersection.

void mainVR(out vec4 fragColor, in vec2 fragCoord, in vec3 ro, in vec3 rd) {
    vec3 col = mix(vec3(0.9, 0.2, 0.5), vec3(0.05, 0.0, 0.2), clamp(rd.y * 2.0, 0.0, 1.0));
    if (rd.y < 0.0) {
        vec2 p = (ro.xz + rd.xz * (-1.5 / rd.y)) + vec2(0.0, iTime * 2.0);
        vec2 g = abs(fract(p) - 0.5);
        float l = smoothstep(0.47, 0.5, max(g.x, g.y));
        col = vec3(0.03, 0.0, 0.1) + vec3(0.1, 0.9, 1.0) * l * exp(rd.y * 3.0);
    } else {
        vec3 s = normalize(vec3(0.0, 0.2, -1.0));
        float d = acos(clamp(dot(rd, s), -1.0, 1.0));
        float stripes = step(0.3, fract(rd.y * 40.0 - 4.0));
        col = mix(col, vec3(1.0, 0.8, 0.2), smoothstep(0.25, 0.24, d) * (rd.y > 0.2 ? 1.0 : stripes));
    }
    fragColor = vec4(col, 1.0);
}
