// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The game's own shaders, as the original's materials make them – written by
 * tools/gen-game-glsl.mjs from the original's code, do not edit.
 */
#include "render/glsl_gen.h"

#include <cstring>

namespace fs {

const GameShader GAME_SHADERS[] = {
{"ground", "fs-ground",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>
varying vec3 vFsWorld;
attribute float amat;
flat varying float vFsMat;
#include <batching_pars_vertex>
#include <uv_pars_vertex>
#include <displacementmap_pars_vertex>
#include <envmap_pars_vertex>
#include <color_pars_vertex>
#include <fog_pars_vertex>
#include <normal_pars_vertex>
#include <morphtarget_pars_vertex>
#include <skinning_pars_vertex>
#include <shadowmap_pars_vertex>
#include <logdepthbuf_pars_vertex>
#include <clipping_planes_pars_vertex>
void main() {
	#include <uv_vertex>
	#include <color_vertex>
	#include <morphinstance_vertex>
	#include <morphcolor_vertex>
	#include <batching_vertex>
	#include <beginnormal_vertex>
	#include <morphnormal_vertex>
	#include <skinbase_vertex>
	#include <skinnormal_vertex>
	#include <defaultnormal_vertex>
	#include <normal_vertex>
	#include <begin_vertex>
vFsWorld = (modelMatrix * vec4(transformed, 1.0)).xyz;
vFsMat = amat;
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>
	#include <logdepthbuf_vertex>
	#include <clipping_planes_vertex>
	vViewPosition = - mvPosition.xyz;
	#include <worldpos_vertex>
	#include <envmap_vertex>
	#include <shadowmap_vertex>
	#include <fog_vertex>
})GLSL",
R"GLSL(#define LAMBERT
uniform vec3 diffuse;
uniform vec3 emissive;
uniform float opacity;
#include <common>
varying vec3 vFsWorld;
flat varying float vFsMat;

float fsHash(vec2 p) {
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}
float fsNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsHash(i), fsHash(i + vec2(1.0, 0.0)), f.x),
             mix(fsHash(i + vec2(0.0, 1.0)), fsHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
vec3 fsLin(vec3 c) { return pow(c, vec3(2.2)); }


// joint mask of a rectangular paving pattern, 1 = joint
float fsJoints(vec2 p, vec2 size, float bond, float w) {
  vec2 g = p / size;
  g.x += bond * floor(g.y);
  vec2 f = fract(g);
  vec2 d = min(f, 1.0 - f) * size;
  return 1.0 - smoothstep(w * 0.5, w, min(d.x, d.y));
}
vec3 fsGround(float m, vec3 wp, float dist) {
  vec2 p = wp.xz;
  // size of one pixel on the ground: detail finer than that is faded out
  // instead of aliasing into moire patterns at grazing angles
  float fw = max(fwidth(p.x), fwidth(p.y));
  float fine = (1.0 - smoothstep(10.0, 45.0, dist)) * (1.0 - smoothstep(0.02, 0.07, fw));
  float mid = (1.0 - smoothstep(40.0, 220.0, dist)) * (1.0 - smoothstep(0.4, 1.6, fw));
  float patch_ = fsNoise(p * 0.31) - 0.5;
  float grain = (fsNoise(p * 17.0) - 0.5) * fine;
  float speck = (fsHash(floor(p * 55.0)) - 0.5) * fine * (1.0 - smoothstep(0.006, 0.02, fw));
  vec3 c;
  if (m < 0.5) {                       // asphalt
    c = vec3(0.325, 0.33, 0.345) * (1.0 + 0.16 * patch_ * mid) + grain * 0.06 + speck * 0.07;
  } else if (m < 1.5) {                // concrete slabs
    c = vec3(0.60, 0.60, 0.58) * (1.0 + 0.10 * patch_ * mid) + grain * 0.05;
    c *= 1.0 - 0.35 * fsJoints(p, vec2(5.0, 3.6), 0.0, 0.03) * mid;
  } else if (m < 2.5) {                // paving stones
    c = vec3(0.56, 0.54, 0.52) * (1.0 + 0.12 * patch_ * mid) + grain * 0.05;
    c *= 1.0 - 0.30 * fsJoints(p, vec2(0.2, 0.1), 0.5, 0.012) * fine;
    c *= 0.94 + 0.12 * fsHash(floor(p / vec2(0.2, 0.1))) * fine;
  } else if (m < 3.5) {                // setts / cobbles
    c = vec3(0.43, 0.42, 0.41) * (1.0 + 0.14 * patch_ * mid) + grain * 0.06;
    c *= 1.0 - 0.45 * fsJoints(p, vec2(0.13, 0.13), 0.5, 0.02) * fine;
    c *= 0.88 + 0.24 * fsHash(floor(p / 0.13)) * fine;
  } else if (m < 4.5) {                // gravel
    c = vec3(0.60, 0.57, 0.50) * (1.0 + 0.15 * patch_ * mid) + grain * 0.10 + speck * 0.12;
  } else if (m < 5.5) {                // dirt
    c = vec3(0.46, 0.39, 0.29) * (1.0 + 0.22 * patch_ * mid) + grain * 0.08 + speck * 0.05;
  } else if (m < 6.5) {                // grass track
    c = vec3(0.36, 0.47, 0.22) * (1.0 + 0.25 * patch_ * mid) + grain * 0.10;
  } else if (m < 7.5) {                // sidewalk slabs
    c = vec3(0.66, 0.65, 0.62) * (1.0 + 0.08 * patch_ * mid) + grain * 0.04;
    c *= 1.0 - 0.28 * fsJoints(p, vec2(0.5, 0.5), 0.5, 0.012) * fine;
  } else if (m < 8.5) {                // kerb stone
    c = vec3(0.72, 0.71, 0.69) + grain * 0.05;
    c *= 1.0 - 0.30 * fsJoints(p, vec2(1.0, 1.0), 0.0, 0.015) * fine;
  } else if (m < 9.5) {                // grass verge
    c = vec3(0.40, 0.52, 0.25) * (1.0 + 0.22 * patch_ * mid) + grain * 0.10 + speck * 0.05;
  } else if (m < 10.5) {               // worn asphalt
    c = vec3(0.40, 0.40, 0.40) * (1.0 + 0.2 * patch_ * mid) + grain * 0.07 + speck * 0.06;
  } else if (m < 11.5) {               // railway ballast
    c = vec3(0.44, 0.40, 0.37) + grain * 0.12 + speck * 0.14;
  } else if (m < 12.5) {               // footpath
    c = vec3(0.62, 0.59, 0.54) * (1.0 + 0.1 * patch_ * mid) + grain * 0.06;
  } else if (m < 13.5) {               // cycle path (red asphalt)
    c = vec3(0.56, 0.30, 0.26) * (1.0 + 0.12 * patch_ * mid) + grain * 0.05;
  } else if (m < 14.5) {               // parking bays beside the road (light paving)
    c = vec3(0.62, 0.61, 0.58) * (1.0 + 0.10 * patch_ * mid) + grain * 0.05;
    c *= 1.0 - 0.30 * fsJoints(p, vec2(0.2, 0.2), 0.5, 0.012) * fine;
  } else if (m < 20.5) {               // white paint
    c = vec3(0.93, 0.93, 0.91) * (0.9 + 0.1 * fsNoise(p * 6.0)) + grain * 0.06;
  } else if (m < 21.5) {               // yellow paint
    c = vec3(0.95, 0.78, 0.12) + grain * 0.05;
  } else if (m < 22.5) {               // red paint
    c = vec3(0.70, 0.20, 0.16) + grain * 0.05;
  } else {                             // blue paint
    c = vec3(0.15, 0.32, 0.70) + grain * 0.05;
  }
  return fsLin(clamp(c, 0.0, 1.0));
}

#include <dithering_pars_fragment>
#include <color_pars_fragment>
#include <uv_pars_fragment>
#include <map_pars_fragment>
#include <alphamap_pars_fragment>
#include <alphatest_pars_fragment>
#include <alphahash_pars_fragment>
#include <aomap_pars_fragment>
#include <lightmap_pars_fragment>
#include <emissivemap_pars_fragment>
#include <envmap_common_pars_fragment>
#include <envmap_pars_fragment>
#include <envmap_physical_pars_fragment>
#include <fog_pars_fragment>
#include <bsdfs>
#include <lights_pars_begin>
#include <normal_pars_fragment>
#include <lights_lambert_pars_fragment>
#include <shadowmap_pars_fragment>
#include <bumpmap_pars_fragment>
#include <normalmap_pars_fragment>
#include <specularmap_pars_fragment>
#include <logdepthbuf_pars_fragment>
#include <clipping_planes_pars_fragment>
void main() {
	vec4 diffuseColor = vec4( diffuse, opacity );
	#include <clipping_planes_fragment>
	ReflectedLight reflectedLight = ReflectedLight( vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ) );
	vec3 totalEmissiveRadiance = emissive;
	#include <logdepthbuf_fragment>
	#include <map_fragment>
	#include <color_fragment>
diffuseColor.rgb = fsGround(vFsMat, vFsWorld, distance(vFsWorld, cameraPosition));
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	#include <emissivemap_fragment>
	#include <lights_lambert_fragment>
)GLSL"
R"GLSL(	#include <lights_fragment_begin>
	#include <lights_fragment_maps>
	#include <lights_fragment_end>
	#include <aomap_fragment>
	vec3 outgoingLight = reflectedLight.directDiffuse + reflectedLight.indirectDiffuse + totalEmissiveRadiance;
	#include <envmap_fragment>
	#include <opaque_fragment>
	#include <tonemapping_fragment>
	#include <colorspace_fragment>
	#include <fog_fragment>
	#include <premultiplied_alpha_fragment>
	#include <dithering_fragment>
})GLSL",
""},
{"paint", "fs-paint",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>
varying vec3 vFsWorld;
attribute float amat;
flat varying float vFsMat;
#include <batching_pars_vertex>
#include <uv_pars_vertex>
#include <displacementmap_pars_vertex>
#include <envmap_pars_vertex>
#include <color_pars_vertex>
#include <fog_pars_vertex>
#include <normal_pars_vertex>
#include <morphtarget_pars_vertex>
#include <skinning_pars_vertex>
#include <shadowmap_pars_vertex>
#include <logdepthbuf_pars_vertex>
#include <clipping_planes_pars_vertex>
void main() {
	#include <uv_vertex>
	#include <color_vertex>
	#include <morphinstance_vertex>
	#include <morphcolor_vertex>
	#include <batching_vertex>
	#include <beginnormal_vertex>
	#include <morphnormal_vertex>
	#include <skinbase_vertex>
	#include <skinnormal_vertex>
	#include <defaultnormal_vertex>
	#include <normal_vertex>
	#include <begin_vertex>
vFsWorld = (modelMatrix * vec4(transformed, 1.0)).xyz;
vFsMat = amat;
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>
	#include <logdepthbuf_vertex>
	#include <clipping_planes_vertex>
	vViewPosition = - mvPosition.xyz;
	#include <worldpos_vertex>
	#include <envmap_vertex>
	#include <shadowmap_vertex>
	#include <fog_vertex>
})GLSL",
R"GLSL(#define LAMBERT
uniform vec3 diffuse;
uniform vec3 emissive;
uniform float opacity;
#include <common>
varying vec3 vFsWorld;
flat varying float vFsMat;

