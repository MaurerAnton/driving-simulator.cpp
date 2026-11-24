// SPDX-License-Identifier: AGPL-3.0-only
// LINK: engine core platform
/**
 * The engine without the graphics card: attributes, geometries, the scene graph, cameras, lights,
 * materials, textures, the raycaster, the geometries of three.js, curves and shapes.  The numbers
 * themselves are compared with three.js in tools/diff/three-x.mjs + tools/dev/three_x_dump.cpp;
 * here is what must hold whatever the numbers are.
 */
#include <cassert>
#include <cmath>
#include <cstdio>

#include "engine/engine.h"
#include "engine/geometries.h"
#include "engine/gpu.h"

using namespace fs;

static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
static bool near(const Vector3& a, const Vector3& b, double eps = 1e-9) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

/** how many handles of a kind wait to be freed */
static size_t freedCount(gpu::Kind kind) {
  size_t n = 0;
  for (const gpu::Freed& f : gpu::freed())
    if (f.kind == kind) n++;
  return n;
}

static void testColor() {
  // a hex number is an sRGB colour, kept as linear numbers
  const Color c(0x808890);
  assert(near(c.r, 0.21586050010324417) && near(c.g, 0.24620132669705552) && near(c.b, 0.2788942634659966));
  assert(c.getHex() == 0x808890 && c.getHexString() == "808890" && c.getStyle() == "rgb(128,136,144)");
  assert(Color(1, 0.5, 0.25).r == 1 && Color(1, 0.5, 0.25).g == 0.5);   // three numbers are taken as they are
  assert(Color("#f00").equals(Color(0xff0000)) && Color("rgb(255,0,0)").equals(Color(0xff0000)) && Color("red").equals(Color(0xff0000)));
  const Color green("hsl(120,100%,50%)");
  assert(near(green.r, 0, 1e-12) && green.g == 1 && green.b == 0 && green.getHex() == 0x00ff00);
  assert(Color().equals(Color(0xffffff)) && Color(0).equals(Color(0, 0, 0)));
  Color d;
  d.set(0x808890);
  assert(d.equals(c));
  d.set(1, 2, 3);
  assert(d.b == 3);
  d.set("nothing of the kind");
  assert(d.b == 3);
  // the header's example: an int literal in a designated initialiser
  const MaterialPtr m = lambert({.color = 0x808890});
  assert(m->type == MaterialType::Lambert && m->color.equals(c));
  const MaterialParams p{.color = 0x808890, .transparent = true, .opacity = 0.5};
  assert(p.color && p.color->equals(c) && p.transparent && *p.transparent);
}

static void testAttributes() {
  auto a = attr(std::vector<uint8_t>(8), 4, true);
  a->setXYZW(1, 0, 0.5, 1, 2);   // 0, 128, 255, 510 & 255
  assert(a->u8[4] == 0 && a->u8[5] == 128 && a->u8[6] == 255 && a->u8[7] == 254);
  assert(near(a->getY(1), 128 / 255.0) && a->getZ(1) == 1 && a->count() == 2 && a->length() == 8 && a->byteLength() == 8);
  auto f = attr(std::vector<float>{1, 2, 3, 4, 5, 6}, 3);
  assert(f->count() == 2 && f->getZ(1) == 6 && f->byteLength() == 24 && f->data() == f->f32.data());
  const uint32_t version = f->version;
  f->needsUpdate();
  assert(f->version == version + 1);
  auto g = f->clone();
  g->setX(0, 9);
  assert(f->getX(0) == 1 && g->getX(0) == 9 && g->itemSize == 3);
  f->applyMatrix4(Matrix4().makeTranslation(1, 1, 1));
  assert(f->getX(0) == 2 && f->getZ(1) == 7);
  assert(std::isnan(f->at(99)));
  f->put(99, 1);   // outside: nothing happens
  // handed back when it goes
  const size_t before = freedCount(gpu::Kind::Buffer);
  {
    auto h = attr(std::vector<float>(3), 3);
    h->gpu = 77;
  }
  assert(freedCount(gpu::Kind::Buffer) == before + 1 && gpu::freed().back().handle == 77);
}

