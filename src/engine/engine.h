// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The engine: the part of three.js the game uses, by the same names, so that the
 * original's drawing code translates line by line –
 *
 *     const m = new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ color: 0x808890 }));
 *     m.position.set(x, y, z);  m.castShadow = true;  group.add(m);
 *
 *     auto m = make<Mesh>(geo, lambert({.color = 0x808890}));
 *     m->position.set(x, y, z);  m->castShadow = true;  group->add(m);
 *
 * – with a renderer underneath that is written for this game (engine/renderer.h):
 * buffers uploaded once, one vertex array per geometry, culling by bounding spheres,
 * draws sorted by program, one shadow map, lights and fog in a few uniforms.
 *
 * Objects are held by std::shared_ptr (as JavaScript holds them by reference) and
 * made with make<T>(…).  A geometry, a material or a texture frees what it holds on
 * the graphics card when the last reference to it goes; `dispose()` exists and does
 * nothing more than that would.
 *
 * Colours are kept as three.js keeps them: linear-sRGB numbers.  `Color(0xRRGGBB)`
 * and setHex() read an sRGB colour and convert it; set(r, g, b) takes linear numbers.
 */
#pragma once

#include <climits>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "engine/math.h"
#include "platform/canvas.h"

namespace fs {

/** `new THREE.X(…)` */
template <class T, class... A>
std::shared_ptr<T> make(A&&... a) {
  return std::make_shared<T>(std::forward<A>(a)...);
}

// ---------------------------------------------------------------------------
// BufferAttribute: one array of a geometry (positions, normals, an index …)
// ---------------------------------------------------------------------------

enum class AttrType { F32, U8, I8, U16, I16, U32, I32 };

class BufferAttribute {
 public:
  AttrType type = AttrType::F32;
  /** numbers per vertex */
  int itemSize = 3;
  /** integers are read as 0 … 1 (-1 … 1) by the shader */
  bool normalized = false;
  /** THREE.DynamicDrawUsage: rewritten often */
  bool dynamic = false;
  /** InstancedBufferAttribute: one item per instance (meshPerAttribute), 0: per vertex */
  int divisor = 0;
  /** goes up whenever the array was changed (`attr.needsUpdate = true` → needsUpdate()) */
  uint32_t version = 1;
  /**
   * addUpdateRange / clearUpdateRanges: the parts of the array that changed (in numbers, not items).
   * three.js uploads only these; this renderer uploads the whole buffer, so they are kept but not needed.
   */
  struct UpdateRange {
    int start = 0, count = 0;
  };
  std::vector<UpdateRange> updateRanges;
  void addUpdateRange(int start, int count) { updateRanges.push_back({start, count}); }
  void clearUpdateRanges() { updateRanges.clear(); }

  // (the array: the one of these that `type` names)
  std::vector<float> f32;
  std::vector<uint8_t> u8;
  std::vector<int8_t> i8;
  std::vector<uint16_t> u16;
  std::vector<int16_t> i16;
  std::vector<uint32_t> u32;
  std::vector<int32_t> i32;

  BufferAttribute() = default;
  BufferAttribute(std::vector<float> a, int size, bool norm = false) : type(AttrType::F32), itemSize(size), normalized(norm), f32(std::move(a)) {}
  BufferAttribute(std::vector<uint8_t> a, int size, bool norm = false) : type(AttrType::U8), itemSize(size), normalized(norm), u8(std::move(a)) {}
  BufferAttribute(std::vector<int8_t> a, int size, bool norm = false) : type(AttrType::I8), itemSize(size), normalized(norm), i8(std::move(a)) {}
  BufferAttribute(std::vector<uint16_t> a, int size, bool norm = false) : type(AttrType::U16), itemSize(size), normalized(norm), u16(std::move(a)) {}
  BufferAttribute(std::vector<int16_t> a, int size, bool norm = false) : type(AttrType::I16), itemSize(size), normalized(norm), i16(std::move(a)) {}
  BufferAttribute(std::vector<uint32_t> a, int size, bool norm = false) : type(AttrType::U32), itemSize(size), normalized(norm), u32(std::move(a)) {}
  BufferAttribute(std::vector<int32_t> a, int size, bool norm = false) : type(AttrType::I32), itemSize(size), normalized(norm), i32(std::move(a)) {}
  ~BufferAttribute();

  /** how many numbers the array holds (array.length) */
  size_t length() const;
  /** how many vertices (length / itemSize) */
  size_t count() const { return itemSize > 0 ? length() / static_cast<size_t>(itemSize) : 0; }
  /** the array's bytes, as they go to the graphics card */
  const void* data() const;
  size_t byteLength() const;

