#include "StarKeybindingsMenu.hpp"
#include "StarRoot.hpp"
#include "StarAssets.hpp"
#include "StarConfiguration.hpp"
#include "StarGuiReader.hpp"
#include "StarListWidget.hpp"
#include "StarLabelWidget.hpp"
#include "StarButtonWidget.hpp"
#include "StarOrderedSet.hpp"
#include "StarJsonExtra.hpp"
#include "StarTabSet.hpp"
#include "StarTime.hpp"

namespace Star {

KeybindingsMenu::KeybindingsMenu() : m_activeKeybinding(nullptr), m_clearHoldFromController(false) {
  GuiReader reader;
  reader.registerCallback("cancel",
      [&](Widget*) {
        revert();
        dismiss();
      });
  reader.registerCallback("accept",
      [&](Widget*) {
        apply();
        dismiss();
      });
  reader.registerCallback("setDefault", [&](Widget*) { resetDefaults(); });

  auto assets = Root::singleton().assets();

  m_maxBindings = assets->json("/interface/windowconfig/keybindingsmenu.config:maxBindings").toUInt();

  Json paneLayout = assets->json("/interface/windowconfig/keybindingsmenu.config:paneLayout");
  // Increase pane width by 200% if possible (using correct Star::Json API)
  if (paneLayout.type() == Json::Type::Object && paneLayout.contains("size")) {
    Json sizeArr = paneLayout.get("size");
    if (sizeArr.type() == Json::Type::Array && sizeArr.size() == 2) {
      JsonArray arr = sizeArr.toArray();
      arr[0] = Json(arr[0].toInt() * 2);
      paneLayout.set("size", arr);
    }
  }
  reader.construct(paneLayout, this);

  m_tabSet = fetchChild<TabSetWidget>("categories");

  buildListsFromConfig();

  m_currentMods = KeyMod::NoMod;
}

KeyboardCaptureMode KeybindingsMenu::keyboardCaptureMode() const {
  return m_activeKeybinding ? KeyboardCaptureMode::KeyEvents : KeyboardCaptureMode::None;
}

bool KeybindingsMenu::sendEvent(InputEvent const& event) {
  if (!m_visible)
    return false;

  if (m_activeKeybinding) {
    constexpr uint64_t ClearHoldThresholdMs = 500;

    if (auto keyDown = event.ptr<KeyDownEvent>()) {
      if (keyDown->key == Key::Escape) {
        m_clearHoldStartMs = Time::monotonicMilliseconds();
        m_clearHoldFromController = false;
        return true;
      }
    } else if (auto keyUp = event.ptr<KeyUpEvent>()) {
      if (keyUp->key == Key::Escape && m_clearHoldStartMs && !m_clearHoldFromController) {
        uint64_t heldFor = Time::monotonicMilliseconds() - *m_clearHoldStartMs;
        if (heldFor >= ClearHoldThresholdMs)
          clearActive();
        else
          exitActiveMode();
        return true;
      }
    } else if (auto controllerDown = event.ptr<ControllerButtonDownEvent>()) {
      if (controllerDown->controllerButton == ControllerButton::Start) {
        m_clearHoldStartMs = Time::monotonicMilliseconds();
        m_clearHoldFromController = true;
        return true;
      }
    } else if (auto controllerUp = event.ptr<ControllerButtonUpEvent>()) {
      if (controllerUp->controllerButton == ControllerButton::Start && m_clearHoldStartMs && m_clearHoldFromController) {
        uint64_t heldFor = Time::monotonicMilliseconds() - *m_clearHoldStartMs;
        if (heldFor >= ClearHoldThresholdMs)
          clearActive();
        else
          exitActiveMode();
        return true;
      }
    }

    if (m_clearHoldStartMs)
      return true;

    if (m_context->actions(event).contains(InterfaceAction::KeybindingClear)) {
      clearActive();
      return true;
    }

    if (m_context->actions(event).contains(InterfaceAction::KeybindingCancel)) {
      exitActiveMode();
      return true;
    }
  }

  if (m_activeKeybinding) {
    // HACK: I need to pass events only to the trash button first.
    if (m_activeKeybinding->parent()->fetchChild<ButtonWidget>("deleteBinding")->sendEvent(event))
      return true;

    if (auto keyUp = event.ptr<KeyUpEvent>()) {
      if (Maybe<KeyMod> modKey = KeyChordMods.maybe(keyUp->key)) {
        m_currentMods &= ~*modKey;
        setKeybinding(KeyChord{keyUp->key, m_currentMods});
        return true;
      }
    } else if (auto keyDown = event.ptr<KeyDownEvent>()) {
      Maybe<KeyMod> modKey = KeyModNames.maybeLeft(KeyNames.getRight(keyDown->key));

      if (modKey) {
        m_currentMods |= *modKey;
        return true;
      } else {
        setKeybinding(KeyChord{keyDown->key, m_currentMods});
        return true;
      }
    } else if (auto controllerDown = event.ptr<ControllerButtonDownEvent>()) {
      setKeybinding(ControllerButtonChord{controllerDown->controllerButton});
      return true;
    } else if (auto controllerAxis = event.ptr<ControllerAxisEvent>()) {
      if (controllerAxis->controllerAxis != ControllerAxis::Invalid
          && (controllerAxis->controllerAxisValue >= 0.5f || controllerAxis->controllerAxisValue <= -0.5f)) {
        setKeybinding(ControllerAxisChord{
            controllerAxis->controllerAxis,
            controllerAxis->controllerAxisValue >= 0.0f ? (int8_t)1 : (int8_t)-1,
            0.5f});
        return true;
      }
    }
  }

  if (!m_activeKeybinding && m_tabSet && event.is<ControllerButtonDownEvent>()) {
    if (m_context->actions(event).contains(InterfaceAction::InterfaceKeybindingsTabPrevious)) {
      selectTab(-1);
      return true;
    }
    if (m_context->actions(event).contains(InterfaceAction::InterfaceKeybindingsTabNext)) {
      selectTab(1);
      return true;
    }
  }

  if (m_context->actions(event).contains(InterfaceAction::GuiClose)) {
    dismiss();
    return true;
  }

  if (Pane::sendEvent(event))
    return true;

  return false;
}

void KeybindingsMenu::show() {
  m_origConfiguration = Root::singleton().configuration()->get("bindings");
  Pane::show();
}

void KeybindingsMenu::dismissed() {
  exitActiveMode();
  Pane::dismissed();
}

void KeybindingsMenu::buildListsFromConfig() {
  m_playerList = fetchChild<ListWidget>("categories.tabs.player.scrollArea.keyList");
  m_toolBarList = fetchChild<ListWidget>("categories.tabs.toolbar.scrollArea.keyList");
  m_gameList = fetchChild<ListWidget>("categories.tabs.game.scrollArea.keyList");

  m_childToAction.clear();

  auto doKeybindingsFor = [&](ListWidgetPtr const& list, Json const& keybinds) {
    list->clear();

    list->registerMemberCallback("activateBinding", [this](Widget* widget) { activateBinding(widget); });

    list->registerMemberCallback("deleteBinding", [this](Widget*) { clearActive(); });

    auto config = Root::singleton().configuration();
    auto bindings = config->get("bindings");

    for (auto const& keybind : keybinds.iterateArray()) {
      auto newListMember = list->addItem();
      auto actionString = keybind.get("action").toString();
      auto action = InterfaceActionNames.getLeft(actionString);
      List<InputDescriptor> inputDesc;
      try {
        for (auto const& bindingEntry : bindings.get(actionString).iterateArray())
          inputDesc.append(inputDescriptorFromJson(bindingEntry));
      } catch (StarException const& e) {
        Logger::warn("Could not load keybinding for {}. {}\n", actionString, e.what());
      }

      auto boundKeysButton = newListMember->fetchChild<ButtonWidget>("boundKeys");
      auto deleteButton = newListMember->fetchChild<ButtonWidget>("deleteBinding");

      // Give more room for long controller/axis chord labels in the rebind field.
      Vec2I boundSize = boundKeysButton->size();
      int widthDelta = 72 * 2; // 200% wider
      boundSize[0] += widthDelta;
      boundKeysButton->setSize(boundSize);

      Vec2I deletePos = deleteButton->position();
      deletePos[0] += widthDelta;
      deleteButton->setPosition(deletePos);

      m_childToAction.insert({boundKeysButton.get(), action});
      newListMember->fetchChild<LabelWidget>("actionName")->setText(keybind.getString("label"));
      boundKeysButton->setText(StringList(inputDesc.transformed(printInputDescriptor)).join(", "));
      deleteButton->hide();
    }
  };

  auto assets = Root::singleton().assets();
  Json playerActions = assets->json("/interface/windowconfig/keybindingsmenu.config:keyActions.player");
  Json toolbarActions = assets->json("/interface/windowconfig/keybindingsmenu.config:keyActions.toolbar");
  Json gameActions = assets->json("/interface/windowconfig/keybindingsmenu.config:keyActions.game");

  Set<String> configuredActions;
  auto collectConfiguredActions = [&configuredActions](Json const& keybinds) {
    for (auto const& keybind : keybinds.iterateArray())
      configuredActions.add(keybind.getString("action"));
  };

  collectConfiguredActions(playerActions);
  collectConfiguredActions(toolbarActions);
  collectConfiguredActions(gameActions);

  JsonArray mergedGameActions = gameActions.toArray();

  // Include any enum actions not explicitly listed in the keybindings config.
  for (int actionValue = (int)InterfaceAction::PlayerUp; actionValue <= (int)InterfaceAction::InterfaceCrafting; ++actionValue) {
    auto action = (InterfaceAction)actionValue;
    if (action == InterfaceAction::None)
      continue;

    String actionName;
    try {
      actionName = InterfaceActionNames.getRight(action);
    } catch (std::exception const&) {
      continue;
    }

    if (configuredActions.contains(actionName))
      continue;

    mergedGameActions.append(JsonObject{{"action", actionName}, {"label", actionName}});
    configuredActions.add(actionName);
  }

  for (auto const& bindingPair : Root::singleton().configuration()->get("bindings").iterateObject()) {
    if (configuredActions.contains(bindingPair.first))
      continue;

    try {
      auto action = InterfaceActionNames.getLeft(bindingPair.first);
      if (action == InterfaceAction::None)
        continue;

      mergedGameActions.append(JsonObject{{"action", bindingPair.first}, {"label", bindingPair.first}});
      configuredActions.add(bindingPair.first);
    } catch (std::exception const&) {
      // Ignore unknown custom bindings we cannot map to an InterfaceAction.
    }
  }

  doKeybindingsFor(m_playerList, playerActions);
  doKeybindingsFor(m_toolBarList, toolbarActions);
  doKeybindingsFor(m_gameList, mergedGameActions);
}

void KeybindingsMenu::selectTab(int delta) {
  if (!m_tabSet || m_tabSet->tabCount() == 0)
    return;

  int currentTab = (int)m_tabSet->selectedTab();
  if (currentTab < 0)
    currentTab = 0;

  int nextTab = currentTab + delta;
  int tabCount = (int)m_tabSet->tabCount();
  if (nextTab < 0)
    nextTab = tabCount - 1;
  else if (nextTab >= tabCount)
    nextTab = 0;

  m_tabSet->tabSelect((size_t)nextTab);
}

bool KeybindingsMenu::activateBinding(Widget* widget) {
  exitActiveMode();

  m_activeKeybinding = widget;
  m_activeKeybinding->parent()->fetchChild<ButtonWidget>("deleteBinding")->show();
  convert<ButtonWidget>(m_activeKeybinding)->setHighlighted(true);

  return false;
}

void KeybindingsMenu::setKeybinding(InputDescriptor const& desc) {
  if (!m_activeKeybinding)
    return;

  auto out = inputDescriptorToJson(desc);

  auto config = Root::singleton().configuration();
  auto base = config->get("bindings");

  auto action = m_childToAction.get(m_activeKeybinding);
  auto key = InterfaceActionNames.getRight(action);

  auto bindings = OrderedHashSet<Json>::from(base.get(key).toArray());

  if (bindings.contains(out))
    bindings.clear();

  bindings.add(out);

  if (bindings.size() > m_maxBindings)
    bindings.removeFirst();

  base = base.set(key, JsonArray::from(bindings));

  config->set("bindings", base);

  StringList buttonText;

  for (auto const& entry : base.get(key).iterateArray()) {
    try {
      auto stored = inputDescriptorFromJson(entry);
      buttonText.push_back(printInputDescriptor(stored));
    } catch (StarException const& e) {
      buttonText.push_back("unknown");
    }
  }

  convert<ButtonWidget>(m_activeKeybinding)->setText(buttonText.join(", "));

  apply();
  exitActiveMode();
}

void KeybindingsMenu::clearActive() {
  if (!m_activeKeybinding)
    return;

  auto config = Root::singleton().configuration();
  auto base = config->get("bindings").toObject();

  auto action = m_childToAction.get(m_activeKeybinding);
  auto key = InterfaceActionNames.getRight(action);

  base[key] = JsonArray{};
  config->set("bindings", base);

  convert<ButtonWidget>(m_activeKeybinding)->setText("<Unbound>");

  apply();
  exitActiveMode();
}

void KeybindingsMenu::exitActiveMode() {
  if (!m_activeKeybinding)
    return;

  m_activeKeybinding->parent()->fetchChild<ButtonWidget>("deleteBinding")->hide();
  convert<ButtonWidget>(m_activeKeybinding)->setHighlighted(false);
  m_activeKeybinding = nullptr;
  m_currentMods = KeyMod::NoMod;
  m_clearHoldStartMs.reset();
  m_clearHoldFromController = false;
}

void KeybindingsMenu::apply() {
  m_context->refreshKeybindings();
}

void KeybindingsMenu::revert() {
  Root::singleton().configuration()->set("bindings", m_origConfiguration);
  apply();

  buildListsFromConfig();
}

void KeybindingsMenu::resetDefaults() {
  auto config = Root::singleton().configuration();
  config->set("bindings", config->getDefault("bindings"));
  apply();

  buildListsFromConfig();
}

}