static void testGeometry() {
  auto g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0}, 3));
  g->setAttribute("uv", attr(std::vector<float>{0, 0, 1, 0, 0, 1, 1, 1}, 2));
  g->setIndex(std::vector<uint32_t>{0, 1, 2, 2, 1, 3});
  assert(g->index->type == AttrType::U16 && g->index->count() == 6);
  assert(make<BufferGeometry>()->setIndex(std::vector<uint32_t>{0, 65535, 1}).index->type == AttrType::U32);
  g->computeVertexNormals();
  assert(g->attributes.size() == 3 && g->attributes[2].first == "normal");
  assert(near(g->normal()->getZ(0), 1) && near(g->normal()->getX(3), 0));
  g->computeBoundingBox();
  g->computeBoundingSphere();
  assert(g->boundingBox->min == Vector3(0, 0, 0) && g->boundingBox->max == Vector3(1, 1, 0));
  assert(near(g->boundingSphere->center, Vector3(0.5, 0.5, 0)) && near(g->boundingSphere->radius, std::sqrt(0.5), 1e-7));
  g->translate(1, 2, 3);   // the bounds follow
  assert(g->boundingBox->min == Vector3(1, 2, 3) && near(g->boundingSphere->center, Vector3(1.5, 2.5, 3)));
  g->addGroup(0, 3, 0).addGroup(3, 3, 1);
  const GeometryPtr n = g->toNonIndexed();
  assert(!n->index && n->position()->count() == 6 && n->uv()->count() == 6 && n->groups.size() == 2 && n->toNonIndexed() == n);
  const GeometryPtr c = g->clone();
  assert(c->id != g->id && c->position() != g->position() && c->position()->count() == 4 && c->index->count() == 6 && c->groups.size() == 2 && c->boundingBox);
  c->deleteAttribute("uv");
  assert(!c->hasAttribute("uv") && g->hasAttribute("uv"));
  g->center();
  assert(near(g->position()->getX(0), -0.5) && near(g->position()->getY(3), 0.5));
  // a geometry rotated: its normals turn with it
  g->rotateX(PI / 2);
  assert(near(g->normal()->getY(0), -1, 1e-6));
  // dispose hands back the vertex array and the buffers; so does the end of the geometry
  const size_t vaos = freedCount(gpu::Kind::VertexArray), buffers = freedCount(gpu::Kind::Buffer);
  g->gpuVao = 5;
  g->position()->gpu = 6;
  g->index->gpu = 7;
  g->dispose();
  assert(g->gpuVao == 0 && g->position()->gpu == 0 && freedCount(gpu::Kind::VertexArray) == vaos + 1 && freedCount(gpu::Kind::Buffer) == buffers + 2);
}