  /** array[i] as a number (not denormalised) / array[i] = v */
  double at(size_t i) const;
  void put(size_t i, double v);
  /** getX(i) … as three.js: of a normalised attribute, the number the shader sees */
  double getX(size_t i) const { return get(i, 0); }
  double getY(size_t i) const { return get(i, 1); }
  double getZ(size_t i) const { return get(i, 2); }
  double getW(size_t i) const { return get(i, 3); }
  BufferAttribute& setX(size_t i, double v) { return set(i, 0, v); }
  BufferAttribute& setY(size_t i, double v) { return set(i, 1, v); }
  BufferAttribute& setZ(size_t i, double v) { return set(i, 2, v); }
  BufferAttribute& setW(size_t i, double v) { return set(i, 3, v); }
  BufferAttribute& setXY(size_t i, double x, double y) {
    set(i, 0, x);
    return set(i, 1, y);
  }
  BufferAttribute& setXYZ(size_t i, double x, double y, double z) {
    set(i, 0, x);
    set(i, 1, y);
    return set(i, 2, z);
  }
  BufferAttribute& setXYZW(size_t i, double x, double y, double z, double w) {
    set(i, 0, x);
    set(i, 1, y);
    set(i, 2, z);
    return set(i, 3, w);
  }
  /** `attr.needsUpdate = true` */
  void needsUpdate() { version++; }
  /** `attr.setUsage(THREE.DynamicDrawUsage)` */
  BufferAttribute& setDynamic() {
    dynamic = true;
    return *this;
  }
  std::shared_ptr<BufferAttribute> clone() const;
  /** applyMatrix4 on a position attribute (itemSize 3) */
  BufferAttribute& applyMatrix4(const Matrix4& m);
  /** applyNormalMatrix on a normal attribute, by the matrix's upper 3×3 inverse transpose */
  BufferAttribute& transformNormals(const Matrix4& m);
  /** applyMatrix3 (itemSize 2 or 3), applyNormalMatrix (the normal matrix itself), transformDirection */
  BufferAttribute& applyMatrix3(const Matrix3& m);
  BufferAttribute& applyNormalMatrix(const Matrix3& m);
  BufferAttribute& transformDirection(const Matrix4& m);

  /** the renderer's handle of the uploaded array (0: not uploaded) */
  mutable uint32_t gpu = 0;
  mutable uint32_t gpuVersion = 0;

 private:
  double get(size_t i, int c) const;
  BufferAttribute& set(size_t i, int c, double v);
};

using AttrPtr = std::shared_ptr<BufferAttribute>;

/** `new THREE.BufferAttribute(array, itemSize, normalized)` / Float32BufferAttribute */
template <class T>
AttrPtr attr(std::vector<T> array, int itemSize, bool normalized = false) {
  return std::make_shared<BufferAttribute>(std::move(array), itemSize, normalized);
}

// ---------------------------------------------------------------------------
// BufferGeometry
// ---------------------------------------------------------------------------

class BufferGeometry : public std::enable_shared_from_this<BufferGeometry> {
 public:
  struct Group {
    int start = 0;
    int count = 0;
    int materialIndex = 0;
  };
  struct DrawRange {
    int start = 0;
    /** INT_MAX: to the end (three.js: Infinity) */
    int count = INT_MAX;
  };

  const uint32_t id;
  std::string name;
  /** 'position', 'normal', 'uv', 'color' and the game's own ('amat', 'astyle' …), in the order they were set */
  std::vector<std::pair<std::string, AttrPtr>> attributes;
  AttrPtr index;
  std::vector<Group> groups;
  DrawRange drawRange;
  std::optional<Sphere> boundingSphere;
  std::optional<Box3> boundingBox;
  /** InstancedBufferGeometry.instanceCount (-1: not an instanced geometry) */
  int instanceCount = -1;

  BufferGeometry();
  virtual ~BufferGeometry();
  BufferGeometry(const BufferGeometry&) = delete;
  BufferGeometry& operator=(const BufferGeometry&) = delete;

  BufferGeometry& setAttribute(const std::string& name, AttrPtr a);
  /** getAttribute(name) (null: there is none) */
  AttrPtr getAttribute(const std::string& name) const;
  bool hasAttribute(const std::string& name) const { return getAttribute(name) != nullptr; }
  BufferGeometry& deleteAttribute(const std::string& name);
  BufferGeometry& setIndex(AttrPtr a);
  /** setIndex([…]) with plain numbers: 16 bits where they fit, as three.js chooses */
  BufferGeometry& setIndex(const std::vector<uint32_t>& indices);
  BufferGeometry& clearIndex();
  BufferGeometry& addGroup(int start, int count, int materialIndex = 0);
  BufferGeometry& clearGroups();
  BufferGeometry& setDrawRange(int start, int count);