float fsHash(vec2 p) {
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}
float fsNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsHash(i), fsHash(i + vec2(1.0, 0.0)), f.x),
             mix(fsHash(i + vec2(0.0, 1.0)), fsHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
vec3 fsLin(vec3 c) { return pow(c, vec3(2.2)); }


// joint mask of a rectangular paving pattern, 1 = joint
float fsJoints(vec2 p, vec2 size, float bond, float w) {
  vec2 g = p / size;
  g.x += bond * floor(g.y);
  vec2 f = fract(g);
  vec2 d = min(f, 1.0 - f) * size;
  return 1.0 - smoothstep(w * 0.5, w, min(d.x, d.y));
}
vec3 fsGround(float m, vec3 wp, float dist) {
  vec2 p = wp.xz;
  // size of one pixel on the ground: detail finer than that is faded out
  // instead of aliasing into moire patterns at grazing angles
  float fw = max(fwidth(p.x), fwidth(p.y));
  float fine = (1.0 - smoothstep(10.0, 45.0, dist)) * (1.0 - smoothstep(0.02, 0.07, fw));
  float mid = (1.0 - smoothstep(40.0, 220.0, dist)) * (1.0 - smoothstep(0.4, 1.6, fw));
  float patch_ = fsNoise(p * 0.31) - 0.5;
  float grain = (fsNoise(p * 17.0) - 0.5) * fine;
  float speck = (fsHash(floor(p * 55.0)) - 0.5) * fine * (1.0 - smoothstep(0.006, 0.02, fw));
  vec3 c;
  if (m < 0.5) {                       // asphalt
    c = vec3(0.325, 0.33, 0.345) * (1.0 + 0.16 * patch_ * mid) + grain * 0.06 + speck * 0.07;
  } else if (m < 1.5) {                // concrete slabs
    c = vec3(0.60, 0.60, 0.58) * (1.0 + 0.10 * patch_ * mid) + grain * 0.05;
    c *= 1.0 - 0.35 * fsJoints(p, vec2(5.0, 3.6), 0.0, 0.03) * mid;
  } else if (m < 2.5) {                // paving stones
    c = vec3(0.56, 0.54, 0.52) * (1.0 + 0.12 * patch_ * mid) + grain * 0.05;
    c *= 1.0 - 0.30 * fsJoints(p, vec2(0.2, 0.1), 0.5, 0.012) * fine;
    c *= 0.94 + 0.12 * fsHash(floor(p / vec2(0.2, 0.1))) * fine;
  } else if (m < 3.5) {                // setts / cobbles
    c = vec3(0.43, 0.42, 0.41) * (1.0 + 0.14 * patch_ * mid) + grain * 0.06;
    c *= 1.0 - 0.45 * fsJoints(p, vec2(0.13, 0.13), 0.5, 0.02) * fine;
    c *= 0.88 + 0.24 * fsHash(floor(p / 0.13)) * fine;
  } else if (m < 4.5) {                // gravel
    c = vec3(0.60, 0.57, 0.50) * (1.0 + 0.15 * patch_ * mid) + grain * 0.10 + speck * 0.12;
  } else if (m < 5.5) {                // dirt
    c = vec3(0.46, 0.39, 0.29) * (1.0 + 0.22 * patch_ * mid) + grain * 0.08 + speck * 0.05;
  } else if (m < 6.5) {                // grass track
    c = vec3(0.36, 0.47, 0.22) * (1.0 + 0.25 * patch_ * mid) + grain * 0.10;
  } else if (m < 7.5) {                // sidewalk slabs
    c = vec3(0.66, 0.65, 0.62) * (1.0 + 0.08 * patch_ * mid) + grain * 0.04;
    c *= 1.0 - 0.28 * fsJoints(p, vec2(0.5, 0.5), 0.5, 0.012) * fine;
  } else if (m < 8.5) {                // kerb stone
    c = vec3(0.72, 0.71, 0.69) + grain * 0.05;
    c *= 1.0 - 0.30 * fsJoints(p, vec2(1.0, 1.0), 0.0, 0.015) * fine;
  } else if (m < 9.5) {                // grass verge
    c = vec3(0.40, 0.52, 0.25) * (1.0 + 0.22 * patch_ * mid) + grain * 0.10 + speck * 0.05;
  } else if (m < 10.5) {               // worn asphalt
    c = vec3(0.40, 0.40, 0.40) * (1.0 + 0.2 * patch_ * mid) + grain * 0.07 + speck * 0.06;
  } else if (m < 11.5) {               // railway ballast
    c = vec3(0.44, 0.40, 0.37) + grain * 0.12 + speck * 0.14;
  } else if (m < 12.5) {               // footpath
    c = vec3(0.62, 0.59, 0.54) * (1.0 + 0.1 * patch_ * mid) + grain * 0.06;
  } else if (m < 13.5) {               // cycle path (red asphalt)
    c = vec3(0.56, 0.30, 0.26) * (1.0 + 0.12 * patch_ * mid) + grain * 0.05;
  } else if (m < 14.5) {               // parking bays beside the road (light paving)
    c = vec3(0.62, 0.61, 0.58) * (1.0 + 0.10 * patch_ * mid) + grain * 0.05;
    c *= 1.0 - 0.30 * fsJoints(p, vec2(0.2, 0.2), 0.5, 0.012) * fine;
  } else if (m < 20.5) {               // white paint
    c = vec3(0.93, 0.93, 0.91) * (0.9 + 0.1 * fsNoise(p * 6.0)) + grain * 0.06;
  } else if (m < 21.5) {               // yellow paint
    c = vec3(0.95, 0.78, 0.12) + grain * 0.05;
  } else if (m < 22.5) {               // red paint
    c = vec3(0.70, 0.20, 0.16) + grain * 0.05;
  } else {                             // blue paint
    c = vec3(0.15, 0.32, 0.70) + grain * 0.05;
  }
  return fsLin(clamp(c, 0.0, 1.0));
}

#include <dithering_pars_fragment>
#include <color_pars_fragment>
#include <uv_pars_fragment>
#include <map_pars_fragment>
#include <alphamap_pars_fragment>
#include <alphatest_pars_fragment>
#include <alphahash_pars_fragment>
#include <aomap_pars_fragment>
#include <lightmap_pars_fragment>
#include <emissivemap_pars_fragment>
#include <envmap_common_pars_fragment>
#include <envmap_pars_fragment>
#include <envmap_physical_pars_fragment>
#include <fog_pars_fragment>
#include <bsdfs>
#include <lights_pars_begin>
#include <normal_pars_fragment>
#include <lights_lambert_pars_fragment>
#include <shadowmap_pars_fragment>
#include <bumpmap_pars_fragment>
#include <normalmap_pars_fragment>
#include <specularmap_pars_fragment>
#include <logdepthbuf_pars_fragment>
#include <clipping_planes_pars_fragment>
void main() {
	vec4 diffuseColor = vec4( diffuse, opacity );
	#include <clipping_planes_fragment>
	ReflectedLight reflectedLight = ReflectedLight( vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ) );
	vec3 totalEmissiveRadiance = emissive;
	#include <logdepthbuf_fragment>
	#include <map_fragment>
	#include <color_fragment>
diffuseColor.rgb = fsGround(vFsMat, vFsWorld, distance(vFsWorld, cameraPosition));
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	#include <emissivemap_fragment>
	#include <lights_lambert_fragment>
)GLSL"
R"GLSL(	#include <lights_fragment_begin>
	#include <lights_fragment_maps>
	#include <lights_fragment_end>
	#include <aomap_fragment>
	vec3 outgoingLight = reflectedLight.directDiffuse + reflectedLight.indirectDiffuse + totalEmissiveRadiance;
	#include <envmap_fragment>
	#include <opaque_fragment>
	#include <tonemapping_fragment>
	#include <colorspace_fragment>
	#include <fog_fragment>
	#include <premultiplied_alpha_fragment>
	#include <dithering_fragment>
})GLSL",
""},
{"terrain", "fs-terrain",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>
varying vec3 vFsWorld;

#include <batching_pars_vertex>
#include <uv_pars_vertex>
#include <displacementmap_pars_vertex>
#include <envmap_pars_vertex>
#include <color_pars_vertex>
#include <fog_pars_vertex>
#include <normal_pars_vertex>
#include <morphtarget_pars_vertex>
#include <skinning_pars_vertex>
#include <shadowmap_pars_vertex>
#include <logdepthbuf_pars_vertex>
#include <clipping_planes_pars_vertex>
void main() {
	#include <uv_vertex>
	#include <color_vertex>
	#include <morphinstance_vertex>
	#include <morphcolor_vertex>
	#include <batching_vertex>
	#include <beginnormal_vertex>
	#include <morphnormal_vertex>
	#include <skinbase_vertex>
	#include <skinnormal_vertex>
	#include <defaultnormal_vertex>
	#include <normal_vertex>
	#include <begin_vertex>
vFsWorld = (modelMatrix * vec4(transformed, 1.0)).xyz;

	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>
	#include <logdepthbuf_vertex>
	#include <clipping_planes_vertex>
	vViewPosition = - mvPosition.xyz;
	#include <worldpos_vertex>
	#include <envmap_vertex>
	#include <shadowmap_vertex>
	#include <fog_vertex>
})GLSL",
R"GLSL(#define LAMBERT
uniform vec3 diffuse;
uniform vec3 emissive;
uniform float opacity;
#include <common>
varying vec3 vFsWorld;


