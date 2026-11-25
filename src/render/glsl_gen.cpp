// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The game's own shaders, as the original's materials make them – written by
 * tools/gen-game-glsl.mjs from the original's code, do not edit.
 */
#include "render/glsl_gen.h"

#include <cstring>

namespace fs {

const GameShader GAME_SHADERS[] = {
{"people", "fs-people",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>
attribute float aVar;
attribute float aMask;
attribute vec3 aColA;
attribute vec3 aColB;
attribute float aSlot;
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
vColor.rgb = color * (aSlot < 0.5 ? aColA : (aSlot < 1.5 ? aColB : vec3(1.0)));
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
if (((int(aMask + 0.5) >> int(aVar + 0.5)) & 1) == 0) transformed = vec3(0.0);
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
{"signBack", "fs-sign-back",
R"GLSL(#include <common>
#include <batching_pars_vertex>
#include <uv_pars_vertex>
#include <envmap_pars_vertex>
#include <color_pars_vertex>
#include <fog_pars_vertex>
#include <morphtarget_pars_vertex>
#include <skinning_pars_vertex>
#include <logdepthbuf_pars_vertex>
#include <clipping_planes_pars_vertex>
void main() {
	#include <uv_vertex>
	#include <color_vertex>
	#include <morphinstance_vertex>
	#include <morphcolor_vertex>
	#include <batching_vertex>
	#if defined ( USE_ENVMAP ) || defined ( USE_SKINNING )
		#include <beginnormal_vertex>
		#include <morphnormal_vertex>
		#include <skinbase_vertex>
		#include <skinnormal_vertex>
		#include <defaultnormal_vertex>
	#endif
	#include <begin_vertex>
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <project_vertex>
	#include <logdepthbuf_vertex>
	#include <clipping_planes_vertex>
	#include <worldpos_vertex>
	#include <envmap_vertex>
	#include <fog_vertex>
})GLSL",
R"GLSL(uniform vec3 diffuse;
uniform float opacity;
#ifndef FLAT_SHADED
	varying vec3 vNormal;
#endif
#include <common>
#include <dithering_pars_fragment>
#include <color_pars_fragment>
#include <uv_pars_fragment>
#include <map_pars_fragment>
#include <alphamap_pars_fragment>
#include <alphatest_pars_fragment>
#include <alphahash_pars_fragment>
#include <aomap_pars_fragment>
#include <lightmap_pars_fragment>
#include <envmap_common_pars_fragment>
#include <envmap_pars_fragment>
#include <fog_pars_fragment>
#include <specularmap_pars_fragment>
#include <logdepthbuf_pars_fragment>
#include <clipping_planes_pars_fragment>
void main() {
	vec4 diffuseColor = vec4( diffuse, opacity );
	#include <clipping_planes_fragment>
	#include <logdepthbuf_fragment>
	#include <map_fragment>
diffuseColor.rgb = vec3(0.30, 0.32, 0.33);
	#include <color_fragment>
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
	ReflectedLight reflectedLight = ReflectedLight( vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ) );
	#ifdef USE_LIGHTMAP
		vec4 lightMapTexel = texture2D( lightMap, vLightMapUv );
		reflectedLight.indirectDiffuse += lightMapTexel.rgb * lightMapIntensity * RECIPROCAL_PI;
	#else
		reflectedLight.indirectDiffuse += vec3( 1.0 );
	#endif
	#include <aomap_fragment>
	reflectedLight.indirectDiffuse *= diffuseColor.rgb;
	vec3 outgoingLight = reflectedLight.indirectDiffuse;
	#include <envmap_fragment>
	#include <opaque_fragment>
	#include <tonemapping_fragment>
	#include <colorspace_fragment>
	#include <fog_fragment>
	#include <premultiplied_alpha_fragment>
	#include <dithering_fragment>
})GLSL",
""},
{"far", "fs-far",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>
varying vec2 vFarXZ;
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
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>
	#include <logdepthbuf_vertex>
	#include <clipping_planes_vertex>
	vViewPosition = - mvPosition.xyz;
	#include <worldpos_vertex>
vFarXZ = (modelMatrix * vec4(transformed, 1.0)).xz;
	#include <envmap_vertex>
	#include <shadowmap_vertex>
	#include <fog_vertex>
})GLSL",
R"GLSL(#define LAMBERT
uniform vec3 diffuse;
uniform vec3 emissive;
uniform float opacity;
#include <common>
varying vec2 vFarXZ;
uniform float uInner;
uniform vec2 uCam;
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
  if (distance(vFarXZ, uCam) < uInner) discard;
	vec4 diffuseColor = vec4( diffuse, opacity );
	#include <clipping_planes_fragment>
	ReflectedLight reflectedLight = ReflectedLight( vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ) );
	vec3 totalEmissiveRadiance = emissive;
	#include <logdepthbuf_fragment>
	#include <map_fragment>
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
"uInner uCam"},
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
{"fs-terrain|fs-season-terrain", "fs-terrain|fs-season-terrain",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;

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

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}

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
)GLSL"
R"GLSL(        float fine = 1.0 - smoothstep(8.0, 50.0, d);
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
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = fsSeasonGround(diffuseColor.rgb);
  // (no snow on the water)
  float fsWater = step(diffuseColor.r + 0.05, diffuseColor.b) * step(diffuseColor.g, diffuseColor.b);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.77582, 0.81485, 0.87137), fsSnowCover(uFsSnow, fsUp, 0.55, 0.8, vFsSeasonXZ) * (1.0 - fsWater));
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"fs-far|fs-season-far", "fs-far|fs-season-far",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;