  /** the common attributes, by name (null: not there) */
  AttrPtr position() const { return getAttribute("position"); }
  AttrPtr normal() const { return getAttribute("normal"); }
  AttrPtr uv() const { return getAttribute("uv"); }
  AttrPtr color() const { return getAttribute("color"); }

  BufferGeometry& applyMatrix4(const Matrix4& m);
  BufferGeometry& applyQuaternion(const Quaternion& q);
  BufferGeometry& rotateX(double angle);
  BufferGeometry& rotateY(double angle);
  BufferGeometry& rotateZ(double angle);
  BufferGeometry& translate(double x, double y, double z);
  BufferGeometry& scale(double x, double y, double z);
  /** move it so that the middle of its bounding box is the origin */
  BufferGeometry& center();
  BufferGeometry& computeVertexNormals();
  BufferGeometry& normalizeNormals();
  BufferGeometry& computeBoundingBox();
  BufferGeometry& computeBoundingSphere();
  /** setFromPoints(points): a position attribute from the points */
  BufferGeometry& setFromPoints(const std::vector<Vector3>& points);
  std::shared_ptr<BufferGeometry> toNonIndexed() const;
  std::shared_ptr<BufferGeometry> clone() const;
  BufferGeometry& copy(const BufferGeometry& source);
  void dispose();

  /** the renderer's vertex array for it (0: none yet), and what it was built from */
  mutable uint32_t gpuVao = 0;
  mutable uint64_t gpuLayout = 0;
};

using GeometryPtr = std::shared_ptr<BufferGeometry>;

// ---------------------------------------------------------------------------
// Texture
// ---------------------------------------------------------------------------

enum class Wrapping { ClampToEdge, Repeat, MirroredRepeat };
enum class Filter { Nearest, Linear, NearestMipmapNearest, LinearMipmapNearest, NearestMipmapLinear, LinearMipmapLinear };
enum class PixelFormat { RGBA, RGB, Red, Depth };
enum class PixelType { UnsignedByte, HalfFloat, Float };

class Texture {
 public:
  const uint32_t id;
  std::string name;
  int width = 0, height = 0;
  PixelFormat format = PixelFormat::RGBA;
  PixelType type = PixelType::UnsignedByte;
  Wrapping wrapS = Wrapping::ClampToEdge, wrapT = Wrapping::ClampToEdge;
  Filter magFilter = Filter::Linear, minFilter = Filter::LinearMipmapLinear;
  bool generateMipmaps = true;
  int anisotropy = 1;
  /** `colorSpace = THREE.SRGBColorSpace`: the pixels are sRGB (colour pictures); false: plain numbers */
  bool srgb = false;
  /** the first row of the pixels is the top of the picture (true for canvases and pictures, false for data) */
  bool flipY = true;
  Vector2 repeat{1, 1};
  Vector2 offset{0, 0};
  /** goes up when the pixels changed (`texture.needsUpdate = true` → needsUpdate()) */
  uint32_t version = 0;

  /** where the pixels come from: bytes (DataTexture) … */
  std::shared_ptr<std::vector<uint8_t>> data;
  /** … a canvas (CanvasTexture) … */
  Canvas canvas;
  /** … or a picture the host has decoded (see plat images) */
  int image = 0;

  Texture();
  virtual ~Texture();
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  void needsUpdate() { version++; }
  void dispose();