float fsHash(vec2 p) {
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}
float fsNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsHash(i), fsHash(i + vec2(1.0, 0.0)), f.x),
             mix(fsHash(i + vec2(0.0, 1.0)), fsHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
vec3 fsLin(vec3 c) { return pow(c, vec3(2.2)); }


#include <dithering_pars_fragment>
#include <color_pars_fragment>
#include <uv_pars_fragment>
#include <map_pars_fragment>
#include <alphamap_pars_fragment>
#include <alphatest_pars_fragment>
#include <alphahash_pars_fragment>
#include <aomap_pars_fragment>
#include <lightmap_pars_fragment>
#include <emissivemap_pars_fragment>
#include <envmap_common_pars_fragment>
#include <envmap_pars_fragment>
#include <envmap_physical_pars_fragment>
#include <fog_pars_fragment>
#include <bsdfs>
#include <lights_pars_begin>
#include <normal_pars_fragment>
#include <lights_lambert_pars_fragment>
#include <shadowmap_pars_fragment>
#include <bumpmap_pars_fragment>
#include <normalmap_pars_fragment>
#include <specularmap_pars_fragment>
#include <logdepthbuf_pars_fragment>
#include <clipping_planes_pars_fragment>
void main() {
	vec4 diffuseColor = vec4( diffuse, opacity );
	#include <clipping_planes_fragment>
	ReflectedLight reflectedLight = ReflectedLight( vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ) );
	vec3 totalEmissiveRadiance = emissive;
	#include <logdepthbuf_fragment>
	
#ifdef USE_MAP
#define FS_COVER_EDGE
vec3 fsCoverRGB;
{
  vec4 fsLin = texture2D(map, vMapUv);
  fsCoverRGB = fsLin.rgb;
  ivec2 fsN = textureSize(map, 0);
  vec2 fsP = vMapUv * vec2(fsN) - 0.5;
  vec2 fsDp = fwidth(fsP);
  float fsPx = max(fsDp.x, fsDp.y);
  vec2 fsDw = fwidth(vFsWorld.xz);
  float fsMpp = max(max(fsDw.x, fsDw.y), 1e-4);
  if (fsPx < 1.0) {
    vec2 fsI = floor(fsP);
    vec2 fsF = fsP - fsI;
    ivec2 fsI0 = ivec2(fsI);
    ivec2 fsHi = fsN - 1;
    vec4 fsT[4];
    fsT[0] = texelFetch(map, clamp(fsI0, ivec2(0), fsHi), 0);
    fsT[1] = texelFetch(map, clamp(fsI0 + ivec2(1, 0), ivec2(0), fsHi), 0);
    fsT[2] = texelFetch(map, clamp(fsI0 + ivec2(0, 1), ivec2(0), fsHi), 0);
    fsT[3] = texelFetch(map, clamp(fsI0 + ivec2(1, 1), ivec2(0), fsHi), 0);
    if (min(min(fsT[0].a, fsT[1].a), min(fsT[2].a, fsT[3].a)) > 0.01) {
      vec4 fsW = vec4((1.0 - fsF.x) * (1.0 - fsF.y), fsF.x * (1.0 - fsF.y), (1.0 - fsF.x) * fsF.y, fsF.x * fsF.y);
      vec4 fsD = vec4(fsT[0].a, fsT[1].a, fsT[2].a, fsT[3].a) * (255.0 / 80.0) - 0.1;
      // each texel's class: its signed distance interpolated here
      float fsPhi[4];
      for (int k = 0; k < 4; k++) {
        float p = 0.0;
        for (int i = 0; i < 4; i++) {
          vec3 e = fsT[i].rgb - fsT[k].rgb;
          p += fsW[i] * (dot(e, e) < 1e-7 ? fsD[i] : -fsD[i]);
        }
        fsPhi[k] = p;
      }
      int fsB = 0;
      for (int k = 1; k < 4; k++) if (fsPhi[k] > fsPhi[fsB]) fsB = k;
      vec3 fsCb = fsT[fsB].rgb;
      float fsSecond = -1e9;
      vec3 fsCs = fsCb;
      for (int k = 0; k < 4; k++) {
        vec3 e = fsT[k].rgb - fsCb;
        if (dot(e, e) >= 1e-7 && fsPhi[k] > fsSecond) {
          fsSecond = fsPhi[k];
          fsCs = fsT[k].rgb;
        }
      }
      vec3 fsSharp = fsCb;
      if (fsSecond > -1e8) {
        // (half the difference: the distance to the edge, m; smoothed over a pixel)
        float fsE = 0.5 * (fsPhi[fsB] - fsSecond);
        fsSharp = mix(fsCs, fsCb, clamp(fsE / fsMpp + 0.5, 0.5, 1.0));
      }
      fsCoverRGB = mix(fsSharp, fsLin.rgb, smoothstep(0.5, 1.0, fsPx));
    }
  }
  diffuseColor *= vec4(fsCoverRGB, 1.0);
}
#endif

      {
        float d = distance(vFsWorld, cameraPosition);
        float fine = 1.0 - smoothstep(8.0, 50.0, d);
        float mid = 1.0 - smoothstep(60.0, 400.0, d);
        float n = (fsNoise(vFsWorld.xz * 0.11) - 0.5) * 0.30 * (0.35 + 0.65 * mid)
                + (fsNoise(vFsWorld.xz * 1.3) - 0.5) * 0.22 * mid
                + (fsNoise(vFsWorld.xz * 19.0) - 0.5) * 0.30 * fine;
        diffuseColor.rgb *= 1.0 + n;
      }
	#include <color_fragment>
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	#include <emissivemap_fragment>
	#include <lights_lambert_fragment>
	#include <lights_fragment_begin>
	#include <lights_fragment_maps>
	#include <lights_fragment_end>
	#include <aomap_fragment>
	vec3 outgoingLight = reflectedLight.directDiffuse + reflectedLight.indirectDiffuse + totalEmissiveRadiance;
	#include <envmap_fragment>
	#include <opaque_fragment>
	#include <tonemapping_fragment>
	#include <colorspace_fragment>
	#include <fog_fragment>
	#include <premultiplied_alpha_fragment>
	#include <dithering_fragment>
})GLSL",
""},
{"building", "fs-bldg",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>
attribute vec4 astyle;
attribute vec4 auvd;
flat varying vec4 vFsStyle;
varying vec4 vFsUvd;
varying vec3 vFsWorld;

#include <batching_pars_vertex>
#include <uv_pars_vertex>
#include <displacementmap_pars_vertex>
#include <envmap_pars_vertex>
#include <color_pars_vertex>
#include <fog_pars_vertex>
#include <normal_pars_vertex>
#include <morphtarget_pars_vertex>
#include <skinning_pars_vertex>
#include <shadowmap_pars_vertex>
#include <logdepthbuf_pars_vertex>
#include <clipping_planes_pars_vertex>
void main() {
	#include <uv_vertex>
	#include <color_vertex>
	#include <morphinstance_vertex>
	#include <morphcolor_vertex>
	#include <batching_vertex>
	#include <beginnormal_vertex>
	#include <morphnormal_vertex>
	#include <skinbase_vertex>
	#include <skinnormal_vertex>
	#include <defaultnormal_vertex>
	#include <normal_vertex>
	#include <begin_vertex>
vFsStyle = astyle;
vFsUvd = auvd * 0.02;
vFsWorld = (modelMatrix * vec4(transformed, 1.0)).xyz;

	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>
	#include <logdepthbuf_vertex>
	#include <clipping_planes_vertex>
	vViewPosition = - mvPosition.xyz;
	#include <worldpos_vertex>
	#include <envmap_vertex>
	#include <shadowmap_vertex>
	#include <fog_vertex>
})GLSL",
R"GLSL(#define LAMBERT
uniform vec3 diffuse;
uniform vec3 emissive;
uniform float opacity;
#include <common>
varying vec3 vFsWorld;


