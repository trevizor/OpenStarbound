#include "StarControllerBuildMenu.hpp"
#include "StarRoot.hpp"
#include "StarAssets.hpp"
#include "StarConfiguration.hpp"
#include "StarGuiReader.hpp"
#include "StarSliderBar.hpp"
#include "StarButtonWidget.hpp"
#include "StarLabelWidget.hpp"

#include <cmath>

namespace Star {

ControllerBuildMenu::ControllerBuildMenu(Json const& config) {
  GuiReader reader;

  reader.registerCallback("cancel", [&](Widget*) {
    dismiss();
  });

  reader.registerCallback("accept", [&](Widget*) {
    apply();
    dismiss();
  });

  reader.registerCallback("controllerMouseEnabledCheckbox", [&](Widget*) {
    updateControllerMouseEnabled();
  });

  reader.registerCallback("inventoryBuildFromInventoryCheckbox", [&](Widget*) {
    updateInventoryBuildFromInventory();
  });

  reader.registerCallback("controllerMouseSpeedSlider", [&](Widget*) {
    updateControllerMouseSpeed();
  });

  reader.registerCallback("controllerMouseDeadzoneSlider", [&](Widget*) {
    updateControllerMouseDeadzone();
  });

  reader.registerCallback("gameSpeedSlider", [&](Widget*) {
    updateGameSpeed();
  });

  reader.construct(config.get("paneLayout"), this);

  m_controllerMouseEnabledButton = fetchChild<ButtonWidget>("controllerMouseEnabledCheckbox");
  m_inventoryBuildFromInventoryButton = fetchChild<ButtonWidget>("inventoryBuildFromInventoryCheckbox");
  m_controllerMouseSpeedSlider = fetchChild<SliderBarWidget>("controllerMouseSpeedSlider");
  m_controllerMouseDeadzoneSlider = fetchChild<SliderBarWidget>("controllerMouseDeadzoneSlider");
  m_gameSpeedSlider = fetchChild<SliderBarWidget>("gameSpeedSlider");
  m_controllerMouseSpeedLabel = fetchChild<LabelWidget>("controllerMouseSpeedValueLabel");
  m_controllerMouseDeadzoneLabel = fetchChild<LabelWidget>("controllerMouseDeadzoneValueLabel");
  m_gameSpeedLabel = fetchChild<LabelWidget>("gameSpeedValueLabel");

  m_controllerMouseSpeedSlider->setRange(200, 3000, 50);
  m_controllerMouseDeadzoneSlider->setRange(0, 95, 1);
  m_gameSpeedSlider->setRange(50, 150, 5);

  initConfig();
  syncGui();
}

void ControllerBuildMenu::show() {
  Pane::show();
  initConfig();
  syncGui();
}

StringList const ControllerBuildMenu::ConfigKeys = {
  "controllerMouseEnabled",
  "controllerMouseSpeed",
  "controllerMouseDeadzone",
  "gameSpeed",
  "inventoryBuildFromInventory"
};

void ControllerBuildMenu::initConfig() {
  auto configuration = Root::singleton().configuration();
  for (auto const& key : ConfigKeys)
    m_localChanges.set(key, configuration->get(key));
}

void ControllerBuildMenu::syncGui() {
  bool enabled = m_localChanges.get("controllerMouseEnabled").optBool().value(true);
  int speed = (int)std::round(m_localChanges.get("controllerMouseSpeed").optFloat().value(1400.0f));
  int deadzonePercent = (int)std::round(m_localChanges.get("controllerMouseDeadzone").optFloat().value(0.20f) * 100.0f);
  int gameSpeedPercent = (int)std::round(clamp(m_localChanges.get("gameSpeed").optFloat().value(1.0f), 0.5f, 1.5f) * 100.0f);
  bool buildFromInventory = m_localChanges.get("inventoryBuildFromInventory").optBool().value(true);

  m_controllerMouseEnabledButton->setChecked(enabled);
  m_inventoryBuildFromInventoryButton->setChecked(buildFromInventory);

  m_controllerMouseSpeedSlider->setVal(speed, false);
  m_controllerMouseSpeedLabel->setText(toString(speed));

  deadzonePercent = clamp(deadzonePercent, 0, 95);
  m_controllerMouseDeadzoneSlider->setVal(deadzonePercent, false);
  m_controllerMouseDeadzoneLabel->setText(strf("{}%", deadzonePercent));

  m_gameSpeedSlider->setVal(gameSpeedPercent, false);
  m_gameSpeedLabel->setText(strf("{:.2f}x", gameSpeedPercent / 100.0f));
}

void ControllerBuildMenu::apply() {
  auto configuration = Root::singleton().configuration();
  for (auto const& pair : m_localChanges)
    configuration->set(pair.first, pair.second);
}

void ControllerBuildMenu::updateControllerMouseEnabled() {
  bool enabled = m_controllerMouseEnabledButton->isChecked();
  m_localChanges.set("controllerMouseEnabled", enabled);
  Root::singleton().configuration()->set("controllerMouseEnabled", enabled);
}

void ControllerBuildMenu::updateInventoryBuildFromInventory() {
  bool enabled = m_inventoryBuildFromInventoryButton->isChecked();
  m_localChanges.set("inventoryBuildFromInventory", enabled);
  Root::singleton().configuration()->set("inventoryBuildFromInventory", enabled);
}

void ControllerBuildMenu::updateControllerMouseSpeed() {
  int speed = m_controllerMouseSpeedSlider->val();
  m_localChanges.set("controllerMouseSpeed", speed);
  Root::singleton().configuration()->set("controllerMouseSpeed", speed);
  m_controllerMouseSpeedLabel->setText(toString(speed));
}

void ControllerBuildMenu::updateControllerMouseDeadzone() {
  float deadzone = m_controllerMouseDeadzoneSlider->val() / 100.0f;
  m_localChanges.set("controllerMouseDeadzone", deadzone);
  Root::singleton().configuration()->set("controllerMouseDeadzone", deadzone);
  m_controllerMouseDeadzoneLabel->setText(strf("{}%", m_controllerMouseDeadzoneSlider->val()));
}

void ControllerBuildMenu::updateGameSpeed() {
  float speed = m_gameSpeedSlider->val() / 100.0f;
  m_localChanges.set("gameSpeed", speed);
  Root::singleton().configuration()->set("gameSpeed", speed);
  m_gameSpeedLabel->setText(strf("{:.2f}x", speed));
}

}