static void testObjects() {
  auto root = make<Scene>();
  auto a = make<Group>();
  auto b = make<Mesh>(boxGeometry(), basic());
  assert(a->id != b->id && root->type == ObjectType::Scene && b->type == ObjectType::Mesh);
  root->add(a);
  a->add(b);
  assert(a->parent == root.get() && b->parent == a.get() && root->children.size() == 1);
  a->position.set(1, 2, 3);
  a->rotation.y = PI / 2;
  b->position.set(1, 0, 0);
  root->updateMatrixWorld();
  Vector3 world;
  assert(near(b->getWorldPosition(world), Vector3(1, 2, 2)));   // +x of a turned quarter left about y is -z
  // the angles and the quaternion are one
  assert(near(a->quaternion.y, std::sin(PI / 4)) && near(a->quaternion.w, std::cos(PI / 4)));
  a->quaternion.setFromAxisAngle(Vector3(1, 0, 0), 0.5);
  assert(near(a->rotation.x, 0.5) && near(a->rotation.y, 0) && near(a->rotation.z, 0));
  a->rotation.y = 0.25;   // after quaternion.copy(…): the x angle stays what the quaternion said
  assert(near(a->rotation.x, 0.5) && near(a->rotation.y, 0.25));
  Quaternion expect;
  expect.setFromEuler(Euler(0.5, 0.25, 0));
  assert(near(a->quaternion.x, expect.x) && near(a->quaternion.y, expect.y) && near(a->quaternion.z, expect.z) && near(a->quaternion.w, expect.w));
  a->rotation.x += 0.5;
  assert(near(a->rotation.x, 1));
  a->rotation.set(0.1, 0.2, 0.3, Euler::YXZ);
  assert(a->rotation.order() == Euler::YXZ && near(a->rotation.euler().z, 0.3));
  expect.setFromEuler(Euler(0.1, 0.2, 0.3, Euler::YXZ));
  assert(near(a->quaternion.w, expect.w));
  // attach keeps the place in the world
  root->updateMatrixWorld(true);
  const Matrix4 before = b->matrixWorld;
  root->attach(b);
  assert(b->parent == root.get() && a->children.empty() && root->children.size() == 2);
  for (int i = 0; i < 16; i++) assert(near(b->matrixWorld.elements[i], before.elements[i], 1e-9));
  // lookAt: an object's +z, a camera's -z
  auto cam = perspectiveCamera();
  cam->position.set(0, 0, 5);
  cam->lookAt(0, 0, 0);
  Vector3 dir;
  assert(near(cam->getWorldDirection(dir), Vector3(0, 0, -1)));
  b->removeFromParent();
  b->position.set(0, 0, 5);
  b->quaternion.identity();
  b->lookAt(Vector3(5, 0, 5));
  assert(near(b->getWorldDirection(dir), Vector3(1, 0, 0)));
  // remove, clear, the order of a traverse, a name
  a->name = "a";
  auto c = make<Object3D>();
  c->name = "c";
  a->add(c);
  std::string order;
  root->traverse([&](Object3D& o) { order += o.name.empty() ? "?" : o.name; });
  assert(order == "?ac");
  assert(root->getObjectByName("c") == c.get() && root->getObjectByName("x") == nullptr);
  a->visible = false;
  order.clear();
  root->traverseVisible([&](Object3D& o) { order += o.name.empty() ? "?" : o.name; });
  assert(order == "?");
  a->remove(c);
  assert(c->parent == nullptr && a->children.empty());
  a->add(c);
  a->add(a);   // not into itself
  assert(a->children.size() == 1);
  root->clear();
  assert(root->children.empty() && a->parent == nullptr);
  // the parent's may be the last reference: the object goes, nothing is touched after
  auto holder = make<Group>();
  holder->add(make<Group>());
  holder->children[0]->removeFromParent();
  assert(holder->children.empty());
  holder->add(make<Group>());
  auto other = make<Group>();
  other->add(holder->children[0]);   // (a reference into the list it is taken out of)
  assert(holder->children.empty() && other->children.size() == 1 && other->children[0]->parent == other.get());
  // a child outlives its parent without one
  auto orphan = make<Group>();
  {
    auto parent = make<Group>();
    parent->add(orphan);
  }
  assert(orphan->parent == nullptr);
  // layers
  c->layersSet(3);
  assert(c->layers == 8u);
  c->layersEnable(0);
  c->layersDisable(3);
  assert(c->layers == 1u);
  assert(c->as<Mesh>() == nullptr && b->as<Mesh>() == b);
}

