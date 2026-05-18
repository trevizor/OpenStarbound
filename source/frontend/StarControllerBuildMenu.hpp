#pragma once

#include "StarPane.hpp"

namespace Star {

STAR_CLASS(SliderBarWidget);
STAR_CLASS(ButtonWidget);
STAR_CLASS(LabelWidget);
STAR_CLASS(ControllerBuildMenu);

class ControllerBuildMenu : public Pane {
public:
  ControllerBuildMenu(Json const& config);

  void show() override;

private:
  static StringList const ConfigKeys;

  void initConfig();
  void syncGui();
  void apply();

  void updateControllerMouseEnabled();
  void updateInventoryBuildFromInventory();
  void updateControllerMouseSpeed();
  void updateControllerMouseDeadzone();
  void updateGameSpeed();
  void updateIncomingDamageMultiplier();
  void updateOngoingDamageMultiplier();

  SliderBarWidgetPtr m_controllerMouseSpeedSlider;
  SliderBarWidgetPtr m_controllerMouseDeadzoneSlider;
  SliderBarWidgetPtr m_gameSpeedSlider;
  SliderBarWidgetPtr m_incomingDamageMultiplierSlider;
  SliderBarWidgetPtr m_ongoingDamageMultiplierSlider;
  ButtonWidgetPtr m_controllerMouseEnabledButton;
  ButtonWidgetPtr m_inventoryBuildFromInventoryButton;
  LabelWidgetPtr m_controllerMouseSpeedLabel;
  LabelWidgetPtr m_controllerMouseDeadzoneLabel;
  LabelWidgetPtr m_gameSpeedLabel;
  LabelWidgetPtr m_incomingDamageMultiplierLabel;
  LabelWidgetPtr m_ongoingDamageMultiplierLabel;
  LabelWidgetPtr m_incomingDamageMultiplierValueLabel;
  LabelWidgetPtr m_ongoingDamageMultiplierValueLabel;

  JsonObject m_localChanges;
};

}