varying vec2 vFarXZ;
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
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

	#include <logdepthbuf_vertex>
	#include <clipping_planes_vertex>
	vViewPosition = - mvPosition.xyz;
	#include <worldpos_vertex>
vFarXZ = (modelMatrix * vec4(transformed, 1.0)).xz;
	#include <envmap_vertex>
	#include <shadowmap_vertex>
	#include <fog_vertex>
})GLSL",
R"GLSL(#define LAMBERT
uniform vec3 diffuse;
uniform vec3 emissive;
uniform float opacity;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}

varying vec2 vFarXZ;
uniform float uInner;
uniform vec2 uCam;
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
  if (distance(vFarXZ, uCam) < uInner) discard;
	vec4 diffuseColor = vec4( diffuse, opacity );
	#include <clipping_planes_fragment>
	ReflectedLight reflectedLight = ReflectedLight( vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ) );
	vec3 totalEmissiveRadiance = emissive;
	#include <logdepthbuf_fragment>
	#include <map_fragment>
	#include <color_fragment>
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = fsSeasonGround(diffuseColor.rgb);
  // (no snow on the water)
  float fsWater = step(diffuseColor.r + 0.05, diffuseColor.b) * step(diffuseColor.g, diffuseColor.b);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.77582, 0.81485, 0.87137), fsSnowCover(uFsSnow, fsUp, 0.55, 0.8, vFsSeasonXZ) * (1.0 - fsWater));
}
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
"uInner uCam uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"fs-ground|fs-season-ground", "fs-ground|fs-season-ground",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;

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

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}

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
)GLSL"
R"GLSL(  } else if (m < 14.5) {               // parking bays beside the road (light paving)
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
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = fsSeasonGround(diffuseColor.rgb);
  float fsRoad = (vFsMat < 1.5 || abs(vFsMat - 3.0) < 0.5 || abs(vFsMat - 10.0) < 0.5 || abs(vFsMat - 13.0) < 0.5) ? 1.0 : 0.0;
  float fsS = fsSnowCover(mix(uFsSnow, uFsRoadSnow, fsRoad), fsUp, 0.55, 0.8, vFsSeasonXZ);
  diffuseColor.rgb = mix(diffuseColor.rgb, mix(vec3(0.77582, 0.81485, 0.87137), vec3(0.60514, 0.65188, 0.73195), fsRoad), fsS);
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"fs-ground|fs-season-ground|fs-wet", "fs-ground|fs-season-ground|fs-wet",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>
varying vec2 vFsWetXZ;

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;

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
vFsWetXZ = (modelMatrix * vec4(transformed, 1.0)).xz;

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

uniform float uFsWet;
uniform vec3 uFsWetSky;
varying vec2 vFsWetXZ;
float fsWetHash(vec2 p) {
  p = fract(p * vec2(233.34, 851.73));
  p += dot(p, p + 23.45);
  return fract(p.x * p.y);
}
float fsWetNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsWetHash(i), fsWetHash(i + vec2(1.0, 0.0)), f.x), mix(fsWetHash(i + vec2(0.0, 1.0)), fsWetHash(i + vec2(1.0, 1.0)), f.x), f.y);
}


varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}

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
)GLSL"
R"GLSL(  } else if (m < 11.5) {               // railway ballast
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
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = fsSeasonGround(diffuseColor.rgb);
  float fsRoad = (vFsMat < 1.5 || abs(vFsMat - 3.0) < 0.5 || abs(vFsMat - 10.0) < 0.5 || abs(vFsMat - 13.0) < 0.5) ? 1.0 : 0.0;
  float fsS = fsSnowCover(mix(uFsSnow, uFsRoadSnow, fsRoad), fsUp, 0.55, 0.8, vFsSeasonXZ);
  diffuseColor.rgb = mix(diffuseColor.rgb, mix(vec3(0.77582, 0.81485, 0.87137), vec3(0.60514, 0.65188, 0.73195), fsRoad), fsS);
}
#include <emissivemap_fragment>
	#include <lights_lambert_fragment>
	#include <lights_fragment_begin>
	#include <lights_fragment_maps>
	#include <lights_fragment_end>
	#include <aomap_fragment>
	vec3 outgoingLight = reflectedLight.directDiffuse + reflectedLight.indirectDiffuse + totalEmissiveRadiance;
	#include <envmap_fragment>
	
if (uFsWet > 0.001) {
  // A wet road: darker, and at a glancing angle the sky mirrored in its film of water –
  // most of all in the puddles, which come once it has rained a while.
  float fsC = clamp(dot(normalize(vViewPosition), normal), 0.0, 1.0);
  float fsF = 0.03 + 0.97 * pow(1.0 - fsC, 5.0);
  float fsPuddle = smoothstep(0.64, 0.74, fsWetNoise(vFsWetXZ * 0.23)) * smoothstep(0.55, 1.0, uFsWet);
  outgoingLight *= 1.0 - uFsWet * (0.38 + 0.25 * fsPuddle);
  outgoingLight = mix(outgoingLight, uFsWetSky, clamp(fsF * uFsWet * (0.45 + 0.5 * fsPuddle), 0.0, 1.0));
}

#include <opaque_fragment>
	#include <tonemapping_fragment>
	#include <colorspace_fragment>
	#include <fog_fragment>
	#include <premultiplied_alpha_fragment>
	#include <dithering_fragment>
})GLSL",
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow uFsWet uFsWetSky"},
{"fs-paint|fs-season-paint", "fs-paint|fs-season-paint",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;

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

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}

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
)GLSL"
R"GLSL(  } else if (m < 14.5) {               // parking bays beside the road (light paving)
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
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.60514, 0.65188, 0.73195), fsSnowCover(uFsRoadSnow, fsUp, 0.55, 0.8, vFsSeasonXZ));
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"fs-paint|fs-season-paint|fs-wet", "fs-paint|fs-season-paint|fs-wet",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>
varying vec2 vFsWetXZ;

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;

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
vFsWetXZ = (modelMatrix * vec4(transformed, 1.0)).xz;

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

uniform float uFsWet;
uniform vec3 uFsWetSky;
varying vec2 vFsWetXZ;
float fsWetHash(vec2 p) {
  p = fract(p * vec2(233.34, 851.73));
  p += dot(p, p + 23.45);
  return fract(p.x * p.y);
}
float fsWetNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsWetHash(i), fsWetHash(i + vec2(1.0, 0.0)), f.x), mix(fsWetHash(i + vec2(0.0, 1.0)), fsWetHash(i + vec2(1.0, 1.0)), f.x), f.y);
}


varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}

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
)GLSL"
R"GLSL(  } else if (m < 11.5) {               // railway ballast
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
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.60514, 0.65188, 0.73195), fsSnowCover(uFsRoadSnow, fsUp, 0.55, 0.8, vFsSeasonXZ));
}
#include <emissivemap_fragment>
	#include <lights_lambert_fragment>
	#include <lights_fragment_begin>
	#include <lights_fragment_maps>
	#include <lights_fragment_end>
	#include <aomap_fragment>
	vec3 outgoingLight = reflectedLight.directDiffuse + reflectedLight.indirectDiffuse + totalEmissiveRadiance;
	#include <envmap_fragment>
	
if (uFsWet > 0.001) {
  // A wet road: darker, and at a glancing angle the sky mirrored in its film of water –
  // most of all in the puddles, which come once it has rained a while.
  float fsC = clamp(dot(normalize(vViewPosition), normal), 0.0, 1.0);
  float fsF = 0.03 + 0.97 * pow(1.0 - fsC, 5.0);
  float fsPuddle = smoothstep(0.64, 0.74, fsWetNoise(vFsWetXZ * 0.23)) * smoothstep(0.55, 1.0, uFsWet);
  outgoingLight *= 1.0 - uFsWet * (0.38 + 0.25 * fsPuddle);
  outgoingLight = mix(outgoingLight, uFsWetSky, clamp(fsF * uFsWet * (0.45 + 0.5 * fsPuddle), 0.0, 1.0));
}