static void testCamerasAndLights() {
  auto cam = perspectiveCamera(50, 2, 0.1, 2000);
  assert(cam->isPerspective && near(cam->projectionMatrix.elements[5], 1 / std::tan(25 * PI / 180)) && near(cam->projectionMatrix.elements[0], cam->projectionMatrix.elements[5] / 2));
  assert(near(cam->projectionMatrix.elements[10], -(2000 + 0.1) / (2000 - 0.1)) && cam->projectionMatrix.elements[11] == -1);
  cam->position.set(3, 4, 5);
  cam->rotation.set(-0.3, 0.6, 0, Euler::YXZ);
  cam->updateMatrixWorld();
  Matrix4 id;
  id.multiplyMatrices(cam->matrixWorld, cam->matrixWorldInverse);
  for (int i = 0; i < 16; i++) assert(near(id.elements[i], i % 5 == 0 ? 1 : 0));
  // into the picture and back
  Vector3 p(1, 2, -20), q = p;
  p.applyMatrix4(cam->matrixWorld);   // a point 20 before the camera
  q = p;
  project(q, *cam);
  assert(std::fabs(q.x) < 1 && std::fabs(q.y) < 1 && q.z > -1 && q.z < 1);
  assert(near(unproject(q, *cam), p, 1e-6));
  assert(near(p.clone().project(*cam), Vector3(q).project(*cam)));
  // a window of the full picture: the left half
  cam->setViewOffset(1000, 500, 0, 0, 500, 500);
  assert(near(cam->aspect, 2) && near(cam->projectionMatrix.elements[0], cam->projectionMatrix.elements[5]) && near(cam->projectionMatrix.elements[8], -1));
  cam->clearViewOffset();
  assert(near(cam->projectionMatrix.elements[8], 0));
  cam->zoom = 2;
  cam->updateProjectionMatrix();
  assert(near(cam->projectionMatrix.elements[5], 2 / std::tan(25 * PI / 180)));
  auto ortho = orthographicCamera(-4, 4, 2, -2, 1, 11);
  assert(!ortho->isPerspective && near(ortho->projectionMatrix.elements[0], 0.25) && near(ortho->projectionMatrix.elements[5], 0.5) && near(ortho->projectionMatrix.elements[10], -0.2) &&
         near(ortho->projectionMatrix.elements[14], -1.2));
  // the lights as three.js makes them
  const LightPtr sun = directionalLight(0xffffff, 2);
  assert(sun->type == ObjectType::Light && sun->lightType == LightType::Directional && sun->intensity == 2 && sun->position == Vector3(0, 1, 0) && sun->target && !sun->castShadow);
  const Camera& sc = *sun->shadow.camera;
  assert(!sc.isPerspective && sc.left == -5 && sc.right == 5 && sc.top == 5 && sc.bottom == -5 && sc.near == 0.5 && sc.far == 500 && sun->shadow.mapSize == Vector2(512, 512));
  assert(near(sc.projectionMatrix.elements[0], 0.2));
  const LightPtr hemi = hemisphereLight(0xffffff, 0x000000, 0.5);
  assert(hemi->groundColor.equals(Color(0, 0, 0)) && hemi->position == Vector3(0, 1, 0) && hemi->intensity == 0.5);
  const LightPtr spot = spotLight(0xffffff);
  assert(near(spot->angle, PI / 3) && spot->decay == 2 && spot->target && spot->shadow.camera->isPerspective && spot->shadow.camera->fov == 50);
  assert(pointLight(0xffffff)->shadow.camera->fov == 90 && ambientLight(0x404040, 0.3)->intensity == 0.3);
  // a light looks along -z, as a camera does
  sun->position.set(0, 0, 0);
  sun->lookAt(1, 0, 0);
  Vector3 dir;
  assert(near(sun->getWorldDirection(dir), Vector3(-1, 0, 0)));   // (its +z: only a camera turns the answer round)
}

static void testLod() {
  auto lod = make<LOD>();
  auto fine = make<Group>(), coarse = make<Group>();
  lod->addLevel(coarse, 100, 0.1);
  lod->addLevel(fine);
  assert(lod->levels.size() == 2 && lod->levels[0].object == fine && lod->children.size() == 2);
  auto cam = perspectiveCamera();
  const auto at = [&](double d) {
    cam->position.set(d, 0, 0);
    cam->updateMatrixWorld();
    lod->update(*cam);
    return lod->getCurrentLevel();
  };
  assert(at(50) == 0 && fine->visible && !coarse->visible);
  assert(at(101) == 1 && !fine->visible && coarse->visible);
  assert(at(95) == 1);   // within the hysteresis: it stays
  assert(at(89) == 0);
  assert(at(95) == 0);
}

