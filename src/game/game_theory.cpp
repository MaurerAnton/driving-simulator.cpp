// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 (the theory lessons: game.ts:2693–2728)
#include "audio/audio.h"
#include "game/game.h"
#include "game/theorymode.h"
#include "input/input.h"
#include "platform/plat.h"
#include "render/far.h"
#include "render/props.h"
#include "render/signs.h"
#include "sim/traffic.h"
#include "ui/hud.h"

namespace fs {

void Game::openTheory() {
  if (!yardFrame) return;
  // (later: clearPreview, menuPreview – game.ts:2700)
  traffic->clear();
  // (later: rail.clear – game.ts:2703)
  hud->hideOverlay();
  hud->setHudVisible(false);
  audio->suspend();
  input->enabled = true;
  input->resetLook(false, true);
  state = "theory";
  if (!theory)
    theory = std::make_unique<TheoryMode>(TheoryHost{
        .app = &app,
        .ui = ui,
        .settings = &settings,
        .input = input.get(),
        .signs = signs.get(),
        .yard = &*yardFrame,
        .around =
            [this](double x, double z) {
              app.tiles->update(x, z);
              props->update(x, z);
              far->update(app.camera->position.x, app.camera->position.z);
            },
        .close =
            [this] {
              input->resetLook(cameraMode == "chase", cameraMode == "free");
              toMenu();
            },
    });
  theory->open(URLSearchParams(plat::locationSearch()).get("theory"));
}

}  // namespace fs