#include <opaque_fragment>
	#include <tonemapping_fragment>
	#include <colorspace_fragment>
	#include <fog_fragment>
	#include <premultiplied_alpha_fragment>
	#include <dithering_fragment>
})GLSL",
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow uFsWet uFsWetSky"},
{"fs-bldg|fs-season-building", "fs-bldg|fs-season-building",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;

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

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}

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
)GLSL"
R"GLSL(      float frame = (1.0 - smoothstep(0.03, 0.07, min(e.x, e.y))) * w * fine;
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
        float fsGlass;
        vec3 fsBase = vFsStyle.yzw / 255.0;
        diffuseColor.rgb = fsLin(clamp(fsFacade(vFsStyle.x, fsBase, vFsUvd, vFsWorld, distance(vFsWorld, cameraPosition), fsGlass), 0.0, 1.0));
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.77582, 0.81485, 0.87137), fsSnowCover(uFsSnow, fsUp, 0.4, 0.65, vFsSeasonXZ));
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"fs-bldg-plain|fs-season-building", "fs-bldg-plain|fs-season-building",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;

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

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}

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
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.77582, 0.81485, 0.87137), fsSnowCover(uFsSnow, fsUp, 0.4, 0.65, vFsSeasonXZ));
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"|fs-season-struct", "|fs-season-struct",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;

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
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
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
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.77582, 0.81485, 0.87137), fsSnowCover(uFsSnow, fsUp, 0.5, 0.75, vFsSeasonXZ));
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"|fs-season-deciduous", "|fs-season-deciduous",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;
varying float vFsLeafCover;
varying float vFsBare;

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

{
#ifdef USE_INSTANCING
  vec2 fsTree = instanceMatrix[3].xz;
#else
  vec2 fsTree = modelMatrix[3].xz;
#endif
  // the tree's own random numbers: when its leaves come and go, which colour they turn
  float fsR1 = fract(sin(dot(fsTree, vec2(12.9898, 78.233))) * 43758.5453);
  float fsR2 = fract(fsR1 * 91.7 + 0.31);
  // the crown is green, the trunk brown
  float fsCrown = step(color.r * 1.25, color.g);
  float fsTurn = clamp(uFsAutumn * 1.4 - fsR2 * 0.4, 0.0, 1.0);
  vec3 fsAut = fsR2 < 0.45 ? vec3(8.08169, 1.85649, 0.54998) : fsR2 < 0.7 ? vec3(7.99178, 0.65481, 0.35194) : fsR2 < 0.85 ? vec3(5.20621, 0.16389, 0.35194) : vec3(2.69286, 0.52535, 0.74062);
  vec3 fsT = mix(vec3(1.0), vec3(1.76331, 1.54428, 1.14698), uFsSpring);
  fsT = mix(fsT, fsAut, fsTurn);
  // blossom on some of the crown's faces: every fruit tree, now and then another
  float fsBlooms = step(0.85, fsR1);
  float fsHere = step(fract(sin(dot(position.xyz, vec3(17.1, 31.7, 11.3)) + fsR1 * 7.0) * 4375.85), 0.65);
  fsT = mix(fsT, vec3(12.05680, 3.67743, 21.03101), uFsBlossom * fsBlooms * fsHere);
  float fsHas = clamp(uFsLeaves * 1.3 - fsR1 * 0.3, 0.0, 1.0);
  vColor.rgb *= mix(vec3(1.0), fsT, fsCrown);
  // what is bare of the crown: the grey-brown of its twigs, whatever green it had (a tint of the green went black)
  vColor.rgb = mix(vColor.rgb, vec3(0.36625, 0.30947, 0.24620) * (0.85 + 0.3 * fsR2), (1.0 - fsHas) * fsCrown);
  vFsLeafCover = mix(1.0, 1.000 + 0.000 * fsHas, fsCrown);
  vFsBare = (1.0 - fsHas) * fsCrown;
}

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
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}
varying float vFsLeafCover;
varying float vFsBare;

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
	#include <alphamap_fragment>
	#include <alphatest_fragment>

#ifdef FS_THIN
if (vFsLeafCover < 0.999) {
  float fsD = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  if (fsD > vFsLeafCover) discard;
}
#endif

	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.77582, 0.81485, 0.87137), fsSnowCover(uFsSnow * mix(0.5, 1.0, uFsLeaves), fsUp, 0.2, 0.55, vFsSeasonXZ));
}

