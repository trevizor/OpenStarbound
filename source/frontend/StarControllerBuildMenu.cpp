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

  reader.registerCallback("incomingDamageMultiplierSlider", [&](Widget*) {
    updateIncomingDamageMultiplier();
  });
  reader.registerCallback("ongoingDamageMultiplierSlider", [&](Widget*) {
    updateOngoingDamageMultiplier();
  });

  reader.construct(config.get("paneLayout"), this);

  m_controllerMouseEnabledButton = fetchChild<ButtonWidget>("controllerMouseEnabledCheckbox");
  m_inventoryBuildFromInventoryButton = fetchChild<ButtonWidget>("inventoryBuildFromInventoryCheckbox");
  m_controllerMouseSpeedSlider = fetchChild<SliderBarWidget>("controllerMouseSpeedSlider");
  m_controllerMouseDeadzoneSlider = fetchChild<SliderBarWidget>("controllerMouseDeadzoneSlider");
  m_gameSpeedSlider = fetchChild<SliderBarWidget>("gameSpeedSlider");
  m_incomingDamageMultiplierSlider = fetchChild<SliderBarWidget>("incomingDamageMultiplierSlider");
  m_ongoingDamageMultiplierSlider = fetchChild<SliderBarWidget>("ongoingDamageMultiplierSlider");
  m_controllerMouseSpeedLabel = fetchChild<LabelWidget>("controllerMouseSpeedValueLabel");
  m_controllerMouseDeadzoneLabel = fetchChild<LabelWidget>("controllerMouseDeadzoneValueLabel");
  m_gameSpeedLabel = fetchChild<LabelWidget>("gameSpeedValueLabel");
  m_incomingDamageMultiplierValueLabel = fetchChild<LabelWidget>("incomingDamageMultiplierValueLabel");
  m_ongoingDamageMultiplierValueLabel = fetchChild<LabelWidget>("ongoingDamageMultiplierValueLabel");

  m_controllerMouseSpeedSlider->setRange(200, 3000, 50);
  m_controllerMouseDeadzoneSlider->setRange(0, 95, 1);
  m_gameSpeedSlider->setRange(50, 150, 5);
  m_incomingDamageMultiplierSlider->setRange(50, 200, 25);
  m_ongoingDamageMultiplierSlider->setRange(50, 200, 25);

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
  "inventoryBuildFromInventory",
  "incomingDamageMultiplier",
  "ongoingDamageMultiplier"
};

void ControllerBuildMenu::updateIncomingDamageMultiplier() {
  float value = m_incomingDamageMultiplierSlider->val() / 100.0f;
  value = Star::clamp(value, 0.5f, 2.0f);
  m_localChanges["incomingDamageMultiplier"] = value;
  Root::singleton().configuration()->set("incomingDamageMultiplier", value);
  m_incomingDamageMultiplierValueLabel->setText(strf("{:.2f}x", value));
}

void ControllerBuildMenu::updateOngoingDamageMultiplier() {
  float value = m_ongoingDamageMultiplierSlider->val() / 100.0f;
  value = Star::clamp(value, 0.5f, 2.0f);
  m_localChanges["ongoingDamageMultiplier"] = value;
  Root::singleton().configuration()->set("ongoingDamageMultiplier", value);
  m_ongoingDamageMultiplierValueLabel->setText(strf("{:.2f}x", value));
}

void ControllerBuildMenu::initConfig() {
  auto configuration = Root::singleton().configuration();
  for (auto const& key : ConfigKeys)
    m_localChanges[key] = configuration->get(key);
}

void ControllerBuildMenu::syncGui() {
  bool enabled = m_localChanges.get("controllerMouseEnabled").optBool().value(true);
  int speed = (int)std::round(m_localChanges.get("controllerMouseSpeed").optFloat().value(1400.0f));
  int deadzonePercent = (int)std::round(m_localChanges.get("controllerMouseDeadzone").optFloat().value(0.20f) * 100.0f);
  int gameSpeedPercent = (int)std::round(clamp(m_localChanges.get("gameSpeed").optFloat().value(1.0f), 0.5f, 1.5f) * 100.0f);
  bool buildFromInventory = m_localChanges.get("inventoryBuildFromInventory").optBool().value(true);
  buildFromInventory = false; //removing because is quite broken right now
  float incomingMultiplier = Star::clamp(m_localChanges.get("incomingDamageMultiplier").optFloat().value(1.0f), 0.5f, 2.0f);
  float ongoingMultiplier = Star::clamp(m_localChanges.get("ongoingDamageMultiplier").optFloat().value(1.0f), 0.5f, 2.0f);
  int incomingVal = (int)std::round(incomingMultiplier * 100.0f);
  int ongoingVal = (int)std::round(ongoingMultiplier * 100.0f);

  m_controllerMouseEnabledButton->setChecked(enabled);
  m_inventoryBuildFromInventoryButton->setChecked(buildFromInventory);

  m_controllerMouseSpeedSlider->setVal(speed, false);
  m_controllerMouseSpeedLabel->setText(toString(speed));

  deadzonePercent = clamp(deadzonePercent, 0, 95);
  m_controllerMouseDeadzoneSlider->setVal(deadzonePercent, false);
  m_controllerMouseDeadzoneLabel->setText(strf("{}%", deadzonePercent));

  m_gameSpeedSlider->setVal(gameSpeedPercent, false);
  m_gameSpeedLabel->setText(strf("{:.2f}x", gameSpeedPercent / 100.0f));

  m_incomingDamageMultiplierSlider->setVal(incomingVal, false);
  m_incomingDamageMultiplierValueLabel->setText(strf("{:.2f}x", incomingVal / 100.0f));
  m_ongoingDamageMultiplierSlider->setVal(ongoingVal, false);
  m_ongoingDamageMultiplierValueLabel->setText(strf("{:.2f}x", ongoingVal / 100.0f));
}

void ControllerBuildMenu::apply() {
  auto configuration = Root::singleton().configuration();
  for (auto const& pair : m_localChanges)
    configuration->set(pair.first, pair.second);
}

void ControllerBuildMenu::updateControllerMouseEnabled() {
  m_localChanges["controllerMouseEnabled"] = m_controllerMouseEnabledButton->isChecked();
}

void ControllerBuildMenu::updateInventoryBuildFromInventory() {
  m_localChanges["inventoryBuildFromInventory"] = m_inventoryBuildFromInventoryButton->isChecked();
}

void ControllerBuildMenu::updateControllerMouseSpeed() {
  float value = m_controllerMouseSpeedSlider->val();
  m_localChanges["controllerMouseSpeed"] = value;
  m_controllerMouseSpeedLabel->setText(toString((int)std::round(value)));
}

void ControllerBuildMenu::updateControllerMouseDeadzone() {
  float value = m_controllerMouseDeadzoneSlider->val() / 100.0f;
  m_localChanges["controllerMouseDeadzone"] = value;
  m_controllerMouseDeadzoneLabel->setText(strf("{}%", (int)std::round(value * 100.0f)));
}

void ControllerBuildMenu::updateGameSpeed() {
  float value = m_gameSpeedSlider->val() / 100.0f;
  m_localChanges["gameSpeed"] = value;
  m_gameSpeedLabel->setText(strf("{:.2f}x", value));
}

}
