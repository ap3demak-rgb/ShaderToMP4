// Disque centre : verifie la geometrie (centre, rayon) et le fond
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float d = length(p);
    vec3 col = mix(vec3(1.0, 0.8, 0.1), vec3(0.05, 0.1, 0.3), smoothstep(0.30, 0.32, d));
    fragColor = vec4(col, 1.0);
}