{
  vec3 fsUpB = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  normal = normalize(mix(normal, fsUpB, vFsBare * 0.850));
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"|fs-season-orchard", "|fs-season-orchard",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;
varying float vFsLeafCover;
varying float vFsBare;

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

{
#ifdef USE_INSTANCING
  vec2 fsTree = instanceMatrix[3].xz;
#else
  vec2 fsTree = modelMatrix[3].xz;
#endif
  // the tree's own random numbers: when its leaves come and go, which colour they turn
  float fsR1 = fract(sin(dot(fsTree, vec2(12.9898, 78.233))) * 43758.5453);
  float fsR2 = fract(fsR1 * 91.7 + 0.31);
  // the crown is green, the trunk brown
  float fsCrown = step(color.r * 1.25, color.g);
  float fsTurn = clamp(uFsAutumn * 1.4 - fsR2 * 0.4, 0.0, 1.0);
  vec3 fsAut = fsR2 < 0.45 ? vec3(8.08169, 1.85649, 0.54998) : fsR2 < 0.7 ? vec3(7.99178, 0.65481, 0.35194) : fsR2 < 0.85 ? vec3(5.20621, 0.16389, 0.35194) : vec3(2.69286, 0.52535, 0.74062);
  vec3 fsT = mix(vec3(1.0), vec3(1.76331, 1.54428, 1.14698), uFsSpring);
  fsT = mix(fsT, fsAut, fsTurn);
  // blossom on some of the crown's faces: every fruit tree, now and then another
  float fsBlooms = 1.0;
  float fsHere = step(fract(sin(dot(position.xyz, vec3(17.1, 31.7, 11.3)) + fsR1 * 7.0) * 4375.85), 0.65);
  fsT = mix(fsT, vec3(12.05680, 3.67743, 21.03101), uFsBlossom * fsBlooms * fsHere);
  float fsHas = clamp(uFsLeaves * 1.3 - fsR1 * 0.3, 0.0, 1.0);
  vColor.rgb *= mix(vec3(1.0), fsT, fsCrown);
  // what is bare of the crown: the grey-brown of its twigs, whatever green it had (a tint of the green went black)
  vColor.rgb = mix(vColor.rgb, vec3(0.36625, 0.30947, 0.24620) * (0.85 + 0.3 * fsR2), (1.0 - fsHas) * fsCrown);
  vFsLeafCover = mix(1.0, 1.000 + 0.000 * fsHas, fsCrown);
  vFsBare = (1.0 - fsHas) * fsCrown;
}

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
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}
varying float vFsLeafCover;
varying float vFsBare;

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
	#include <alphamap_fragment>
	#include <alphatest_fragment>

#ifdef FS_THIN
if (vFsLeafCover < 0.999) {
  float fsD = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  if (fsD > vFsLeafCover) discard;
}
#endif

	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.77582, 0.81485, 0.87137), fsSnowCover(uFsSnow * mix(0.5, 1.0, uFsLeaves), fsUp, 0.2, 0.55, vFsSeasonXZ));
}

{
  vec3 fsUpB = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  normal = normalize(mix(normal, fsUpB, vFsBare * 0.850));
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"|fs-season-evergreen", "|fs-season-evergreen",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;
varying float vFsLeafCover;
varying float vFsBare;

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
vFsLeafCover = 1.0;
vFsBare = 0.0;
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
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

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

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}
varying float vFsLeafCover;
varying float vFsBare;

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
	#include <alphamap_fragment>
	#include <alphatest_fragment>

#ifdef FS_THIN
if (vFsLeafCover < 0.999) {
  float fsD = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  if (fsD > vFsLeafCover) discard;
}
#endif

	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.77582, 0.81485, 0.87137), fsSnowCover(uFsSnow * 1.0, fsUp, 0.2, 0.55, vFsSeasonXZ));
}