static void testMaterialsAndTextures() {
  assert(basic()->type == MaterialType::Basic && phong()->type == MaterialType::Phong && lineBasic()->type == MaterialType::Line && pointsMaterial()->type == MaterialType::Points);
  assert(basic()->color.equals(Color(1, 1, 1)) && basic()->fog && !basic()->transparent && basic()->side == Side::Front);
  assert(phong()->specular.equals(Color(0x111111)) && phong()->shininess == 30 && lambert()->emissive.equals(Color(0, 0, 0)));
  assert(spriteMaterial()->transparent && !spriteMaterial({.transparent = false})->transparent);
  const MaterialPtr shader = shaderMaterial("void main() {}", "void main() {}", {{"uTime", uniform(0.0)}});
  assert(shader->type == MaterialType::Shader && !shader->fog && shader->uniforms.count("uTime") == 1 && shaderMaterial("", "", {}, {.fog = true})->fog);
  const MaterialPtr m = phong({.color = 0x3a74c6, .transparent = true, .opacity = 0.4, .side = Side::Double, .emissive = 0x101010, .name = "glass"});
  m->patch = make<ShaderPatch>();
  const MaterialPtr c = m->clone();
  assert(c->id != m->id && c->type == MaterialType::Phong && c->color.equals(m->color) && c->transparent && c->opacity == 0.4 && c->side == Side::Double && c->name == "glass" &&
         c->emissive.equals(Color(0x101010)));
  assert(!c->patch);   // as three.js: what onBeforeCompile was set to is not copied
  c->color.setHex(0);
  assert(!m->color.equals(c->color));
  // the uniforms of a copy are its own
  const MaterialPtr s2 = shader->clone();
  s2->uniforms["uTime"]->value = 5.0;
  assert(std::get<double>(shader->uniforms["uTime"]->value) == 0 && s2->uniforms["uTime"] != shader->uniforms["uTime"]);
  const uint32_t version = m->version;
  m->needsUpdate();
  assert(m->version == version + 1);
  // textures
  const TexturePtr data = dataTexture(std::vector<uint8_t>(4 * 4 * 4), 4, 4);
  assert(data->width == 4 && data->data->size() == 64 && !data->flipY && !data->generateMipmaps && data->magFilter == Filter::Nearest && data->minFilter == Filter::Nearest &&
         data->format == PixelFormat::RGBA && data->version == 0);
  data->needsUpdate();
  assert(data->version == 1);
  const TexturePtr canvas = canvasTexture(Canvas());
  assert(canvas->flipY && canvas->generateMipmaps && canvas->minFilter == Filter::LinearMipmapLinear && canvas->magFilter == Filter::Linear && canvas->version == 1 && !canvas->srgb);
  const size_t textures = freedCount(gpu::Kind::Texture), framebuffers = freedCount(gpu::Kind::Framebuffer), renderbuffers = freedCount(gpu::Kind::Renderbuffer);
  {
    RenderTarget target(320, 200);
    assert(target.texture->isRenderTarget && target.texture->width == 320 && !target.texture->generateMipmaps && target.texture->minFilter == Filter::Linear && !target.texture->flipY &&
           target.depthBuffer && target.samples == 0);
    target.gpuFbo = 3;
    target.gpuDepth = 4;
    target.texture->gpu = 5;
    target.setSize(320, 200);   // the same size: nothing happens
    assert(target.gpuFbo == 3);
    target.setSize(64, 48);     // another: what was made for the old size is handed back
    assert(target.width == 64 && target.texture->height == 48 && target.gpuFbo == 0 && target.texture->gpu == 0);
    assert(freedCount(gpu::Kind::Framebuffer) == framebuffers + 1 && freedCount(gpu::Kind::Renderbuffer) == renderbuffers + 1 && freedCount(gpu::Kind::Texture) == textures + 1);
    target.gpuFbo = 9;
  }
  assert(freedCount(gpu::Kind::Framebuffer) == framebuffers + 2);
  // a material hands back its programs
  int programs = 0;
  const size_t freedPrograms = freedCount(gpu::Kind::Programs);
  {
    const MaterialPtr gone = basic();
    gone->gpuPrograms = &programs;
  }
  assert(freedCount(gpu::Kind::Programs) == freedPrograms + 1 && gpu::freed().back().pointer == &programs);
}

