// SPDX-License-Identifier: AGPL-3.0-only
// LINK: platform core
// Does this machine draw off screen?  Clears the picture to a colour, writes it as a PNG, says which driver drew.
#include <GLES3/gl3.h>
#include <cstdio>

#include "platform/glhost.h"

int main(int argc, char** argv) {
  using namespace fs::plat;
  if (!gl::createContext("", 320, 180)) return 1;
  std::printf("driver: %s\nversion: %s\n", gl::rendererName().c_str(), reinterpret_cast<const char*>(glGetString(GL_VERSION)));
  glBindFramebuffer(GL_FRAMEBUFFER, gl::screenFramebuffer());
  glViewport(0, 0, 320, 180);
  glClearColor(0.2f, 0.5f, 0.8f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  const char* out = argc > 1 ? argv[1] : "gl_probe.png";
  std::printf("%s: %s\n", out, gl::savePng(out, 320, 180) ? "written" : "NOT written");
  return 0;
}