{
  vec3 fsUpB = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  normal = normalize(mix(normal, fsUpB, vFsBare * 0.850));
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"|fs-season-deciduous-thin", "|fs-season-deciduous-thin",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;
varying float vFsLeafCover;
varying float vFsBare;

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

{
#ifdef USE_INSTANCING
  vec2 fsTree = instanceMatrix[3].xz;
#else
  vec2 fsTree = modelMatrix[3].xz;
#endif
  // the tree's own random numbers: when its leaves come and go, which colour they turn
  float fsR1 = fract(sin(dot(fsTree, vec2(12.9898, 78.233))) * 43758.5453);
  float fsR2 = fract(fsR1 * 91.7 + 0.31);
  // the crown is green, the trunk brown
  float fsCrown = step(color.r * 1.25, color.g);
  float fsTurn = clamp(uFsAutumn * 1.4 - fsR2 * 0.4, 0.0, 1.0);
  vec3 fsAut = fsR2 < 0.45 ? vec3(8.08169, 1.85649, 0.54998) : fsR2 < 0.7 ? vec3(7.99178, 0.65481, 0.35194) : fsR2 < 0.85 ? vec3(5.20621, 0.16389, 0.35194) : vec3(2.69286, 0.52535, 0.74062);
  vec3 fsT = mix(vec3(1.0), vec3(1.76331, 1.54428, 1.14698), uFsSpring);
  fsT = mix(fsT, fsAut, fsTurn);
  // blossom on some of the crown's faces: every fruit tree, now and then another
  float fsBlooms = step(0.85, fsR1);
  float fsHere = step(fract(sin(dot(position.xyz, vec3(17.1, 31.7, 11.3)) + fsR1 * 7.0) * 4375.85), 0.65);
  fsT = mix(fsT, vec3(12.05680, 3.67743, 21.03101), uFsBlossom * fsBlooms * fsHere);
  float fsHas = clamp(uFsLeaves * 1.3 - fsR1 * 0.3, 0.0, 1.0);
  vColor.rgb *= mix(vec3(1.0), fsT, fsCrown);
  // what is bare of the crown: the grey-brown of its twigs, whatever green it had (a tint of the green went black)
  vColor.rgb = mix(vColor.rgb, vec3(0.36625, 0.30947, 0.24620) * (0.85 + 0.3 * fsR2), (1.0 - fsHas) * fsCrown);
  vFsLeafCover = mix(1.0, 1.000 + 0.000 * fsHas, fsCrown);
  vFsBare = (1.0 - fsHas) * fsCrown;
}

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
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

	#include <logdepthbuf_vertex>
	#include <clipping_planes_vertex>
	vViewPosition = - mvPosition.xyz;
	#include <worldpos_vertex>
	#include <envmap_vertex>
	#include <shadowmap_vertex>
	#include <fog_vertex>
})GLSL",
R"GLSL(#define FS_THIN
#define LAMBERT
uniform vec3 diffuse;
uniform vec3 emissive;
uniform float opacity;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}
varying float vFsLeafCover;
varying float vFsBare;

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
	#include <alphamap_fragment>
	#include <alphatest_fragment>

#ifdef FS_THIN
if (vFsLeafCover < 0.999) {
  float fsD = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  if (fsD > vFsLeafCover) discard;
}
#endif

	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.77582, 0.81485, 0.87137), fsSnowCover(uFsSnow * mix(0.5, 1.0, uFsLeaves), fsUp, 0.2, 0.55, vFsSeasonXZ));
}

{
  vec3 fsUpB = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  normal = normalize(mix(normal, fsUpB, vFsBare * 0.850));
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"|fs-season-orchard-thin", "|fs-season-orchard-thin",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsBlossom;
uniform float uFsAutumn;
varying float vFsLeafCover;
varying float vFsBare;

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

{
#ifdef USE_INSTANCING
  vec2 fsTree = instanceMatrix[3].xz;
#else
  vec2 fsTree = modelMatrix[3].xz;
#endif
  // the tree's own random numbers: when its leaves come and go, which colour they turn
  float fsR1 = fract(sin(dot(fsTree, vec2(12.9898, 78.233))) * 43758.5453);
  float fsR2 = fract(fsR1 * 91.7 + 0.31);
  // the crown is green, the trunk brown
  float fsCrown = step(color.r * 1.25, color.g);
  float fsTurn = clamp(uFsAutumn * 1.4 - fsR2 * 0.4, 0.0, 1.0);
  vec3 fsAut = fsR2 < 0.45 ? vec3(8.08169, 1.85649, 0.54998) : fsR2 < 0.7 ? vec3(7.99178, 0.65481, 0.35194) : fsR2 < 0.85 ? vec3(5.20621, 0.16389, 0.35194) : vec3(2.69286, 0.52535, 0.74062);
  vec3 fsT = mix(vec3(1.0), vec3(1.76331, 1.54428, 1.14698), uFsSpring);
  fsT = mix(fsT, fsAut, fsTurn);
  // blossom on some of the crown's faces: every fruit tree, now and then another
  float fsBlooms = 1.0;
  float fsHere = step(fract(sin(dot(position.xyz, vec3(17.1, 31.7, 11.3)) + fsR1 * 7.0) * 4375.85), 0.65);
  fsT = mix(fsT, vec3(12.05680, 3.67743, 21.03101), uFsBlossom * fsBlooms * fsHere);
  float fsHas = clamp(uFsLeaves * 1.3 - fsR1 * 0.3, 0.0, 1.0);
  vColor.rgb *= mix(vec3(1.0), fsT, fsCrown);
  // what is bare of the crown: the grey-brown of its twigs, whatever green it had (a tint of the green went black)
  vColor.rgb = mix(vColor.rgb, vec3(0.36625, 0.30947, 0.24620) * (0.85 + 0.3 * fsR2), (1.0 - fsHas) * fsCrown);
  vFsLeafCover = mix(1.0, 1.000 + 0.000 * fsHas, fsCrown);
  vFsBare = (1.0 - fsHas) * fsCrown;
}

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
	#include <morphtarget_vertex>
	#include <skinning_vertex>
	#include <displacementmap_vertex>
	#include <project_vertex>

{
  vec4 fsW = vec4(transformed, 1.0);
#ifdef USE_INSTANCING
  fsW = instanceMatrix * fsW;
#endif
  vFsSeasonXZ = (modelMatrix * fsW).xz;
}

	#include <logdepthbuf_vertex>
	#include <clipping_planes_vertex>
	vViewPosition = - mvPosition.xyz;
	#include <worldpos_vertex>
	#include <envmap_vertex>
	#include <shadowmap_vertex>
	#include <fog_vertex>
})GLSL",
R"GLSL(#define FS_THIN
#define LAMBERT
uniform vec3 diffuse;
uniform vec3 emissive;
uniform float opacity;
#include <common>