static void testRaycaster() {
  auto mesh = make<Mesh>(planeGeometry(2, 2), basic());   // in the xy plane, facing +z (the rays keep off its diagonal: there both triangles are hit)
  mesh->position.set(0, 0, -3);
  mesh->updateMatrixWorld();
  const Raycaster front(Vector3(0.25, 0.5, 5), Vector3(0, 0, -1));
  std::vector<Intersection> hits = front.intersectObject(*mesh);
  assert(hits.size() == 1 && near(hits[0].distance, 8) && near(hits[0].point, Vector3(0.25, 0.5, -3)) && hits[0].object == mesh.get() && near(hits[0].normal, Vector3(0, 0, 1)) &&
         hits[0].instanceId == -1);
  const Raycaster back(Vector3(0.25, 0.5, -9), Vector3(0, 0, 1));
  assert(back.intersectObject(*mesh).empty());   // the back of a front-sided material
  mesh->material->side = Side::Back;
  assert(back.intersectObject(*mesh).size() == 1 && front.intersectObject(*mesh).empty());
  mesh->material->side = Side::Double;
  assert(back.intersectObject(*mesh).size() == 1 && front.intersectObject(*mesh).size() == 1);
  assert(Raycaster(Vector3(0.25, 0.5, 5), Vector3(0, 0, -1), 0, 7).intersectObject(*mesh).empty());    // too far
  assert(Raycaster(Vector3(0.25, 0.5, 5), Vector3(0, 0, -1), 9, 20).intersectObject(*mesh).empty());   // too near
  assert(Raycaster(Vector3(5, 5, 5), Vector3(0, 0, -1)).intersectObject(*mesh).empty());                // beside it
  // below a group, nearest first
  auto group = make<Group>();
  auto second = make<Mesh>(planeGeometry(2, 2), basic({.side = Side::Double}));
  second->position.set(0, 0, 1);
  group->add(mesh, second);
  group->updateMatrixWorld(true);
  hits = front.intersectObject(*group);
  assert(hits.size() == 2 && hits[0].object == second.get() && hits[1].object == mesh.get() && hits[0].distance < hits[1].distance);
  assert(front.intersectObject(*group, false).empty() && front.intersectObjects({mesh, second}).size() == 2);
  // instances
  auto instances = make<InstancedMesh>(planeGeometry(1, 1), basic({.side = Side::Double}), 3);
  for (int i = 0; i < 3; i++) instances->setMatrixAt(i, Matrix4().makeTranslation(i * 10.0, 0, 0));
  instances->updateMatrixWorld();
  hits = Raycaster(Vector3(20.1, 0.3, 5), Vector3(0, 0, -1)).intersectObject(*instances);
  assert(hits.size() == 1 && hits[0].instanceId == 2 && hits[0].object == instances.get() && near(hits[0].distance, 5));
  Matrix4 m;
  instances->getMatrixAt(1, m);
  assert(m.elements[12] == 10 && instances->instanceMatrix->divisor == 1 && instances->capacity() == 3 && !instances->instanceColor);
  instances->setColorAt(1, Color(1, 0, 0));
  Color c;
  instances->getColorAt(1, c);
  assert(c.equals(Color(1, 0, 0)) && instances->instanceColor->count() == 3 && instances->instanceColor->getX(0) == 1);
  // through a point of the picture
  auto cam = perspectiveCamera(60, 1, 0.1, 100);
  cam->position.set(0, 0, 10);
  cam->updateMatrixWorld();
  Raycaster picked;
  picked.setFromCamera(Vector2(0, 0), *cam);
  assert(near(picked.ray.origin, Vector3(0, 0, 10)) && near(picked.ray.direction, Vector3(0, 0, -1)));
}