  mutable uint32_t gpu = 0;
  mutable uint32_t gpuVersion = 0xffffffffu;
  /** set by a render target: the renderer owns the pixels */
  bool isRenderTarget = false;
};

using TexturePtr = std::shared_ptr<Texture>;

/** `new THREE.CanvasTexture(canvas)` (sRGB is set by the caller, as in the original) */
TexturePtr canvasTexture(const Canvas& canvas);
/** `new THREE.DataTexture(data, width, height, format)` */
TexturePtr dataTexture(std::vector<uint8_t> data, int width, int height, PixelFormat format = PixelFormat::RGBA);

/** `new THREE.WebGLRenderTarget(width, height, { depthBuffer })` */
class RenderTarget {
 public:
  int width, height;
  bool depthBuffer;
  /** antialiasing of the target (0: none) */
  int samples = 0;
  TexturePtr texture;
  RenderTarget(int w, int h, bool depth = true);
  ~RenderTarget();
  void setSize(int w, int h);
  void dispose();
  mutable uint32_t gpuFbo = 0, gpuDepth = 0, gpuMsFbo = 0, gpuMsColor = 0;
  mutable int gpuW = 0, gpuH = 0;
};

using RenderTargetPtr = std::shared_ptr<RenderTarget>;

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

enum class Side { Front, Back, Double };
enum class Blending { None, Normal, Additive, Multiply };
enum class MaterialType { Basic, Lambert, Phong, Shader, Sprite, Points, Line, Depth };

/** The value of a shader uniform (`{ value: … }`): shared by reference between materials, as in the original. */
struct Uniform {
  using Value = std::variant<double, int, bool, Vector2, Vector3, Vector4, Color, Matrix3, Matrix4, TexturePtr, std::vector<float>>;
  Value value;
  Uniform() : value(0.0) {}
  template <class T>
  explicit Uniform(T v) : value(std::move(v)) {}
};
using UniformPtr = std::shared_ptr<Uniform>;
template <class T>
UniformPtr uniform(T v) {
  return std::make_shared<Uniform>(std::move(v));
}

/**
 * What `material.onBeforeCompile = (shader) => { … }` does in the original: edits of
 * the built-in shader, named by the chunk they hang on (`#include <begin_vertex>` →
 * "begin_vertex"), and the uniforms they add.  `key` is the customProgramCacheKey:
 * materials with the same kind, options and key share one program.
 */
struct ShaderPatch {
  enum Stage { Vertex, Fragment };
  enum Mode { Replace, Before, After };
  struct Edit {
    Stage stage;
    Mode mode;
    /** the chunk's name – or, for replaceText, the text to look for */
    std::string chunk;
    std::string code;
    bool text = false;
  };
  std::string key;
  /**
   * The whole shader as the original's onBeforeCompile leaves it (with its `#include`
   * lines), in place of three.js's own for the material's kind – what
   * tools/gen-game-glsl.mjs captures from the original.  Empty: three.js's, with the edits.
   */
  std::string vertexSource, fragmentSource;
  std::vector<Edit> edits;
  std::map<std::string, UniformPtr> uniforms;

  /** shader.vertexShader.replace('#include <chunk>', code) */
  ShaderPatch& replace(Stage s, const std::string& chunk, const std::string& code) {
    edits.push_back({s, Replace, chunk, code, false});
    return *this;
  }
  /** … .replace('#include <chunk>', code + '\n#include <chunk>') */
  ShaderPatch& before(Stage s, const std::string& chunk, const std::string& code) {
    edits.push_back({s, Before, chunk, code, false});
    return *this;
  }
  /** … .replace('#include <chunk>', '#include <chunk>\n' + code) */
  ShaderPatch& after(Stage s, const std::string& chunk, const std::string& code) {
    edits.push_back({s, After, chunk, code, false});
    return *this;
  }
  /** … .replace(text, code) for a text that is not an include (e.g. 'void main() {') */
  ShaderPatch& replaceText(Stage s, const std::string& text, const std::string& code) {
    edits.push_back({s, Replace, text, code, true});
    return *this;
  }
  ShaderPatch& uniform(const std::string& name, UniformPtr u) {
    uniforms[name] = std::move(u);
    return *this;
  }
};
using ShaderPatchPtr = std::shared_ptr<ShaderPatch>;

/** The parameters of `new THREE.Mesh…Material({ … })`, for lambert(), basic(), phong(). */
struct MaterialParams {
  // (every member has an initialiser, so that `{.color = 0x808890}` leaves nothing for -Wmissing-field-initializers to name)
  std::optional<Color> color{};
  TexturePtr map{};
  /** `alphaMap`: the green channel is the opacity */
  TexturePtr alphaMap{};
  /** `emissiveMap`: the emissive colour times this picture */
  TexturePtr emissiveMap{};
  bool vertexColors = false;
  /** not given: the class's own (a SpriteMaterial is transparent, the others are not) */
  std::optional<bool> transparent{};
  double opacity = 1;
  Side side = Side::Front;
  bool depthWrite = true;
  bool depthTest = true;
  /** not given: the class's own (a ShaderMaterial has no fog, the others have) */
  std::optional<bool> fog{};
  bool toneMapped = true;
  bool flatShading = false;
  double alphaTest = 0;
  Blending blending = Blending::Normal;
  bool polygonOffset = false;
  double polygonOffsetFactor = 0;
  double polygonOffsetUnits = 0;
  std::optional<Color> emissive{};
  double emissiveIntensity = 1;
  std::optional<Color> specular{};
  double shininess = 30;
  bool colorWrite = true;
  std::string name{};
};

class Renderer;
class Scene;
class Camera;

class Material {
 public:
  const uint32_t id;
  MaterialType type = MaterialType::Lambert;
  std::string name;
  Color color{1, 1, 1};
  TexturePtr map;
  /** `alphaMap`: the green channel is the opacity */
  TexturePtr alphaMap;
  /** `emissiveMap`: the emissive colour times this picture */
  TexturePtr emissiveMap;
  /** `material.onBeforeRender`: called before an object with this material is drawn (not for the shadow map) */
  std::function<void(Renderer&, Scene&, Camera&)> onBeforeRender;
  bool vertexColors = false;
  bool transparent = false;
  double opacity = 1;
  Side side = Side::Front;
  /** the side drawn into the shadow map (null: three.js's rule – the back of a front-sided material) */
  std::optional<Side> shadowSide;
  bool depthWrite = true;
  bool depthTest = true;
  bool fog = true;
  bool toneMapped = true;
  bool flatShading = false;
  bool visible = true;
  bool colorWrite = true;
  double alphaTest = 0;
  Blending blending = Blending::Normal;
  bool polygonOffset = false;
  double polygonOffsetFactor = 0, polygonOffsetUnits = 0;
  Color emissive{0, 0, 0};
  double emissiveIntensity = 1;
  // MeshPhongMaterial
  Color specular;   // 0x111111
  double shininess = 30;
  // PointsMaterial / SpriteMaterial
  double size = 1;
  bool sizeAttenuation = true;
  double rotation = 0;
  // LineBasicMaterial: lines are one pixel wide, as in WebGL
  // ShaderMaterial: the whole shader is the game's own
  std::string vertexShader, fragmentShader;
  std::map<std::string, UniformPtr> uniforms;
  /** ShaderMaterial.lights / .defines */
  std::vector<std::pair<std::string, std::string>> defines;
  /** onBeforeCompile + customProgramCacheKey */
  ShaderPatchPtr patch;
  /** goes up when something changed that needs another program (`material.needsUpdate = true`) */
  uint32_t version = 0;