float fsHash(vec2 p) {
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}
float fsNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsHash(i), fsHash(i + vec2(1.0, 0.0)), f.x),
             mix(fsHash(i + vec2(0.0, 1.0)), fsHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
vec3 fsLin(vec3 c) { return pow(c, vec3(2.2)); }

flat varying vec4 vFsStyle;
varying vec4 vFsUvd;

// uv.xy = position on the wall in metres (x along the wall, y above its foot),
// uv2.xy = wall length and eave height.
vec3 fsFacade(float style, vec3 base, vec4 uvd, vec3 wp, float dist, out float glass) {
  glass = 0.0;
  // a wall with measured details (render/facadedetail.ts) has its style + 16: its windows and doors are geometry
  float measured = step(15.5, style);
  style -= 16.0 * measured;
  float fine = 1.0 - smoothstep(25.0, 110.0, dist);
  float mid = 1.0 - smoothstep(120.0, 420.0, dist);
  float grain = (fsNoise(wp.xz * 9.0 + wp.y * 7.0) - 0.5) * 0.06 * fine;
  vec3 c = base + grain;
  if (style > 7.5) {
    // roofs: 8 tiles, 9 flat, 10 metal
    if (style < 8.5) {
      float row = fract(uvd.y / 0.33);
      float col = fract(uvd.x / 0.24 + 0.5 * floor(uvd.y / 0.33));
      c *= 0.86 + 0.14 * smoothstep(0.0, 0.25, row) * fine + (1.0 - fine) * 0.07;
      c *= 1.0 - 0.10 * step(col, 0.08) * fine;
    } else if (style < 9.5) {
      c *= 0.95 + 0.10 * fsNoise(wp.xz * 1.7);
    } else {
      c *= 0.92 + 0.08 * step(0.5, fract(uvd.x / 0.5)) * fine;
    }
    return c;
  }
  float L = max(uvd.z, 0.1);
  float H = max(uvd.w, 0.1);
  // plinth
  c *= 1.0 - 0.18 * (1.0 - smoothstep(0.35, 0.45, uvd.y)) * mid;
  // facing brick (6): courses of bricks with mortar joints – near by, on the buildings with facades
  float brick = step(5.5, style) * step(style, 6.5);
  if (brick > 0.5) {
    float course = uvd.y / 0.0625;
    float bx = uvd.x / 0.25 + 0.5 * mod(floor(course), 2.0);
    vec2 bid = vec2(floor(bx), floor(course));
    float joint = max(1.0 - smoothstep(0.0, 0.09, fract(course)), 1.0 - smoothstep(0.0, 0.04, fract(bx)));
    c *= mix(1.0, 0.86 + 0.24 * fsHash(bid + floor(wp.xz)), fine);
    c = mix(c, vec3(0.62, 0.60, 0.56), joint * fine * 0.55);
    c *= 1.0 - 0.06 * fine * (fsNoise(wp.xz * 0.9 + wp.y * 0.7) - 0.5);
  }
  if (measured < 0.5 && (style < 0.5 || (style > 1.5 && style < 4.5) || brick > 0.5)) {
    // regular window grid that always fits the wall
    float house = style < 0.5 || brick > 0.5 ? 1.0 : 0.0;
    float floorH = house > 0.5 ? 2.75 : 3.3;
    float bay = house > 0.5 ? 2.6 : (style < 2.5 ? 3.4 : 3.0);
    float rows = max(1.0, floor(H / floorH + 0.35));
    float cols = max(1.0, floor(L / bay + 0.3));
    if (L > 1.7 && H > 2.1) {
      vec2 cell = vec2(uvd.x / L * cols, uvd.y / H * rows);
      vec2 id = floor(cell);
      vec2 f = fract(cell);
      vec2 cs = vec2(L / cols, H / rows);          // cell size in metres
      vec2 win = house > 0.5 ? vec2(1.15, 1.30) : (style < 2.5 ? vec2(cs.x * 0.8, 1.6) : vec2(1.3, 1.7));
      win = min(win, cs * vec2(0.75, 0.62));
      vec2 lo = (cs - win) * vec2(0.5, 0.42);
      vec2 q = f * cs;
      float inside = step(lo.x, q.x) * step(q.x, lo.x + win.x) * step(lo.y, q.y) * step(q.y, lo.y + win.y);
      float below = step(uvd.y, H);                // no windows in the gable triangle
      float rnd = fsHash(id + floor(wp.xz * 0.05));
      // a door on the ground floor now and then
      float door = step(id.y, 0.5) * step(0.82, rnd) * step(abs(q.x - cs.x * 0.5), 0.5) * step(q.y, 2.05) * house;
      float w = inside * below * mid * (1.0 - door);
      // frame
      vec2 e = min(q - lo, lo + win - q);
      float frame = (1.0 - smoothstep(0.03, 0.07, min(e.x, e.y))) * w * fine;
      float mullion = (1.0 - smoothstep(0.015, 0.035, abs(q.x - (lo.x + win.x * 0.5)))) * w * fine;
      vec3 glassCol = mix(vec3(0.10, 0.14, 0.19), vec3(0.32, 0.42, 0.52), 0.35 + 0.5 * rnd);
      c = mix(c, glassCol, w);
      c = mix(c, vec3(0.88, 0.88, 0.86), max(frame, mullion * 0.8));
      c = mix(c, base * 0.45 + vec3(0.12, 0.09, 0.06), door * mid);
      glass = w * (1.0 - max(frame, mullion));
    }
  } else if (style < 1.5 && measured < 0.5) {
    // garage: one sectional door on walls wide enough
    if (L > 2.4 && L < 7.5 && H > 2.0) {
      float cols = max(1.0, floor(L / 3.0));
      float cw = L / cols;
      float qx = fract(uvd.x / cw) * cw;
      float door = step(abs(qx - cw * 0.5), min(1.2, cw * 0.4)) * step(uvd.y, 2.05) * step(0.5, fsHash(floor(wp.xz * 0.21)));
      float ribs = 0.92 + 0.08 * step(0.5, fract(uvd.y / 0.5));
      c = mix(c, vec3(0.78, 0.78, 0.76) * ribs, door * mid);
    }
  }
  return c;
}

#include <dithering_pars_fragment>
#include <color_pars_fragment>
#include <uv_pars_fragment>
#include <map_pars_fragment>
#include <alphamap_pars_fragment>
#include <alphatest_pars_fragment>
#include <alphahash_pars_fragment>
#include <aomap_pars_fragment>
#include <lightmap_pars_fragment>
#include <emissivemap_pars_fragment>
#include <envmap_common_pars_fragment>
#include <envmap_pars_fragment>
#include <envmap_physical_pars_fragment>
#include <fog_pars_fragment>
#include <bsdfs>
#include <lights_pars_begin>
#include <normal_pars_fragment>
#include <lights_lambert_pars_fragment>
#include <shadowmap_pars_fragment>
#include <bumpmap_pars_fragment>
#include <normalmap_pars_fragment>
#include <specularmap_pars_fragment>
#include <logdepthbuf_pars_fragment>
#include <clipping_planes_pars_fragment>
void main() {
	vec4 diffuseColor = vec4( diffuse, opacity );
	#include <clipping_planes_fragment>
	ReflectedLight reflectedLight = ReflectedLight( vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ) );
	vec3 totalEmissiveRadiance = emissive;
	#include <logdepthbuf_fragment>
	#include <map_fragment>
	#include <color_fragment>
)GLSL"
R"GLSL(        float fsGlass;
        vec3 fsBase = vFsStyle.yzw / 255.0;
        diffuseColor.rgb = fsLin(clamp(fsFacade(vFsStyle.x, fsBase, vFsUvd, vFsWorld, distance(vFsWorld, cameraPosition), fsGlass), 0.0, 1.0));
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	#include <emissivemap_fragment>
	#include <lights_lambert_fragment>
	#include <lights_fragment_begin>
	#include <lights_fragment_maps>
	#include <lights_fragment_end>
	#include <aomap_fragment>
	vec3 outgoingLight = reflectedLight.directDiffuse + reflectedLight.indirectDiffuse + totalEmissiveRadiance;
	#include <envmap_fragment>
	#include <opaque_fragment>
	#include <tonemapping_fragment>
	#include <colorspace_fragment>
	#include <fog_fragment>
	#include <premultiplied_alpha_fragment>
	#include <dithering_fragment>
})GLSL",
""},
{"buildingPlain", "fs-bldg-plain",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>
attribute vec4 astyle;
flat varying vec3 vFsBase;
#include <batching_pars_vertex>
#include <uv_pars_vertex>
#include <displacementmap_pars_vertex>
#include <envmap_pars_vertex>
#include <color_pars_vertex>
#include <fog_pars_vertex>
#include <normal_pars_vertex>
#include <morphtarget_pars_vertex>
#include <skinning_pars_vertex>
#include <shadowmap_pars_vertex>
#include <logdepthbuf_pars_vertex>
#include <clipping_planes_pars_vertex>
void main() {
	#include <uv_vertex>
	#include <color_vertex>
	#include <morphinstance_vertex>
	#include <morphcolor_vertex>
	#include <batching_vertex>
	#include <beginnormal_vertex>
	#include <morphnormal_vertex>
	#include <skinbase_vertex>
	#include <skinnormal_vertex>
	#include <defaultnormal_vertex>
	#include <normal_vertex>
	#include <begin_vertex>
vFsBase = astyle.yzw / 255.0;
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>
	#include <logdepthbuf_vertex>
	#include <clipping_planes_vertex>
	vViewPosition = - mvPosition.xyz;
	#include <worldpos_vertex>
	#include <envmap_vertex>
	#include <shadowmap_vertex>
	#include <fog_vertex>
})GLSL",
R"GLSL(#define LAMBERT
uniform vec3 diffuse;
uniform vec3 emissive;
uniform float opacity;
#include <common>
flat varying vec3 vFsBase;
#include <dithering_pars_fragment>
#include <color_pars_fragment>
#include <uv_pars_fragment>
#include <map_pars_fragment>
#include <alphamap_pars_fragment>
#include <alphatest_pars_fragment>
#include <alphahash_pars_fragment>
#include <aomap_pars_fragment>
#include <lightmap_pars_fragment>
#include <emissivemap_pars_fragment>
#include <envmap_common_pars_fragment>
#include <envmap_pars_fragment>
#include <envmap_physical_pars_fragment>
#include <fog_pars_fragment>
#include <bsdfs>
#include <lights_pars_begin>
#include <normal_pars_fragment>
#include <lights_lambert_pars_fragment>
#include <shadowmap_pars_fragment>
#include <bumpmap_pars_fragment>
#include <normalmap_pars_fragment>
#include <specularmap_pars_fragment>
#include <logdepthbuf_pars_fragment>
#include <clipping_planes_pars_fragment>
void main() {
	vec4 diffuseColor = vec4( diffuse, opacity );
	#include <clipping_planes_fragment>
	ReflectedLight reflectedLight = ReflectedLight( vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ) );
	vec3 totalEmissiveRadiance = emissive;
	#include <logdepthbuf_fragment>
	#include <map_fragment>
	#include <color_fragment>
diffuseColor.rgb = pow(vFsBase, vec3(2.2));
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	#include <emissivemap_fragment>
	#include <lights_lambert_fragment>
	#include <lights_fragment_begin>
	#include <lights_fragment_maps>
	#include <lights_fragment_end>
	#include <aomap_fragment>
	vec3 outgoingLight = reflectedLight.directDiffuse + reflectedLight.indirectDiffuse + totalEmissiveRadiance;
	#include <envmap_fragment>
	#include <opaque_fragment>
	#include <tonemapping_fragment>
	#include <colorspace_fragment>
	#include <fog_fragment>
	#include <premultiplied_alpha_fragment>
	#include <dithering_fragment>
})GLSL",
""},
};
const int GAME_SHADER_COUNT = 5;

const GameShader* gameShader(const char* name) {
  for (int i = 0; i < GAME_SHADER_COUNT; i++)
    if (!std::strcmp(GAME_SHADERS[i].name, name)) return &GAME_SHADERS[i];
  return nullptr;
}

}  // namespace fs