static void testGeometries() {
  const GeometryPtr box = boxGeometry(1, 2, 3);
  assert(box->position()->count() == 24 && box->index->count() == 36 && box->groups.size() == 6 && box->groups[5].start == 30 && box->groups[5].materialIndex == 5);
  assert(box->attributes[0].first == "position" && box->attributes[1].first == "normal" && box->attributes[2].first == "uv");
  box->computeBoundingBox();
  assert(box->boundingBox->min == Vector3(-0.5, -1, -1.5) && box->boundingBox->max == Vector3(0.5, 1, 1.5));
  const GeometryPtr plane = planeGeometry(2, 2, 2, 2);
  assert(plane->position()->count() == 9 && plane->index->count() == 24 && plane->normal()->getZ(4) == 1);
  const GeometryPtr sphere = sphereGeometry();
  assert(sphere->position()->count() == 33 * 17 && sphere->index->count() == 32 * 15 * 6);
  sphere->computeBoundingSphere();
  assert(near(sphere->boundingSphere->radius, 1, 1e-6));
  const GeometryPtr cylinder = cylinderGeometry(1, 1, 2, 8);
  assert(cylinder->groups.size() == 3 && cylinder->position()->count() == 18 + 2 * 17 && cylinder->index->count() == 8 * 6 + 2 * 8 * 3);
  assert(cylinderGeometry(1, 1, 2, 8, 1, true)->groups.size() == 1 && coneGeometry(1, 2, 8)->groups.size() == 2);
  assert(torusGeometry()->position()->count() == 13 * 49 && circleGeometry(1, 8)->index->count() == 24 && ringGeometry()->position()->count() == 66);
  assert(icosahedronGeometry()->position()->count() == 60 && !icosahedronGeometry()->index && icosahedronGeometry(1, 1)->position()->count() == 240);
  const GeometryPtr lathe = latheGeometry({Vector2(0, -0.5), Vector2(0.5, 0), Vector2(0, 0.5)});
  assert(lathe->position()->count() == 13 * 3 && lathe->attributes[1].first == "uv");
  // curves
  const LineCurve3 line(Vector3(0, 0, 0), Vector3(3, 4, 0));
  assert(near(line.getLength(), 5) && near(line.getPointAt(0.5), Vector3(1.5, 2, 0)) && near(line.getTangent(0.2), Vector3(0.6, 0.8, 0)));
  const CatmullRomCurve3 spline({Vector3(0, 0, 0), Vector3(1, 1, 0), Vector3(2, 0, 0), Vector3(3, 1, 0)});
  assert(near(spline.getPoint(0), Vector3(0, 0, 0)) && near(spline.getPoint(1), Vector3(3, 1, 0)) && spline.getPoints(10).size() == 11 && spline.getLength() > 4);
  const GeometryPtr tube = tubeGeometry(spline, 16, 0.1, 5);
  assert(tube->position()->count() == 17 * 6 && tube->index->count() == 16 * 5 * 6);
  // shapes
  Shape square({Vector2(0, 0), Vector2(2, 0), Vector2(2, 2), Vector2(0, 2)});
  assert(near(square.getLength(), 6) && near(ShapeUtils::area(square.getPoints()), 4) && !ShapeUtils::isClockWise(square.getPoints()));
  std::vector<Vector2> contour = square.getPoints();
  assert(ShapeUtils::triangulateShape(contour).size() == 2);
  square.holes.push_back(make<Path>(std::vector<Vector2>{Vector2(0.5, 0.5), Vector2(1.5, 0.5), Vector2(1.5, 1.5), Vector2(0.5, 1.5)}));
  const GeometryPtr flat = shapeGeometry(square);
  assert(flat->position()->count() == 8 && flat->index->count() == 8 * 3 && flat->groups.empty());
  const GeometryPtr solid = extrudeGeometry(square, {.depth = 0.5, .bevelEnabled = false});
  assert(!solid->index && solid->groups.size() == 2 && solid->position()->count() == 2 * 8 * 3 + 8 * 6 && solid->hasAttribute("normal") && solid->attributes[1].first == "uv");
  solid->computeBoundingBox();
  assert(solid->boundingBox->min == Vector3(0, 0, 0) && solid->boundingBox->max == Vector3(2, 2, 0.5));
  // merging
  const GeometryPtr merged = mergeGeometries({boxGeometry(), sphereGeometry(1, 8, 6)}, true);
  assert(merged && merged->position()->count() == 24 + 9 * 7 && merged->groups.size() == 2 && merged->groups[1].start == 36 && merged->index->getX(36) >= 24);
  assert(mergeGeometries({boxGeometry(), boxGeometry()->toNonIndexed()}) == nullptr);
  const GeometryPtr welded = mergeVertices(boxGeometry()->toNonIndexed());
  assert(welded->position()->count() == 24 && welded->index->count() == 36);   // (corners differ by their normals and uvs)
  const GeometryPtr corners = boxGeometry();
  corners->deleteAttribute("normal");
  corners->deleteAttribute("uv");
  assert(mergeVertices(corners)->position()->count() == 8);
}

int main() {
  testColor();
  testAttributes();
  testGeometry();
  testObjects();
  testCamerasAndLights();
  testLod();
  testMaterialsAndTextures();
  testRaycaster();
  testGeometries();
  std::printf("engine: all checks passed\n");
  return 0;
}