  explicit Material(MaterialType t);
  Material(MaterialType t, const MaterialParams& p);
  virtual ~Material();
  Material(const Material&) = delete;
  Material& operator=(const Material&) = delete;
  void needsUpdate() { version++; }
  std::shared_ptr<Material> clone() const;
  void dispose() {}

  /** the renderer's program for it, per variant (see renderer.cpp) */
  mutable void* gpuPrograms = nullptr;
  mutable uint32_t gpuVersion = 0xffffffffu;
};

using MaterialPtr = std::shared_ptr<Material>;

/** `new THREE.MeshLambertMaterial({ … })` */
MaterialPtr lambert(const MaterialParams& p = MaterialParams());
/** `new THREE.MeshBasicMaterial({ … })` */
MaterialPtr basic(const MaterialParams& p = MaterialParams());
/** `new THREE.MeshPhongMaterial({ … })` (MeshStandardMaterial is drawn as this) */
MaterialPtr phong(const MaterialParams& p = MaterialParams());
/** `new THREE.LineBasicMaterial({ color })` */
MaterialPtr lineBasic(const MaterialParams& p = MaterialParams());
/** `new THREE.SpriteMaterial({ map, … })` */
MaterialPtr spriteMaterial(const MaterialParams& p = MaterialParams());
/** `new THREE.PointsMaterial({ size, … })` */
MaterialPtr pointsMaterial(const MaterialParams& p = MaterialParams());
/** `new THREE.ShaderMaterial({ vertexShader, fragmentShader, uniforms, … })` */
MaterialPtr shaderMaterial(const std::string& vertexShader, const std::string& fragmentShader, std::map<std::string, UniformPtr> uniforms = {},
                           const MaterialParams& p = MaterialParams());

// ---------------------------------------------------------------------------
// Object3D and what is made of it
// ---------------------------------------------------------------------------

enum class ObjectType { Object3D, Group, Scene, Mesh, InstancedMesh, Sprite, Points, Line, LineSegments, LOD, Camera, Light };

class Object3D;
using ObjectPtr = std::shared_ptr<Object3D>;
class Camera;
class Renderer;
class Scene;

/**
 * `object.rotation`: Euler angles that are one thing with the object's quaternion –
 * `o.rotation.y = a` turns the quaternion, and after `o.quaternion.copy(q)` the
 * angles read what q is, as in three.js.
 */
class Rotation {
 public:
  class Component {
   public:
    operator double() const;
    Component& operator=(double v);
    Component& operator=(const Component& o) { return *this = static_cast<double>(o); }
    Component& operator+=(double v) { return *this = static_cast<double>(*this) + v; }
    Component& operator-=(double v) { return *this = static_cast<double>(*this) - v; }
    Component& operator*=(double v) { return *this = static_cast<double>(*this) * v; }

