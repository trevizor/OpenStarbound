#include "StarKeyBindings.hpp"
#include "StarRoot.hpp"
#include "StarConfiguration.hpp"
#include "StarLogging.hpp"
#include "StarJsonExtra.hpp"

#include <bitset>

namespace Star {

HashMap<Key, KeyMod> const KeyChordMods{
  {Key::LShift, KeyMod::LShift},
  {Key::RShift, KeyMod::RShift},
  {Key::LCtrl, KeyMod::LCtrl},
  {Key::RCtrl, KeyMod::RCtrl},
  {Key::LAlt, KeyMod::LAlt},
  {Key::RAlt, KeyMod::RAlt},
  {Key::LGui, KeyMod::LGui},
  {Key::RGui, KeyMod::RGui},
  {Key::AltGr, KeyMod::AltGr}
};

EnumMap<InterfaceAction> const InterfaceActionNames{
    {InterfaceAction::None, "None"},
    {InterfaceAction::PlayerUp, "PlayerUp"},
    {InterfaceAction::PlayerDown, "PlayerDown"},
    {InterfaceAction::PlayerLeft, "PlayerLeft"},
    {InterfaceAction::PlayerRight, "PlayerRight"},
    {InterfaceAction::PlayerJump, "PlayerJump"},
    {InterfaceAction::PlayerMainItem, "PlayerMainItem"},
    {InterfaceAction::PlayerAltItem, "PlayerAltItem"},
    {InterfaceAction::PlayerDropItem, "PlayerDropItem"},
    {InterfaceAction::PlayerInteract, "PlayerInteract"},
    {InterfaceAction::PlayerShifting, "PlayerShifting"},
    {InterfaceAction::PlayerTechAction1, "PlayerTechAction1"},
    {InterfaceAction::PlayerTechAction2, "PlayerTechAction2"},
    {InterfaceAction::PlayerTechAction3, "PlayerTechAction3"},
    {InterfaceAction::PlayerControllerAimOnly, "PlayerControllerAimOnly"},
    {InterfaceAction::PlayerControllerMoveOnly, "PlayerControllerMoveOnly"},
    {InterfaceAction::EmoteBlabbering, "EmoteBlabbering"},
    {InterfaceAction::EmoteShouting, "EmoteShouting"},
    {InterfaceAction::EmoteHappy, "EmoteHappy"},
    {InterfaceAction::EmoteSad, "EmoteSad"},
    {InterfaceAction::EmoteNeutral, "EmoteNeutral"},
    {InterfaceAction::EmoteLaugh, "EmoteLaugh"},
    {InterfaceAction::EmoteAnnoyed, "EmoteAnnoyed"},
    {InterfaceAction::EmoteOh, "EmoteOh"},
    {InterfaceAction::EmoteOooh, "EmoteOooh"},
    {InterfaceAction::EmoteBlink, "EmoteBlink"},
    {InterfaceAction::EmoteWink, "EmoteWink"},
    {InterfaceAction::EmoteEat, "EmoteEat"},
    {InterfaceAction::EmoteSleep, "EmoteSleep"},
    {InterfaceAction::ShowLabels, "ShowLabels"},
    {InterfaceAction::CameraShift, "CameraShift"},
    {InterfaceAction::TitleBack, "TitleBack"},
    {InterfaceAction::CinematicSkip, "CinematicSkip"},
    {InterfaceAction::CinematicNext, "CinematicNext"},
    {InterfaceAction::GuiClose, "GuiClose"},
    {InterfaceAction::InterfaceToggleControllerMouse, "InterfaceToggleControllerMouse"},
    {InterfaceAction::InterfacePanelClose, "InterfacePanelClose"},
    {InterfaceAction::GuiShifting, "GuiShifting"},
    {InterfaceAction::KeybindingClear, "KeybindingClear"},
    {InterfaceAction::KeybindingCancel, "KeybindingCancel"},
    {InterfaceAction::ChatPageUp, "ChatPageUp"},
    {InterfaceAction::ChatPageDown, "ChatPageDown"},
    {InterfaceAction::ChatPreviousLine, "ChatPreviousLine"},
    {InterfaceAction::ChatNextLine, "ChatNextLine"},
    {InterfaceAction::ChatSendLine, "ChatSendLine"},
    {InterfaceAction::ChatBegin, "ChatBegin"},
    {InterfaceAction::ChatBeginCommand, "ChatBeginCommand"},
    {InterfaceAction::ChatStop, "ChatStop"},
    {InterfaceAction::InterfaceShowHelp, "InterfaceShowHelp"},
    {InterfaceAction::InterfaceHideHud, "InterfaceHideHud"},
    {InterfaceAction::InterfaceChangeBarGroup, "InterfaceChangeBarGroup"},
    {InterfaceAction::InterfaceHotbarWheelHold, "InterfaceHotbarWheelHold"},
    {InterfaceAction::InterfaceHotbarStripHold, "InterfaceHotbarStripHold"},
    {InterfaceAction::InterfacePanelWheelHold, "InterfacePanelWheelHold"},
    {InterfaceAction::InterfacePanelSelect, "InterfacePanelSelect"},
    {InterfaceAction::InterfacePanelBack, "InterfacePanelBack"},
    {InterfaceAction::InterfacePanelCursorCenter, "InterfacePanelCursorCenter"},
    {InterfaceAction::InterfacePanelCursorLeft, "InterfacePanelCursorLeft"},
    {InterfaceAction::InterfacePanelCursorRight, "InterfacePanelCursorRight"},
    {InterfaceAction::InterfacePanelCursorUp, "InterfacePanelCursorUp"},
    {InterfaceAction::InterfacePanelCursorDown, "InterfacePanelCursorDown"},
    {InterfaceAction::InterfacePanelScrollUp, "InterfacePanelScrollUp"},
    {InterfaceAction::InterfacePanelScrollDown, "InterfacePanelScrollDown"},
    {InterfaceAction::InterfacePanelDragHold, "InterfacePanelDragHold"},
    {InterfaceAction::InterfaceControllerMouseLeft, "InterfaceControllerMouseLeft"},
    {InterfaceAction::InterfaceControllerMouseRight, "InterfaceControllerMouseRight"},
    {InterfaceAction::InterfaceKeybindingsTabPrevious, "InterfaceKeybindingsTabPrevious"},
    {InterfaceAction::InterfaceKeybindingsTabNext, "InterfaceKeybindingsTabNext"},
    {InterfaceAction::InterfaceTakeAllItems, "InterfaceTakeAllItems"},
    {InterfaceAction::InterfacePlaceTorchAtCursor, "InterfacePlaceTorchAtCursor"},
    {InterfaceAction::InterfaceUseFirstHealingItem, "InterfaceUseFirstHealingItem"},
    {InterfaceAction::InterfaceDeselectHands, "InterfaceDeselectHands"},
    {InterfaceAction::InterfaceBarPrevious, "InterfaceBarPrevious"},
    {InterfaceAction::InterfaceBarNext, "InterfaceBarNext"},
    {InterfaceAction::InterfaceBar1, "InterfaceBar1"},
    {InterfaceAction::InterfaceBar2, "InterfaceBar2"},
    {InterfaceAction::InterfaceBar3, "InterfaceBar3"},
    {InterfaceAction::InterfaceBar4, "InterfaceBar4"},
    {InterfaceAction::InterfaceBar5, "InterfaceBar5"},
    {InterfaceAction::InterfaceBar6, "InterfaceBar6"},
    {InterfaceAction::InterfaceBar7, "InterfaceBar7"},
    {InterfaceAction::InterfaceBar8, "InterfaceBar8"},
    {InterfaceAction::InterfaceBar9, "InterfaceBar9"},
    {InterfaceAction::InterfaceBar10, "InterfaceBar10"},
    {InterfaceAction::EssentialBar1, "EssentialBar1"},
    {InterfaceAction::EssentialBar2, "EssentialBar2"},
    {InterfaceAction::EssentialBar3, "EssentialBar3"},
    {InterfaceAction::EssentialBar4, "EssentialBar4"},
    {InterfaceAction::InterfaceRepeatCommand, "InterfaceRepeatCommand"},
    {InterfaceAction::InterfaceToggleFullscreen, "InterfaceToggleFullscreen"},
    {InterfaceAction::InterfaceReload, "InterfaceReload"},
    {InterfaceAction::InterfaceEscapeMenu, "InterfaceEscapeMenu"},
    {InterfaceAction::InterfaceInventory, "InterfaceInventory"},
    {InterfaceAction::InterfaceCodex, "InterfaceCodex"},
    {InterfaceAction::InterfaceQuest, "InterfaceQuest"},
    {InterfaceAction::InterfaceCrafting, "InterfaceCrafting"},
};

bool KeyChord::operator<(KeyChord const& rhs) const {
  return tie(key, mods) < tie(rhs.key, rhs.mods);
}

static bool axisChordTriggered(ControllerAxisChord const& chord, float value) {
  float signedValue = value * (float)chord.direction;
  return signedValue >= chord.threshold;
}

static size_t keyChordModCount(KeyMod mods) {
  size_t matchedMods = 0;
  for (auto modPair : KeyChordMods) {
    if ((modPair.second & mods) == modPair.second)
      ++matchedMods;
  }
  return matchedMods;
}

InputDescriptor inputDescriptorFromJson(Json const& json) {
  auto type = json.getString("type");
  if (type == "key") {
    Key key;
    auto value = json.get("value");
    if (value.isType(Json::Type::String)) {
      key = KeyNames.getLeft(value.toString());
    } else if (value.canConvert(Json::Type::Int)) {
      key = (Key)value.toUInt();
    } else {
      throw StarException::format("Improper key value '{}'", value);
    }

    KeyMod mods = KeyMod::NoMod;
    for (auto mod : json.get("mods").iterateArray())
      mods |= KeyModNames.getLeft(mod.toString());

    return KeyChord{key, mods};
  } else if (type == "controller" || type == "controllerButton") {
    auto value = json.get("value");
    if (!value.isType(Json::Type::String))
      throw StarException::format("Improper controller button value '{}'", value);

    return ControllerButtonChord{ControllerButtonNames.getLeft(value.toString())};
  } else if (type == "controllerAxis") {
    auto value = json.get("value");
    if (!value.isType(Json::Type::String))
      throw StarException::format("Improper controller axis value '{}'", value);

    auto axis = ControllerAxisNames.getLeft(value.toString());
    int8_t direction = static_cast<int8_t>(json.getInt("direction", 1));
    if (direction == 0)
      direction = 1;
    direction = direction > 0 ? 1 : -1;

    float threshold = json.getFloat("threshold", 0.5f);
    threshold = clamp(threshold, 0.01f, 1.0f);

    return ControllerAxisChord{axis, direction, threshold};
  } else {
    throw StarException::format("Improper bindings type '{}'", type);
  }
}

Json inputDescriptorToJson(InputDescriptor const& descriptor) {
  if (auto chord = descriptor.ptr<KeyChord>()) {
    JsonArray modNames;
    for (auto const& p : KeyModNames) {
      if ((chord->mods & p.first) != KeyMod::NoMod)
        modNames.append(p.second);
    }
    return JsonObject{
      {"type", "key"},
      {"value", KeyNames.getRight(chord->key)},
      {"mods", modNames}
    };
  }

  if (auto button = descriptor.ptr<ControllerButtonChord>()) {
    return JsonObject{
      {"type", "controller"},
      {"value", ControllerButtonNames.getRight(button->button)}
    };
  }

  auto axis = descriptor.get<ControllerAxisChord>();
  return JsonObject{
    {"type", "controllerAxis"},
    {"value", ControllerAxisNames.getRight(axis.axis)},
    {"direction", axis.direction},
    {"threshold", axis.threshold}
  };
}

String printInputDescriptor(InputDescriptor const& descriptor) {
  if (auto chord = descriptor.ptr<KeyChord>()) {
    StringList modNames;
    for (auto const& p : KeyModNames) {
      if ((chord->mods & p.first) != KeyMod::NoMod)
        modNames.append(p.second);
    }

    return String::joinWith(" + ", modNames.join(" + "), KeyNames.getRight(chord->key));
  }

  if (auto button = descriptor.ptr<ControllerButtonChord>())
    return strf("Controller {}", ControllerButtonNames.getRight(button->button));

  auto axis = descriptor.get<ControllerAxisChord>();
  return strf("Controller {} {}>{:0.2f}",
      ControllerAxisNames.getRight(axis.axis),
      axis.direction > 0 ? "+" : "-",
      axis.threshold);
}

KeyBindings::KeyBindings() {}

KeyBindings::KeyBindings(Json const& json) {
  Map<Key, List<pair<KeyMod, InterfaceAction>>> actions;
  Map<ControllerButton, List<InterfaceAction>> controllerButtonActions;
  List<pair<ControllerAxisChord, InterfaceAction>> controllerAxisActions;
  try {
    for (auto const& kvpair : json.iterateObject()) {
      InterfaceAction action = InterfaceActionNames.getLeft(kvpair.first);

      for (auto const& input : kvpair.second.iterateArray()) {
        try {
          auto descriptor = inputDescriptorFromJson(input);
          if (auto chord = descriptor.ptr<KeyChord>()) {
            actions[chord->key].append({chord->mods, action});
          } else if (auto button = descriptor.ptr<ControllerButtonChord>()) {
            controllerButtonActions[button->button].append(action);
          } else {
            controllerAxisActions.append({descriptor.get<ControllerAxisChord>(), action});
          }
        } catch (StarException const& e) {
          Logger::warn("Could not load keybinding for {}: {}\n",
              InterfaceActionNames.getRight(action),
              outputException(e, false));
        }
      }
    }

    m_actions = std::move(actions);
    m_controllerButtonActions = std::move(controllerButtonActions);
    m_controllerAxisActions = std::move(controllerAxisActions);
  } catch (StarException const& e) {
    throw StarException(strf("Could not set keybindings from configuration. {}", outputException(e, false)));
  }
}

Set<InterfaceAction> KeyBindings::actions(Key key) const {
  return actions(KeyChord{key, KeyMod::NoMod});
}

Set<InterfaceAction> KeyBindings::actions(InputEvent const& event) const {
  if (auto keyDown = event.ptr<KeyDownEvent>())
    return actions(KeyChord{keyDown->key, keyDown->mods});
  if (auto controllerDown = event.ptr<ControllerButtonDownEvent>())
    return actions(controllerDown->controllerButton);
  if (auto controllerAxis = event.ptr<ControllerAxisEvent>())
    return actions(controllerAxis->controllerAxis, controllerAxis->controllerAxisValue);
  return {};
}

Set<InterfaceAction> KeyBindings::actions(KeyChord chord) const {
  size_t mostMatchedMods = 0;
  Set<InterfaceAction> matching;
  for (auto const& pair : m_actions.value(chord.key)) {
    // first make sure that all required mods for the binding are held
    if ((pair.first & chord.mods) == pair.first) {
      // now count the number of mods in the binding
      size_t matchedMods = keyChordModCount(pair.first);

      if (matchedMods > mostMatchedMods) {
        matching.clear();
        mostMatchedMods = matchedMods;
      }

      // only activate the binding(s) with the most mods
      if (matchedMods == mostMatchedMods)
        matching.add(pair.second);
    }
  }
  return matching;
}

Set<InterfaceAction> KeyBindings::actions(ControllerButton button) const {
  return Set<InterfaceAction>::from(m_controllerButtonActions.value(button));
}

Set<InterfaceAction> KeyBindings::actions(ControllerAxis axis, float value) const {
  Set<InterfaceAction> matching;
  for (auto const& axisAction : m_controllerAxisActions) {
    if (axisAction.first.axis == axis && axisChordTriggered(axisAction.first, value))
      matching.add(axisAction.second);
  }
  return matching;
}

Set<InterfaceAction> KeyBindings::actionsForKey(Key key) const {
  return Set<InterfaceAction>::from(m_actions.value(key).transformed([](auto p){ return p.second; }));
}

}