varying vec2 vFsSeasonXZ;
uniform float uFsLeaves;
uniform float uFsSpring;
uniform float uFsAutumn;
uniform float uFsDormant;
uniform float uFsSnow;
uniform float uFsRoadSnow;
float fsSeasonHash(vec2 p) {
  p = fract(p * vec2(127.13, 311.71));
  p += dot(p, p + 19.19);
  return fract(p.x * p.y);
}
float fsSeasonNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsSeasonHash(i), fsSeasonHash(i + vec2(1.0, 0.0)), f.x), mix(fsSeasonHash(i + vec2(0.0, 1.0)), fsSeasonHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// how much of a surface the snow covers: on what faces the sky, in patches where it is thin
float fsSnowCover(float amount, float up, float lo, float hi, vec2 xz) {
  if (amount <= 0.001) return 0.0;
  float n = fsSeasonNoise(xz * 0.31) * 0.65 + fsSeasonNoise(xz * 2.3) * 0.35;
  float e = 1.15 - 1.3 * amount;
  return smoothstep(lo, hi, up) * smoothstep(e - 0.15, e + 0.15, n);
}
// what the season does to a ground colour: green plants, fields, the forest floor
vec3 fsSeasonGround(vec3 c) {
  float mx = max(max(c.r, c.g), max(c.b, 1e-4));
  float veg = smoothstep(0.08, 0.3, (c.g - max(c.r, c.b)) / mx);
  float field = smoothstep(0.3, 0.45, (min(c.r, c.g) - c.b) / mx) * (1.0 - smoothstep(0.08, 0.2, abs(c.r - c.g) / mx));
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  float dark = 1.0 - smoothstep(0.06, 0.14, l);
  float fl = l / 0.35402;
  float litter = max(uFsAutumn * (1.0 - uFsLeaves), uFsDormant);
  vec3 o = c;
  o = mix(o, o * vec3(1.10000, 1.15000, 0.90000), uFsSpring * veg);
  o = mix(o, vec3(0.20156, 0.32314, 0.06848) * fl, uFsSpring * field * 0.7);
  o = mix(o, o * vec3(1.15000, 0.92000, 0.80000), uFsAutumn * veg * (1.0 - dark) * 0.6);
  o = mix(o, o * vec3(2.20000, 0.85000, 0.70000), veg * dark * litter * 0.8);
  o = mix(o, vec3(0.23840, 0.16203, 0.07819) * fl, uFsAutumn * field * 0.7);
  float dl = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(o, dl * vec3(1.10000, 1.00000, 0.78000), uFsDormant * veg * (1.0 - dark) * 0.55);
  o = mix(o, vec3(0.18116, 0.13014, 0.07421) * fl, uFsDormant * field * 0.75);
  return o;
}
varying float vFsLeafCover;
varying float vFsBare;

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
	#include <alphamap_fragment>
	#include <alphatest_fragment>

#ifdef FS_THIN
if (vFsLeafCover < 0.999) {
  float fsD = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  if (fsD > vFsLeafCover) discard;
}
#endif

	#include <alphahash_fragment>
	#include <specularmap_fragment>
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	{
  vec3 fsUpV = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  float fsUp = dot(normal, fsUpV);
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.77582, 0.81485, 0.87137), fsSnowCover(uFsSnow * mix(0.5, 1.0, uFsLeaves), fsUp, 0.2, 0.55, vFsSeasonXZ));
}