   private:
    friend class Rotation;
    Component(Rotation* r, int i) : r_(r), i_(i) {}
    Rotation* r_;
    int i_;
  };
  Component x, y, z;

  explicit Rotation(Quaternion* q) : x(this, 0), y(this, 1), z(this, 2), q_(q) {}
  Rotation(const Rotation&) = delete;
  Rotation& operator=(const Rotation& o) { return copy(o); }
  Rotation& set(double x_, double y_, double z_);
  Rotation& set(double x_, double y_, double z_, Euler::Order order);
  Rotation& copy(const Rotation& o);
  Rotation& copy(const Euler& e);
  /** `rotation.order = 'YXZ'` */
  Euler::Order order() const { return e_.order; }
  void order(Euler::Order o);
  Rotation& setFromQuaternion(const Quaternion& q);
  Rotation& setFromRotationMatrix(const Matrix4& m);
  /** the angles as a plain Euler */
  Euler euler() const;

 private:
  void pull() const;   // the quaternion was changed: the angles follow
  void push();         // the angles were changed: the quaternion follows
  Quaternion* q_;
  mutable Euler e_;
  mutable Quaternion seen_;
};

class Object3D : public std::enable_shared_from_this<Object3D> {
 public:
  const uint32_t id;
  const ObjectType type;
  std::string name;
  Vector3 position;
  Quaternion quaternion;
  Rotation rotation{&quaternion};
  Vector3 scale{1, 1, 1};
  /** three.js Object3D.up */
  Vector3 up{0, 1, 0};
  Matrix4 matrix;
  Matrix4 matrixWorld;
  bool matrixAutoUpdate = true;
  bool matrixWorldAutoUpdate = true;
  bool matrixWorldNeedsUpdate = false;
  bool visible = true;
  bool castShadow = false;
  bool receiveShadow = false;
  bool frustumCulled = true;
  int renderOrder = 0;
  /** `layers`: a bit mask; an object is drawn by a camera that shares a bit with it (layers.set(n) → 1 << n) */
  uint32_t layers = 1;
  Object3D* parent = nullptr;
  std::vector<ObjectPtr> children;
  /** `userData`: what the game hangs on an object */
  std::map<std::string, double> userData;
  /** `onBeforeRender`: called before the object is drawn (not for the shadow map) */
  std::function<void(Renderer&, Scene&, Camera&)> onBeforeRender;

  Object3D();
  virtual ~Object3D();
  Object3D(const Object3D&) = delete;
  Object3D& operator=(const Object3D&) = delete;

  /** add(child): it leaves the parent it had */
  Object3D& add(const ObjectPtr& child);
  template <class A, class B, class... Rest>
  Object3D& add(const A& a, const B& b, const Rest&... rest) {
    add(ObjectPtr(a));
    add(ObjectPtr(b));
    (add(ObjectPtr(rest)), ...);
    return *this;
  }
  Object3D& remove(const ObjectPtr& child);
  Object3D& remove(Object3D* child);
  Object3D& removeFromParent();
  /** remove every child */
  Object3D& clear();
  /** attach(child): add it, keeping where it is in the world */
  Object3D& attach(const ObjectPtr& child);

  void updateMatrix();
  virtual void updateMatrixWorld(bool force = false);
  virtual void updateWorldMatrix(bool updateParents, bool updateChildren, bool force = false);
  void lookAt(double x, double y, double z);
  void lookAt(const Vector3& v) { lookAt(v.x, v.y, v.z); }
  Object3D& applyMatrix4(const Matrix4& m);
  Object3D& applyQuaternion(const Quaternion& q);
  Object3D& rotateOnAxis(const Vector3& axis, double angle);
  Object3D& rotateOnWorldAxis(const Vector3& axis, double angle);
  Object3D& rotateX(double angle);
  Object3D& rotateY(double angle);
  Object3D& rotateZ(double angle);
  Object3D& translateOnAxis(const Vector3& axis, double distance);
  Object3D& translateX(double d);
  Object3D& translateY(double d);
  Object3D& translateZ(double d);
  Vector3& localToWorld(Vector3& v) const;
  Vector3& worldToLocal(Vector3& v) const;
  Vector3& getWorldPosition(Vector3& target);
  Quaternion& getWorldQuaternion(Quaternion& target);
  Vector3& getWorldScale(Vector3& target);
  virtual Vector3& getWorldDirection(Vector3& target);
  void traverse(const std::function<void(Object3D&)>& fn);
  void traverseVisible(const std::function<void(Object3D&)>& fn);
  /** the first object below (or this) with that name (null: none) */
  Object3D* getObjectByName(const std::string& n);
  /** `layers.set(n)` / `layers.enable(n)` / `layers.disable(n)` */
  void layersSet(int n) { layers = 1u << n; }
  void layersEnable(int n) { layers |= 1u << n; }
  void layersDisable(int n) { layers &= ~(1u << n); }

