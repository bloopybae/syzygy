#version 450

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>
//
// ACES-inspired tone mapping curve for HDR to SDR fallback presentation.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;

const mat3 ACES_INPUT_MAT = mat3(
    0.59719, 0.35458, 0.04823,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777
);

const mat3 ACES_OUTPUT_MAT = mat3(
    1.60475, -0.53108, -0.07367,
   -0.10208,  1.10813, -0.00605,
   -0.00327, -0.07276,  1.07602
);

vec3 rrt_and_odt_fit(vec3 v) {
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.432951) + 0.238081;
  return a / b;
}

void main() {
  vec3 color = texture(uInput, vUV).rgb;
  color = ACES_INPUT_MAT * color;
  color = rrt_and_odt_fit(color);
  color = ACES_OUTPUT_MAT * color;
  color = clamp(color, 0.0, 1.0);
  fragColor = vec4(color, 1.0);
}