{
  vec3 fsUpB = normalize((viewMatrix * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  normal = normalize(mix(normal, fsUpB, vFsBare * 0.850));
}
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
"uFsLeaves uFsSpring uFsBlossom uFsAutumn uFsDormant uFsSnow uFsRoadSnow"},
{"lambert|dirt", "|dirt",
R"GLSL(#define LAMBERT
varying vec3 vViewPosition;
#include <common>
uniform mat4 uDirtFrame;
varying vec3 vDirtPos;
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
vDirtPos = (uDirtFrame * modelMatrix * vec4(transformed, 1.0)).xyz;
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
uniform float uDirt;
uniform float uDirtHeight;
uniform vec3 uDirtColour;
uniform sampler2D uDirtNoise;
varying vec3 vDirtPos;
float dirtMask = 0.0;
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
{
  vec3 p = vDirtPos;
  float n = texture2D(uDirtNoise, vec2(p.x + p.y * 0.7, p.z - p.y * 0.3) * 0.45).r * 0.6 + texture2D(uDirtNoise, vec2(p.z + p.x * 0.5, p.y) * 1.7).r * 0.4;
  float low = clamp(1.15 - p.y / uDirtHeight, 0.0, 1.0);
  dirtMask = smoothstep(0.32, 0.7, uDirt * (0.45 + 0.85 * low) + (n - 0.5) * 0.6 + 0.12) * min(1.0, uDirt * 4.0);
  diffuseColor.rgb = mix(diffuseColor.rgb, uDirtColour * (0.7 + 0.6 * n), dirtMask * 0.85);
}
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
specularStrength *= 1.0 - 0.85 * dirtMask;
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
"uDirt uDirtFrame uDirtHeight uDirtColour uDirtNoise"},
{"phong|dirt", "|dirt",
R"GLSL(#define PHONG
varying vec3 vViewPosition;
#include <common>
uniform mat4 uDirtFrame;
varying vec3 vDirtPos;
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
	#include <morphcolor_vertex>
	#include <batching_vertex>
	#include <beginnormal_vertex>
	#include <morphinstance_vertex>
	#include <morphnormal_vertex>
	#include <skinbase_vertex>
	#include <skinnormal_vertex>
	#include <defaultnormal_vertex>
	#include <normal_vertex>
	#include <begin_vertex>
vDirtPos = (uDirtFrame * modelMatrix * vec4(transformed, 1.0)).xyz;
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
R"GLSL(#define PHONG
uniform vec3 diffuse;
uniform vec3 emissive;
uniform vec3 specular;
uniform float shininess;
uniform float opacity;
#include <common>
uniform float uDirt;
uniform float uDirtHeight;
uniform vec3 uDirtColour;
uniform sampler2D uDirtNoise;
varying vec3 vDirtPos;
float dirtMask = 0.0;
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
#include <lights_phong_pars_fragment>
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
{
  vec3 p = vDirtPos;
  float n = texture2D(uDirtNoise, vec2(p.x + p.y * 0.7, p.z - p.y * 0.3) * 0.45).r * 0.6 + texture2D(uDirtNoise, vec2(p.z + p.x * 0.5, p.y) * 1.7).r * 0.4;
  float low = clamp(1.15 - p.y / uDirtHeight, 0.0, 1.0);
  dirtMask = smoothstep(0.32, 0.7, uDirt * (0.45 + 0.85 * low) + (n - 0.5) * 0.6 + 0.12) * min(1.0, uDirt * 4.0);
  diffuseColor.rgb = mix(diffuseColor.rgb, uDirtColour * (0.7 + 0.6 * n), dirtMask * 0.85);
}
	#include <alphamap_fragment>
	#include <alphatest_fragment>
	#include <alphahash_fragment>
	#include <specularmap_fragment>
specularStrength *= 1.0 - 0.85 * dirtMask;
	#include <normal_fragment_begin>
	#include <normal_fragment_maps>
	#include <emissivemap_fragment>
	#include <lights_phong_fragment>
	#include <lights_fragment_begin>
	#include <lights_fragment_maps>
	#include <lights_fragment_end>
	#include <aomap_fragment>
	vec3 outgoingLight = reflectedLight.directDiffuse + reflectedLight.indirectDiffuse + reflectedLight.directSpecular + reflectedLight.indirectSpecular + totalEmissiveRadiance;
	#include <envmap_fragment>
	#include <opaque_fragment>
	#include <tonemapping_fragment>
	#include <colorspace_fragment>
	#include <fog_fragment>
	#include <premultiplied_alpha_fragment>
	#include <dithering_fragment>
})GLSL",
"uDirt uDirtFrame uDirtHeight uDirtColour uDirtNoise"},
};
const int GAME_SHADER_COUNT = 24;

const GameShader* gameShader(const char* name) {
  for (int i = 0; i < GAME_SHADER_COUNT; i++)
    if (!std::strcmp(GAME_SHADERS[i].name, name)) return &GAME_SHADERS[i];
  return nullptr;
}

}  // namespace fs