  template <class T>
  std::shared_ptr<T> as() {
    return std::dynamic_pointer_cast<T>(shared_from_this());
  }

 protected:
  explicit Object3D(ObjectType t);
};

class Group : public Object3D {
 public:
  Group() : Object3D(ObjectType::Group) {}
};

class Mesh : public Object3D {
 public:
  GeometryPtr geometry;
  /** the material – or the materials of the geometry's groups, by materialIndex */
  MaterialPtr material;
  std::vector<MaterialPtr> materials;

  Mesh() : Object3D(ObjectType::Mesh) {}
  Mesh(GeometryPtr g, MaterialPtr m) : Object3D(ObjectType::Mesh), geometry(std::move(g)), material(std::move(m)) {}
  Mesh(GeometryPtr g, std::vector<MaterialPtr> ms) : Object3D(ObjectType::Mesh), geometry(std::move(g)), materials(std::move(ms)) {}

 protected:
  Mesh(ObjectType t, GeometryPtr g, MaterialPtr m) : Object3D(t), geometry(std::move(g)), material(std::move(m)) {}
};

/** `new THREE.InstancedMesh(geometry, material, count)` */
class InstancedMesh : public Mesh {
 public:
  /** how many instances are drawn (may be lowered below what there is room for) */
  int count;
  /** 16 numbers an instance */
  AttrPtr instanceMatrix;
  /** 3 numbers an instance (null until setColorAt is first called) */
  AttrPtr instanceColor;
  std::optional<Sphere> boundingSphere;

  InstancedMesh(GeometryPtr g, MaterialPtr m, int capacity);
  void setMatrixAt(int i, const Matrix4& m);
  void getMatrixAt(int i, Matrix4& m) const;
  void setColorAt(int i, const Color& c);
  void getColorAt(int i, Color& c) const;
  void computeBoundingSphere();
  int capacity() const { return capacity_; }
  /** InstancedMesh.dispose(): the graphics card's buffers of the instances are given back (the geometry and material stay theirs) */
  void dispose();
  ~InstancedMesh() override;
  /** the vertex array of this mesh: its geometry's arrays with its own instance arrays (the renderer's) */
  mutable uint32_t gpuVao = 0;
  mutable uint64_t gpuLayout = 0;

 private:
  int capacity_;
};

class Sprite : public Object3D {
 public:
  MaterialPtr material;
  /** where the sprite is held: (0.5, 0.5) its middle */
  Vector2 center{0.5, 0.5};
  explicit Sprite(MaterialPtr m) : Object3D(ObjectType::Sprite), material(std::move(m)) {}
};

class Points : public Mesh {
 public:
  Points(GeometryPtr g, MaterialPtr m) : Mesh(ObjectType::Points, std::move(g), std::move(m)) {}
};

/** a line through the points (`THREE.Line`) */
class Line : public Mesh {
 public:
  Line(GeometryPtr g, MaterialPtr m) : Mesh(ObjectType::Line, std::move(g), std::move(m)) {}

 protected:
  Line(ObjectType t, GeometryPtr g, MaterialPtr m) : Mesh(t, std::move(g), std::move(m)) {}
};

/** pairs of points, each a line (`THREE.LineSegments`) */
class LineSegments : public Line {
 public:
  LineSegments(GeometryPtr g, MaterialPtr m) : Line(ObjectType::LineSegments, std::move(g), std::move(m)) {}
};

class LOD : public Object3D {
 public:
  struct Level {
    ObjectPtr object;
    double distance;
    double hysteresis;
  };
  std::vector<Level> levels;
  bool autoUpdate = true;

  LOD() : Object3D(ObjectType::LOD) {}
  LOD& addLevel(const ObjectPtr& object, double distance = 0, double hysteresis = 0);
  /** show the level for the camera's distance (the renderer calls it when autoUpdate is set) */
  void update(const Camera& camera);
  int getCurrentLevel() const { return current_; }

