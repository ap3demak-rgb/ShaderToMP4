// Tunnel : coordonnees polaires, profondeur = 1 / rayon
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(p) + 0.001;
    float a = atan(p.y, p.x);
    float z = 0.35 / r + iTime * 0.8;
    float stripes = smoothstep(0.0, 0.08, abs(sin(z * 6.0))) * smoothstep(0.0, 0.08, abs(sin(a * 6.0 + z * 0.5)));
    vec3 base = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + z * 0.8 + a);
    vec3 col = mix(vec3(0.02), base, stripes);
    col *= smoothstep(0.0, 0.35, r);
    fragColor = vec4(col, 1.0);
}