 private:
  int current_ = 0;
};

class Fog {
 public:
  Color color;
  double near, far;
  Fog(const Color& c, double n, double f) : color(c), near(n), far(f) {}
};

class Scene : public Object3D {
 public:
  std::shared_ptr<Fog> fog;
  /** scene.background as a colour (none: the clear colour) */
  std::optional<Color> background;
  Scene() : Object3D(ObjectType::Scene) {}
};

class Camera : public Object3D {
 public:
  Matrix4 matrixWorldInverse;
  Matrix4 projectionMatrix;
  Matrix4 projectionMatrixInverse;
  bool isPerspective = true;
  // PerspectiveCamera
  double fov = 50, aspect = 1, near = 0.1, far = 2000, zoom = 1;
  // OrthographicCamera
  double left = -1, right = 1, top = 1, bottom = -1;
  /** setViewOffset: draw only a window of the full picture */
  struct ViewOffset {
    bool enabled = false;
    double fullWidth = 1, fullHeight = 1, offsetX = 0, offsetY = 0, width = 1, height = 1;
  } view;

  Camera() : Object3D(ObjectType::Camera) {}
  void updateProjectionMatrix();
  void updateMatrixWorld(bool force = false) override;
  void updateWorldMatrix(bool updateParents, bool updateChildren, bool force = false) override;
  Vector3& getWorldDirection(Vector3& target) override;
  void setViewOffset(double fullWidth, double fullHeight, double x, double y, double width, double height);
  void clearViewOffset();
};

/** `new THREE.PerspectiveCamera(fov, aspect, near, far)` */
std::shared_ptr<Camera> perspectiveCamera(double fov = 50, double aspect = 1, double near = 0.1, double far = 2000);
/** `new THREE.OrthographicCamera(left, right, top, bottom, near, far)` */
std::shared_ptr<Camera> orthographicCamera(double left, double right, double top, double bottom, double near = 0.1, double far = 2000);

/** `vector.project(camera)` / `vector.unproject(camera)` */
Vector3& project(Vector3& v, const Camera& camera);
Vector3& unproject(Vector3& v, const Camera& camera);

enum class LightType { Ambient, Hemisphere, Directional, Point, Spot };

class Light : public Object3D {
 public:
  const LightType lightType;
  Color color{1, 1, 1};
  double intensity = 1;
  // HemisphereLight
  Color groundColor{1, 1, 1};
  // DirectionalLight, SpotLight: where it shines to (a child of the scene, or left at the origin)
  ObjectPtr target;
  // PointLight, SpotLight
  double distance = 0, decay = 2;
  // SpotLight
  double angle = 1.0471975511965976, penumbra = 0;
  // DirectionalLight.shadow
  struct Shadow {
    std::shared_ptr<Camera> camera;
    Vector2 mapSize{512, 512};
    double bias = 0, normalBias = 0, radius = 1, intensity = 1;
    /** the shadow map is drawn anew every frame unless this is false (then: when needsUpdate) */
    bool autoUpdate = true, needsUpdate = false;
    Matrix4 matrix;
    mutable RenderTargetPtr map;
  } shadow;

  explicit Light(LightType t);
};

using LightPtr = std::shared_ptr<Light>;
/** `new THREE.AmbientLight(color, intensity)` … */
LightPtr ambientLight(const Color& color, double intensity = 1);
LightPtr hemisphereLight(const Color& sky, const Color& ground, double intensity = 1);
LightPtr directionalLight(const Color& color, double intensity = 1);
LightPtr pointLight(const Color& color, double intensity = 1, double distance = 0, double decay = 2);
LightPtr spotLight(const Color& color, double intensity = 1, double distance = 0, double angle = 1.0471975511965976, double penumbra = 0, double decay = 2);

// ---------------------------------------------------------------------------
// Raycaster (what the game asks of it: the nearest hit of a ray with meshes)
// ---------------------------------------------------------------------------

struct Intersection {
  double distance = 0;
  Vector3 point;
  Object3D* object = nullptr;
  /** the triangle's corner indices, and its normal in the object's frame */
  int a = 0, b = 0, c = 0;
  Vector3 normal;
  int instanceId = -1;
};

class Raycaster {
 public:
  Ray ray;
  double near = 0, far = 1e300;
  Raycaster() = default;
  Raycaster(const Vector3& origin, const Vector3& direction, double n = 0, double f = 1e300) : ray(origin, direction), near(n), far(f) {}
  void set(const Vector3& origin, const Vector3& direction) { ray.set(origin, direction); }
  /** setFromCamera(ndc, camera): the ray through a point of the picture (-1 … 1) */
  void setFromCamera(const Vector2& ndc, const Camera& camera);
  /** the hits with the object (and what is below it when `recursive`), nearest first */
  std::vector<Intersection> intersectObject(Object3D& object, bool recursive = true) const;
  std::vector<Intersection> intersectObjects(const std::vector<ObjectPtr>& objects, bool recursive = true) const;
};

}  // namespace fs
