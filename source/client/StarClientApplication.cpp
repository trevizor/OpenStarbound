#include "StarClientApplication.hpp"
#include "StarConfiguration.hpp"
#include "StarJsonExtra.hpp"
#include "StarFile.hpp"
#include "StarEncode.hpp"
#include "StarLogging.hpp"
#include "StarJsonExtra.hpp"
#include "StarRoot.hpp"
#include "StarVersion.hpp"
#include "StarPlayer.hpp"
#include "StarPlayerInventory.hpp"
#include "StarPlayerStorage.hpp"
#include "StarPlayerLog.hpp"
#include "StarContainerInterface.hpp"
#include "StarAssets.hpp"
#include "StarWorldTemplate.hpp"
#include "StarWorldClient.hpp"
#include "StarRootLoader.hpp"
#include "StarInput.hpp"
#include "StarListWidget.hpp"
#include "StarItemSlotWidget.hpp"
#include "StarVoice.hpp"
#include "StarCurve25519.hpp"
#include "StarInterpolation.hpp"

#include "StarCameraLuaBindings.hpp"
#include "StarCelestialLuaBindings.hpp"
#include "StarClipboardLuaBindings.hpp"
#include "StarInputLuaBindings.hpp"
#include "StarInterfaceLuaBindings.hpp"
#include "StarLuaHttpBindings.hpp"
#include "StarRenderingLuaBindings.hpp"
#include "StarTeamClientLuaBindings.hpp"
#include "StarVoiceLuaBindings.hpp"
#include "StarHttpTrustDialog.hpp"
#include "StarMainInterfaceTypes.hpp"

#include "imgui.h"
#include "imgui_freetype.h"

#include <cmath>

#if defined STAR_SYSTEM_WINDOWS
#include <windows.h>
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;
extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
#endif // graphics driver is told by these exports to default to the dedicated GPU

namespace Star {

static float applyControllerAxisResponse(float value, float deadzone, float sensitivity) {
  deadzone = clamp(deadzone, 0.0f, 0.95f);
  sensitivity = max(0.1f, sensitivity);

  float magnitude = std::abs(value);
  if (magnitude <= deadzone)
    return 0.0f;

  float normalized = (magnitude - deadzone) / (1.0f - deadzone);
  float curved = std::pow(clamp(normalized, 0.0f, 1.0f), sensitivity);
  return std::copysign(clamp(curved, 0.0f, 1.0f), value);
}

static bool stickAxisActive(Vec2F const& stick, float threshold = 0.0f) {
  return std::abs(stick[0]) > threshold || std::abs(stick[1]) > threshold;
}

static int hotbarWheelSlotCount(PlayerInventoryPtr const& inventory) {
  return inventory ? (int)inventory->customBarIndexes() + EssentialItemCount : 0;
}

static SelectedActionBarLocation hotbarWheelSelectionFromIndex(PlayerInventoryPtr const& inventory, int index) {
  if (!inventory)
    return {};

  int customBarCount = inventory->customBarIndexes();
  int slotCount = customBarCount + EssentialItemCount;
  if (slotCount <= 0)
    return {};

  index = pmod(index, slotCount);
  if (index < customBarCount / 2)
    return SelectedActionBarLocation((CustomBarIndex)index);
  if (index < customBarCount / 2 + EssentialItemCount)
    return SelectedActionBarLocation((EssentialItem)(index - customBarCount / 2));
  return SelectedActionBarLocation((CustomBarIndex)(index - EssentialItemCount));
}

static ItemPtr hotbarWheelItemForSelection(PlayerInventoryPtr const& inventory, SelectedActionBarLocation const& selection) {
  if (!inventory)
    return {};

  if (selection.is<CustomBarIndex>()) {
    if (auto slot = inventory->customBarPrimarySlot(selection.get<CustomBarIndex>()))
      return inventory->itemsAt(*slot);
    return {};
  }

  if (selection.is<EssentialItem>())
    return inventory->essentialItem(selection.get<EssentialItem>());

  return {};
}

static ClientApplication::PanelWheelOption panelWheelOptionFromIndex(int index) {
  static List<ClientApplication::PanelWheelOption> options = {
    ClientApplication::PanelWheelOption::Inventory,
    ClientApplication::PanelWheelOption::Crafting,
    ClientApplication::PanelWheelOption::Codex,
    ClientApplication::PanelWheelOption::QuestLog,
    ClientApplication::PanelWheelOption::MmUpgrade,
    ClientApplication::PanelWheelOption::Collections,
    ClientApplication::PanelWheelOption::EscapeMenu
  };

  return options.at(pmod(index, (int)options.size()));
}

static List<ClientApplication::PanelWheelOption> const& panelWheelOptions() {
  static List<ClientApplication::PanelWheelOption> options = {
    ClientApplication::PanelWheelOption::Inventory,
    ClientApplication::PanelWheelOption::Crafting,
    ClientApplication::PanelWheelOption::Codex,
    ClientApplication::PanelWheelOption::QuestLog,
    ClientApplication::PanelWheelOption::MmUpgrade,
    ClientApplication::PanelWheelOption::Collections,
    ClientApplication::PanelWheelOption::EscapeMenu
  };
  return options;
}

static String panelWheelOptionLabel(ClientApplication::PanelWheelOption option) {
  switch (option) {
    case ClientApplication::PanelWheelOption::Inventory:
      return "Inventory";
    case ClientApplication::PanelWheelOption::Crafting:
      return "Craft";
    case ClientApplication::PanelWheelOption::Codex:
      return "Codex";
    case ClientApplication::PanelWheelOption::QuestLog:
      return "Quest";
    case ClientApplication::PanelWheelOption::MmUpgrade:
      return "Upgrade";
    case ClientApplication::PanelWheelOption::Collections:
      return "Collections";
    case ClientApplication::PanelWheelOption::EscapeMenu:
      return "Menu";
  }

  return "";
}

static void ensureBindingActionDefault(ConfigurationPtr const& configuration, InterfaceAction action) {
  auto actionName = InterfaceActionNames.getRight(action);

  bool hasAction = false;
  for (auto const& bindingPair : configuration->get("bindings").iterateObject()) {
    if (bindingPair.first == actionName) {
      hasAction = true;
      break;
    }
  }

  if (!hasAction)
    configuration->setPath(strf("bindings.{}", actionName), configuration->getDefault("bindings").get(actionName));
}

Json const AdditionalAssetsSettings = Json::parseJson(R"JSON(
    {
      "missingImage" : "/assetmissing.png",
      "missingAudio" : "/assetmissing.wav"
    }
  )JSON");

Json const AdditionalDefaultConfiguration = Json::parseJson(R"JSON(
    {
      "configurationVersion" : {
        "client" : 8
      },

      "allowAssetsMismatch" : false,
      "vsync" : true,
      "limitTextureAtlasSize" : false,
      "useMultiTexturing" : true,
      "audioChannelSeparation" : [-25, 25],

      "sfxVol" : 100,
      "instrumentVol" : 100,
      "musicVol" : 70,
      "hardwareCursor" : true,
      "windowedResolution" : [1000, 600],
      "fullscreenResolution" : [1920, 1080],
      "fullscreen" : false,
      "borderless" : false,
      "maximized" : true,
      "antiAliasing" : false,
      "zoomLevel" : 3.0,
      "cameraSpeedFactor" : 1.0,
      "interfaceScale" : 0,
      "speechBubbles" : true,
      "controllerInput" : true,
      "controllerAxisDeadzone" : 0.20,
      "controllerAxisSensitivity" : 1.0,
      "controllerMouseEnabled" : true,
      "controllerMouseSpeed" : 1400.0,
      "controllerMouseDeadzone" : 0.20,

      "title" : {
        "multiPlayerAddress" : "",
        "multiPlayerPort" : "",
        "multiPlayerAccount" : "",
        "multiPlayerForceLegacy" : false
      },

      "bindings" : {
        "PlayerUp" :  [ { "type" : "key", "value" : "W", "mods" : [] }, { "type" : "controllerAxis", "value" : "LeftY", "direction" : -1, "threshold" : 0.35 } ],
        "PlayerDown" :  [ { "type" : "key", "value" : "S", "mods" : [] }, { "type" : "controllerAxis", "value" : "LeftY", "direction" : 1, "threshold" : 0.35 } ],
        "PlayerLeft" :  [ { "type" : "key", "value" : "A", "mods" : [] }, { "type" : "controllerAxis", "value" : "LeftX", "direction" : -1, "threshold" : 0.35 } ],
        "PlayerRight" :  [ { "type" : "key", "value" : "D", "mods" : [] }, { "type" : "controllerAxis", "value" : "LeftX", "direction" : 1, "threshold" : 0.35 } ],
        "PlayerJump" :  [ { "type" : "key", "value" : "Space", "mods" : [] }, { "type" : "controller", "value" : "A" } ],
        "PlayerDropItem" :  [ { "type" : "key", "value" : "Q", "mods" : [] } ],
        "PlayerInteract" :  [ { "type" : "key", "value" : "E", "mods" : [] }, { "type" : "controller", "value" : "B" } ],
        "PlayerShifting" :  [ { "type" : "key", "value" : "RShift", "mods" : [] }, { "type" : "key", "value" : "LShift", "mods" : [] }, { "type" : "controller", "value" : "LeftShoulder" } ],
        "PlayerTechAction1" :  [ { "type" : "key", "value" : "F", "mods" : [] } ],
        "PlayerTechAction2" :  [],
        "PlayerTechAction3" :  [],
        "PlayerControllerAimOnly" : [ { "type" : "controllerAxis", "value" : "TriggerLeft", "direction" : 1, "threshold" : 0.4 } ],
        "PlayerControllerMoveOnly" : [ { "type" : "controllerAxis", "value" : "TriggerRight", "direction" : 1, "threshold" : 0.4 } ],
        "EmoteBlabbering" :  [ { "type" : "key", "value" : "Right", "mods" : ["LCtrl", "LShift"] } ],
        "EmoteShouting" :  [ { "type" : "key", "value" : "Up", "mods" : ["LCtrl", "LAlt"] } ],
        "EmoteHappy" :  [ { "type" : "key", "value" : "Up", "mods" : [] } ],
        "EmoteSad" :  [ { "type" : "key", "value" : "Down", "mods" : [] } ],
        "EmoteNeutral" :  [ { "type" : "key", "value" : "Left", "mods" : [] } ],
        "EmoteLaugh" :  [ { "type" : "key", "value" : "Left", "mods" : [ "LCtrl" ] } ],
        "EmoteAnnoyed" :  [ { "type" : "key", "value" : "Right", "mods" : [] } ],
        "EmoteOh" :  [ { "type" : "key", "value" : "Right", "mods" : [ "LCtrl" ] } ],
        "EmoteOooh" :  [ { "type" : "key", "value" : "Down", "mods" : [ "LCtrl" ] } ],
        "EmoteBlink" :  [ { "type" : "key", "value" : "Up", "mods" : [ "LCtrl" ] } ],
        "EmoteWink" :  [ { "type" : "key", "value" : "Up", "mods" : ["LCtrl", "LShift"] } ],
        "EmoteEat" :  [ { "type" : "key", "value" : "Down", "mods" : ["LCtrl", "LShift"] } ],
        "EmoteSleep" :  [ { "type" : "key", "value" : "Left", "mods" : ["LCtrl", "LShift"] } ],
        "ShowLabels" :  [ { "type" : "key", "value" : "RAlt", "mods" : [] }, { "type" : "key", "value" : "LAlt", "mods" : [] } ],
        "CameraShift" :  [ { "type" : "key", "value" : "RCtrl", "mods" : [] }, { "type" : "key", "value" : "LCtrl", "mods" : [] } ],
        "TitleBack" :  [ { "type" : "key", "value" : "Esc", "mods" : [] }, { "type" : "controller", "value" : "Back" } ],
        "CinematicSkip" :  [ { "type" : "key", "value" : "Esc", "mods" : [] }, { "type" : "controller", "value" : "Back" } ],
        "CinematicNext" :  [ { "type" : "key", "value" : "Right", "mods" : [] }, { "type" : "key", "value" : "Return", "mods" : [] }, { "type" : "controller", "value" : "A" } ],
        "GuiClose" :  [ { "type" : "key", "value" : "Esc", "mods" : [] }, { "type" : "controller", "value" : "Back" } ],
        "InterfaceToggleControllerMouse" :  [ { "type" : "controller", "value" : "RightStick" } ],
        "InterfacePanelClose" :  [ { "type" : "controller", "value" : "B" } ],
        "GuiShifting" :  [ { "type" : "key", "value" : "RShift", "mods" : [] }, { "type" : "key", "value" : "LShift", "mods" : [] } ],
        "KeybindingCancel" :  [ { "type" : "key", "value" : "Esc", "mods" : [] }, { "type" : "controller", "value" : "Back" } ],
        "KeybindingClear" :  [ { "type" : "key", "value" : "Del", "mods" : [] }, { "type" : "key", "value" : "Backspace", "mods" : [] } ],
        "ChatPageUp" :  [ { "type" : "key", "value" : "PageUp", "mods" : [] } ],
        "ChatPageDown" :  [ { "type" : "key", "value" : "PageDown", "mods" : [] } ],
        "ChatPreviousLine" :  [ { "type" : "key", "value" : "Up", "mods" : [] } ],
        "ChatNextLine" :  [ { "type" : "key", "value" : "Down", "mods" : [] } ],
        "ChatSendLine" :  [ { "type" : "key", "value" : "Return", "mods" : [] } ],
        "ChatBegin" :  [ { "type" : "key", "value" : "Return", "mods" : [] } ],
        "ChatBeginCommand" :  [ { "type" : "key", "value" : "/", "mods" : [] } ],
        "ChatStop" :  [ { "type" : "key", "value" : "Esc", "mods" : [] } ],
        "InterfaceHideHud" :  [ { "type" : "key", "value" : "F1", "mods" : [] } ],
        "InterfaceChangeBarGroup" :  [ { "type" : "key", "value" : "X", "mods" : [] }, { "type" : "controller", "value" : "RightShoulder" } ],
        "InterfaceHotbarWheelHold" : [ { "type" : "controller", "value" : "DPadDown" } ],
        "InterfacePanelWheelHold" : [ { "type" : "controller", "value" : "DPadUp" } ],
        "InterfaceDeselectHands" :  [ { "type" : "key", "value" : "Z", "mods" : [] }, { "type" : "controller", "value" : "LeftShoulder" } ],
        "InterfaceBar1" :  [ { "type" : "key", "value" : "1", "mods" : [] } ],
        "InterfaceBarPrevious" :  [ { "type" : "controller", "value" : "DPadLeft" } ],
        "InterfaceBarNext" :  [ { "type" : "controller", "value" : "DPadRight" } ],
        "InterfaceBar2" :  [ { "type" : "key", "value" : "2", "mods" : [] } ],
        "InterfaceBar3" :  [ { "type" : "key", "value" : "3", "mods" : [] } ],
        "InterfaceBar4" :  [ { "type" : "key", "value" : "4", "mods" : [] } ],
        "InterfaceBar5" :  [ { "type" : "key", "value" : "5", "mods" : [] } ],
        "InterfaceBar6" :  [ { "type" : "key", "value" : "6", "mods" : [] } ],
        "InterfaceBar7" :  [],
        "InterfaceBar8" :  [],
        "InterfaceBar9" :  [],
        "InterfaceBar10" :  [],
        "EssentialBar1" :  [ { "type" : "key", "value" : "R", "mods" : [] } ],
        "EssentialBar2" :  [ { "type" : "key", "value" : "T", "mods" : [] } ],
        "EssentialBar3" :  [ { "type" : "key", "value" : "Y", "mods" : [] } ],
        "EssentialBar4" :  [ { "type" : "key", "value" : "N", "mods" : [] } ],
        "InterfaceRepeatCommand" :  [ { "type" : "key", "value" : "P", "mods" : [] } ],
        "InterfaceToggleFullscreen" :  [ { "type" : "key", "value" : "F11", "mods" : [] } ],
        "InterfaceReload" :  [],
        "InterfaceEscapeMenu" :  [ { "type" : "key", "value" : "Esc", "mods" : [] }, { "type" : "controller", "value" : "Start" } ],
        "InterfaceInventory" :  [ { "type" : "key", "value" : "I", "mods" : [] } ],
        "InterfaceCodex" :  [ { "type" : "key", "value" : "L", "mods" : [] } ],
        "InterfaceQuest" :  [ { "type" : "key", "value" : "J", "mods" : [] }, { "type" : "controller", "value" : "Guide" } ],
        "InterfaceCrafting" :  [ { "type" : "key", "value" : "C", "mods" : [] } ]
      }
    }
  )JSON");

void ClientApplication::startup(StringList const& cmdLineArgs) {
  RootLoader rootLoader({AdditionalAssetsSettings, AdditionalDefaultConfiguration, String("starbound.log"), LogLevel::Info, false, String("starbound.config")});
  m_root = rootLoader.initOrDie(cmdLineArgs).first;

  Logger::info("OpenStarbound Client v{} for v{} ({}) Source ID: {} Protocol: {}", OpenStarVersionString, StarVersionString, StarArchitectureString, StarSourceIdentifierString, StarProtocolVersion);
}

void ClientApplication::shutdown() {
  // Clear HTTP trust request callback
  LuaBindings::clearHttpTrustRequestCallback();

  m_mainInterface.reset();

  if (m_universeClient)
    m_universeClient->disconnect();

  if (m_universeServer) {
    m_universeServer->stop();
    m_universeServer->join();
    m_universeServer.reset();
  }

  if (m_statistics) {
    m_statistics->writeStatistics();
    m_statistics.reset();
  }

  m_universeClient.reset();
  m_statistics.reset();
}

void ClientApplication::applicationInit(ApplicationControllerPtr appController) {
  Application::applicationInit(appController);

  appController->setCursorVisible(true);

  auto configuration = m_root->configuration();
  ensureBindingActionDefault(configuration, InterfaceAction::InterfacePanelClose);
  ensureBindingActionDefault(configuration, InterfaceAction::InterfaceToggleControllerMouse);
  ensureBindingActionDefault(configuration, InterfaceAction::InterfaceHotbarWheelHold);
  ensureBindingActionDefault(configuration, InterfaceAction::InterfacePanelWheelHold);
  ensureBindingActionDefault(configuration, InterfaceAction::PlayerControllerAimOnly);
  ensureBindingActionDefault(configuration, InterfaceAction::PlayerControllerMoveOnly);

  bool vsync = configuration->get("vsync").toBool();
  Vec2U windowedSize = jsonToVec2U(configuration->get("windowedResolution"));
  Vec2U fullscreenSize = jsonToVec2U(configuration->get("fullscreenResolution"));
  bool fullscreen = configuration->get("fullscreen").toBool();
  bool borderless = configuration->get("borderless").toBool();
  bool maximized = configuration->get("maximized").toBool();
  m_controllerInput = configuration->get("controllerInput").optBool().value();
  
  #ifdef STAR_SYSTEM_WINDOWS
    appController->setBorderlessWorkaround(configuration->get("borderlessWorkaround", true).toBool());
  #endif

  if (fullscreen)
    appController->setFullscreenWindow(fullscreenSize);
  else if (borderless)
    appController->setBorderlessWindow();
  else if (maximized)
    appController->setMaximizedWindow();
  else
    appController->setNormalWindow(windowedSize);

  float updateRate = 1.0f / GlobalTimestep;
  if (auto jUpdateRate = configuration->get("updateRate")) {
    updateRate = jUpdateRate.toFloat();
    GlobalTimestep = 1.0f / updateRate;
  }

  if (auto jServerUpdateRate = configuration->get("serverUpdateRate"))
    ServerGlobalTimestep = 1.0f / jServerUpdateRate.toFloat();

  appController->setTargetUpdateRate(updateRate);
  appController->setVSyncEnabled(vsync);
  appController->setCursorHardware(configuration->get("hardwareCursor").optBool().value(true));

  // Must be called before anything that can invoke an asset load.
  loadMods();
  
  AudioFormat audioFormat = appController->enableAudio();
  m_mainMixer = make_shared<MainMixer>(audioFormat.sampleRate, audioFormat.channels);
  m_mainMixer->setVolume(0.5);
  
  m_worldPainter = make_shared<WorldPainter>();
  m_guiContext = make_shared<GuiContext>(m_mainMixer->mixer(), appController);
  m_input = make_shared<Input>();
  m_voice = make_shared<Voice>(appController);  

  auto assets = m_root->assets();

  {
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    m_immediateFont = *assets->bytes("/hobo.ttf");
    ImFontConfig config{};
    config.FontDataOwnedByAtlas = false;
    config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint;
    io.Fonts->AddFontFromMemoryTTF(m_immediateFont.ptr(), m_immediateFont.size(),
      16, &config, io.Fonts->GetGlyphRangesDefault());
  }

  m_minInterfaceScale = assets->json("/interface.config:minInterfaceScale").toFloat();
  m_maxInterfaceScale = assets->json("/interface.config:maxInterfaceScale").toFloat();
  m_crossoverRes = jsonToVec2F(assets->json("/interface.config:interfaceCrossoverRes"));
  
  appController->setApplicationTitle(assets->json("/client.config:windowTitle").toString());
  appController->setMaxFrameSkip(assets->json("/client.config:maxFrameSkip").toUInt());
  appController->setUpdateTrackWindow(assets->json("/client.config:updateTrackWindow").toFloat());
  
  if (auto jVoice = configuration->get("voice"))
    m_voice->loadJson(jVoice.toObject(), true);

  m_voice->init();
  m_voice->setLocalSpeaker(0);
}

void ClientApplication::renderInit(RendererPtr renderer) {
  Application::renderInit(renderer);
  renderReload();
  m_root->registerReloadListener(m_reloadListener = make_shared<CallbackListener>([this]() { renderReload(); }));

  if (m_root->configuration()->get("limitTextureAtlasSize").optBool().value(false))
    renderer->setSizeLimitEnabled(true);

  renderer->setMultiTexturingEnabled(m_root->configuration()->get("useMultiTexturing").optBool().value(true));

  m_guiContext->renderInit(renderer);

  m_cinematicOverlay = make_shared<Cinematic>();
  m_errorScreen = make_shared<ErrorScreen>();

  if (m_titleScreen)
    m_titleScreen->renderInit(renderer);
  if (m_worldPainter)
    m_worldPainter->renderInit(renderer);

  #ifdef STAR_ENABLE_STEAM_INTEGRATION
  #ifdef STAR_SYSTEM_LINUX
  if (g_steamIsFlatpak) {
    auto config = m_root->configuration();
    if (!config->get("steamFlatpakWarningShown").optBool().value()) {
      config->set("steamFlatpakWarningShown", true);
      m_errorScreen->setMessage(m_root->assets()->json("/interface.config:steamFlatpakWarning").toString());
      changeState(MainAppState::SteamFlatpakWarning);
      return;
    }
  }
  #endif
  #endif

  changeState(MainAppState::Mods);
}

void ClientApplication::windowChanged(WindowMode windowMode, Vec2U screenSize) {
  auto config = m_root->configuration();
  if (windowMode == WindowMode::Fullscreen) {
    config->set("fullscreenResolution", jsonFromVec2U(screenSize));
    config->set("fullscreen", true);
    config->set("borderless", false);
  } else if (windowMode == WindowMode::Borderless) {
    config->set("borderless", true);
    config->set("fullscreen", false);
  } else if (windowMode == WindowMode::Maximized) {
    config->set("maximized", true);
    config->set("fullscreen", false);
    config->set("borderless", false);
  } else {
    config->set("maximized", false);
    config->set("fullscreen", false);
    config->set("borderless", false);
    config->set("windowedResolution", jsonFromVec2U(screenSize));
  }
}

void ClientApplication::processInput(InputEvent const& event) {
  auto routeInputEvent = [this](InputEvent const& routedEvent) {
    bool processed = !m_errorScreen->accepted() && m_errorScreen->handleInputEvent(routedEvent);

    if (!processed) {
      if (m_state == MainAppState::Splash) {
        processed = m_cinematicOverlay->handleInputEvent(routedEvent);
      } else if (m_state == MainAppState::Title) {
        if (!(processed = m_cinematicOverlay->handleInputEvent(routedEvent)))
          processed = m_titleScreen->handleInputEvent(routedEvent);

      } else if (m_state == MainAppState::SinglePlayer || m_state == MainAppState::MultiPlayer) {
        if (!(processed = m_cinematicOverlay->handleInputEvent(routedEvent)))
          processed = m_mainInterface->handleInputEvent(routedEvent);
      }
    }

    m_input->handleInput(routedEvent, processed);
    return processed;
  };

  if (auto keyDown = event.ptr<KeyDownEvent>()) {
    m_heldKeyEvents.append(*keyDown);
    m_edgeKeyEvents.append(*keyDown);
  } else if (auto keyUp = event.ptr<KeyUpEvent>()) {
    eraseWhere(m_heldKeyEvents, [&](auto& keyEvent) {
      return keyEvent.key == keyUp->key;
    });

    Maybe<KeyMod> modKey = KeyModNames.maybeLeft(KeyNames.getRight(keyUp->key));
    if (modKey)
      m_heldKeyEvents.transform([&](auto& keyEvent) {
        return KeyDownEvent{keyEvent.key, keyEvent.mods & ~*modKey};
      });
  }
  else if (auto cDown = event.ptr<ControllerButtonDownEvent>()) {
    bool alreadyHeld = false;
    for (auto const& heldEvent : m_heldControllerButtonEvents) {
      if (heldEvent.controller == cDown->controller && heldEvent.controllerButton == cDown->controllerButton) {
        alreadyHeld = true;
        break;
      }
    }

    if (!alreadyHeld)
      m_heldControllerButtonEvents.append(*cDown);

    m_edgeControllerButtonEvents.append(*cDown);
  }
  else if (auto cUp = event.ptr<ControllerButtonUpEvent>()) {
    eraseWhere(m_heldControllerButtonEvents, [&](auto const& heldEvent) {
      return heldEvent.controller == cUp->controller && heldEvent.controllerButton == cUp->controllerButton;
    });
  }
  else if (auto cAxis = event.ptr<ControllerAxisEvent>()) {
    auto configuration = m_root->configuration();
    float axisDeadzone = configuration->get("controllerAxisDeadzone").optFloat().value(0.20f);
    float axisSensitivity = configuration->get("controllerAxisSensitivity").optFloat().value(1.0f);
    float axisValue = applyControllerAxisResponse(cAxis->controllerAxisValue, axisDeadzone, axisSensitivity);

    float oldAxisValue = m_controllerAxisValues.value(cAxis->controllerAxis, 0.0f);
    auto oldActions = m_guiContext->actions(cAxis->controllerAxis, oldAxisValue);

    if (cAxis->controllerAxis == ControllerAxis::LeftX)
      m_controllerLeftStick[0] = axisValue;
    else if (cAxis->controllerAxis == ControllerAxis::LeftY)
      m_controllerLeftStick[1] = axisValue;
    else if (cAxis->controllerAxis == ControllerAxis::RightX) {
      m_controllerRightStickRaw[0] = cAxis->controllerAxisValue;
      m_controllerRightStick[0] = axisValue;
    }
    else if (cAxis->controllerAxis == ControllerAxis::RightY) {
      m_controllerRightStickRaw[1] = cAxis->controllerAxisValue;
      m_controllerRightStick[1] = axisValue;
    }

    if (cAxis->controllerAxis != ControllerAxis::Invalid)
      m_controllerAxisValues[cAxis->controllerAxis] = axisValue;

    auto newActions = m_guiContext->actions(cAxis->controllerAxis, axisValue);
    for (auto action : newActions) {
      if (!oldActions.contains(action))
        m_edgeControllerAxisActions.add(action);
    }
  }

  bool panelMode = panelInteractionModeActive();
  bool shouldRouteOriginal = !(panelMode && (event.is<ControllerButtonDownEvent>() || event.is<ControllerButtonUpEvent>() || event.is<ControllerAxisEvent>()));
  bool processed = shouldRouteOriginal ? routeInputEvent(event) : false;

  if (panelMode) {
    auto panelHasList = [&](PanePtr const& pane) {
      if (!pane)
        return false;

      List<WidgetPtr> stack = {pane};
      while (!stack.empty()) {
        auto widget = stack.takeLast();
        if (!widget)
          continue;
        if (as<ListWidget>(widget))
          return true;
        for (size_t i = 0; i < widget->numChildren(); ++i)
          stack.append(widget->getChildNum(i));
      }

      return false;
    };

    if (auto cAxis = event.ptr<ControllerAxisEvent>()) {
      if (cAxis->controllerAxis == ControllerAxis::RightY && m_mainInterface) {
        auto topPane = m_mainInterface->paneManager()->topPane({PaneLayer::ModalWindow, PaneLayer::Window});
        float axisY = m_controllerRightStick[1];
        if (topPane && panelHasList(topPane) && std::abs(axisY) > 0.55f) {
          m_panelScrollAccumulator += axisY * 6.0f * GlobalTimestep;
          while (m_panelScrollAccumulator >= 1.0f) {
            InputEvent mouseEvent{MouseWheelEvent{MouseWheel::Up, m_input->mousePosition()}};
            routeInputEvent(mouseEvent);
            m_panelScrollAccumulator -= 1.0f;
          }
          while (m_panelScrollAccumulator <= -1.0f) {
            InputEvent mouseEvent{MouseWheelEvent{MouseWheel::Down, m_input->mousePosition()}};
            routeInputEvent(mouseEvent);
            m_panelScrollAccumulator += 1.0f;
          }
        } else {
          m_panelScrollAccumulator = 0.0f;
        }
      }

      if (cAxis->controllerAxis == ControllerAxis::TriggerLeft) {
        bool triggerHeld = cAxis->controllerAxisValue > 0.6f;
        if (triggerHeld && !m_panelDragMouseHeld) {
          m_panelDragMouseHeld = true;
          routeInputEvent(InputEvent{MouseButtonDownEvent{MouseButton::Left, m_input->mousePosition()}});
        } else if (!triggerHeld && m_panelDragMouseHeld) {
          m_panelDragMouseHeld = false;
          routeInputEvent(InputEvent{MouseButtonUpEvent{MouseButton::Left, m_input->mousePosition()}});
        }
      } else if (cAxis->controllerAxis == ControllerAxis::TriggerRight) {
        float triggerValue = cAxis->controllerAxisValue;
        if (!m_panelFocusHeld && m_panelFocusRecenterTimer <= 0.0f && triggerValue > 0.70f) {
          m_panelFocusHeld = true;
          m_panelFocusRecenterTimer = 0.15f;
          centerCursorOnPanelTarget();
        } else if (m_panelFocusHeld && triggerValue < 0.35f) {
          m_panelFocusHeld = false;
        }
      }

      return;
    }

    if (auto cDown = event.ptr<ControllerButtonDownEvent>()) {
      switch (cDown->controllerButton) {
        case ControllerButton::X:
        case ControllerButton::A: {
          InputEvent mouseEvent{MouseButtonDownEvent{MouseButton::Left, m_input->mousePosition()}};
          routeInputEvent(mouseEvent);
          break;
        }
        case ControllerButton::Y: {
          bool wasShiftHeld = m_guiContext->shiftHeld();
          m_guiContext->setShiftHeld(true);
          routeInputEvent(InputEvent{MouseButtonDownEvent{MouseButton::Left, m_input->mousePosition()}});
          routeInputEvent(InputEvent{MouseButtonUpEvent{MouseButton::Left, m_input->mousePosition()}});
          m_guiContext->setShiftHeld(wasShiftHeld);
          break;
        }
        case ControllerButton::B: {
          if (m_state > MainAppState::Title && m_mainInterface)
            m_mainInterface->paneManager()->dismissAllPanes({PaneLayer::ModalWindow, PaneLayer::Window});
          else {
            routeInputEvent(InputEvent{KeyDownEvent{Key::Escape, KeyMod::NoMod}});
            routeInputEvent(InputEvent{KeyUpEvent{Key::Escape}});
          }
          break;
        }
        case ControllerButton::LeftShoulder:
          routeInputEvent(InputEvent{ControllerButtonDownEvent{cDown->controller, ControllerButton::DPadLeft}});
          break;
        case ControllerButton::RightShoulder:
          routeInputEvent(InputEvent{ControllerButtonDownEvent{cDown->controller, ControllerButton::DPadRight}});
          break;
        default:
          break;
      }
      return;
    }

    if (auto cUp = event.ptr<ControllerButtonUpEvent>()) {
      if (cUp->controllerButton == ControllerButton::X || cUp->controllerButton == ControllerButton::A)
        routeInputEvent(InputEvent{MouseButtonUpEvent{MouseButton::Left, m_input->mousePosition()}});
      return;
    }
  }

  if (auto cDown = event.ptr<ControllerButtonDownEvent>()) {
    if (cDown->controllerButton == ControllerButton::RightStick) {
      auto configuration = m_root->configuration();
      bool controllerMouseEnabled = configuration->get("controllerMouseEnabled").optBool().value(true);
      configuration->set("controllerMouseEnabled", !controllerMouseEnabled);
      processed = true;
    }

    Maybe<MouseButton> mouseButton;
    if (cDown->controllerButton == ControllerButton::X)
      mouseButton = MouseButton::Left;
    else if (cDown->controllerButton == ControllerButton::Y)
      mouseButton = MouseButton::Right;

    if (mouseButton) {
      InputEvent mouseEvent{MouseButtonDownEvent{*mouseButton, m_input->mousePosition()}};
      processed |= routeInputEvent(mouseEvent);
    }
  } else if (auto cUp = event.ptr<ControllerButtonUpEvent>()) {
    Maybe<MouseButton> mouseButton;
    if (cUp->controllerButton == ControllerButton::X)
      mouseButton = MouseButton::Left;
    else if (cUp->controllerButton == ControllerButton::Y)
      mouseButton = MouseButton::Right;

    if (mouseButton) {
      InputEvent mouseEvent{MouseButtonUpEvent{*mouseButton, m_input->mousePosition()}};
      processed |= routeInputEvent(mouseEvent);
    }
  }
}

void ClientApplication::update() {
  float dt = GlobalTimestep * GlobalTimescale;
  auto& app = appController();
  if (m_state >= MainAppState::Title) {
    if (auto p2pNetworkingService = app->p2pNetworkingService()) {
      if (auto join = p2pNetworkingService->pullPendingJoin()) {
        m_pendingMultiPlayerConnection = PendingMultiPlayerConnection{join.takeValue(), {}, {}, false};
        changeState(MainAppState::Title);
      }
      
      if (auto req = p2pNetworkingService->pullJoinRequest())
        m_mainInterface->queueJoinRequest(*req);

      p2pNetworkingService->update();
    }
  }

  if (!m_errorScreen->accepted())
    m_errorScreen->update(dt);

  // This warning is only applicable to Linux systems so no need to process it otherwise.
  #ifdef STAR_ENABLE_STEAM_INTEGRATION
  #ifdef STAR_SYSTEM_LINUX
  if (m_state == MainAppState::SteamFlatpakWarning)
    updateSteamFlatpakWarning(dt);
  else
  #endif
  #endif

  if (m_state == MainAppState::Mods)
    updateMods(dt);
  else if (m_state == MainAppState::ModsWarning)
    updateModsWarning(dt);

  if (m_state == MainAppState::Splash)
    updateSplash(dt);
  else if (m_state == MainAppState::Error)
    updateError(dt);
  else if (m_state == MainAppState::Title)
    updateTitle(dt);
  else if (m_state > MainAppState::Title)
    updateRunning(dt);

  if (m_state >= MainAppState::Title)
    updateControllerMouse(dt);
  
  // Swallow leftover encoded voice data if we aren't in-game to allow mic read to continue for settings.
  if (m_state <= MainAppState::Title) {
    DataStreamBuffer ext;
    m_voice->send(ext);
  } // TODO: directly disable encoding at menu so we don't have to do this

  m_guiContext->cleanup();
  m_edgeKeyEvents.clear();
  m_edgeControllerButtonEvents.clear();
  m_edgeControllerAxisActions.clear();
  m_input->update();
  ++m_framesSkipped;
}

bool ClientApplication::panelInteractionModeActive() const {
  if (m_state == MainAppState::Title)
    return true;

  if (!m_mainInterface)
    return false;

  auto paneManager = m_mainInterface->paneManager();
  if (!paneManager)
    return false;

  return (bool)paneManager->topPane({PaneLayer::ModalWindow, PaneLayer::Window});
}

void ClientApplication::centerCursorOnPanelTarget() {
  if (!m_mainInterface)
    return;

  auto paneManager = m_mainInterface->paneManager();
  if (!paneManager)
    return;

  auto topPane = paneManager->topPane({PaneLayer::ModalWindow, PaneLayer::Window});
  if (!topPane)
    return;

  Vec2F cursorTarget = Vec2F(topPane->screenBoundRect().center()) * m_guiContext->interfaceScale();

  List<PanePtr> inventoryLikePanes;
  for (auto const& pane : paneManager->getAllPanes()) {
    if (!pane)
      continue;
    if (as<InventoryPane>(pane) || as<ContainerPane>(pane))
      inventoryLikePanes.append(pane);
  }

  if (!inventoryLikePanes.empty()) {
    size_t index = m_panelInventoryFocusToggle && inventoryLikePanes.size() > 1 ? 1 : 0;
    index = min(index, inventoryLikePanes.size() - 1);
    auto pane = inventoryLikePanes.at(index);

    List<WidgetPtr> stack = {pane};
    while (!stack.empty()) {
      auto child = stack.takeLast();
      if (!child)
        continue;

      if (auto slot = as<ItemSlotWidget>(child)) {
        cursorTarget = Vec2F(slot->screenBoundRect().center()) * m_guiContext->interfaceScale();
        break;
      }
      if (auto list = as<ListWidget>(child)) {
        cursorTarget = Vec2F(list->screenBoundRect().center()) * m_guiContext->interfaceScale();
        break;
      }

      for (size_t i = 0; i < child->numChildren(); ++i)
        stack.append(child->getChildNum(i));
    }

    if (inventoryLikePanes.size() > 1)
      m_panelInventoryFocusToggle = !m_panelInventoryFocusToggle;
  }

  auto screenSize = Vec2F(renderer()->screenSize());
  cursorTarget[0] = clamp(cursorTarget[0], 0.0f, screenSize[0] - 1.0f);
  cursorTarget[1] = clamp(cursorTarget[1], 0.0f, screenSize[1] - 1.0f);

  Vec2I cursorPosition = Vec2I::round(cursorTarget);
  cursorPosition[1] = (int)screenSize[1] - 1 - cursorPosition[1];
  appController()->setCursorPosition(cursorPosition);
}

void ClientApplication::updateControllerMouse(float dt) {
  if (!m_controllerInput)
    return;

  auto configuration = m_root->configuration();
  if (!configuration->get("controllerMouseEnabled").optBool().value(true))
    return;

  if (panelInteractionModeActive()) {
    float baseSpeed = configuration->get("controllerMouseSpeed").optFloat().value(1400.0f);
    if (baseSpeed <= 0.0f)
      return;

    bool slowOverInteractive = false;
    if (m_mainInterface && m_guiContext) {
      auto paneManager = m_mainInterface->paneManager();
      Vec2I mousePosUi = Vec2I::round(m_input->mousePosition() / m_guiContext->interfaceScale());
      if (auto pane = paneManager->getPaneAt({PaneLayer::ModalWindow, PaneLayer::Window}, mousePosUi)) {
        if (pane->cursorOverride(mousePosUi))
          slowOverInteractive = true;
      }
    }

    float speedScale = slowOverInteractive ? 0.50f : 0.75f;
    Vec2F mouseDelta = Vec2F(m_controllerLeftStick[0], -m_controllerLeftStick[1]) * (baseSpeed * speedScale) * dt;
    if (mouseDelta.magnitudeSquared() <= 0.0f)
      return;

    Vec2F nextMousePosition = m_input->mousePosition() + mouseDelta;
    Vec2F screenSize = Vec2F(renderer()->screenSize());
    nextMousePosition[0] = clamp(nextMousePosition[0], 0.0f, screenSize[0] - 1.0f);
    nextMousePosition[1] = clamp(nextMousePosition[1], 0.0f, screenSize[1] - 1.0f);

    Vec2I cursorPosition = Vec2I::round(nextMousePosition);
    cursorPosition[1] = (int)screenSize[1] - 1 - cursorPosition[1];
    appController()->setCursorPosition(cursorPosition);
    return;
  }

  if (m_mainInterface && m_mainInterface->windowsOpen())
    return;

  if (isActionTaken(InterfaceAction::InterfaceHotbarWheelHold) || isActionTaken(InterfaceAction::InterfacePanelWheelHold))
    return;

  float mouseSpeed = configuration->get("controllerMouseSpeed").optFloat().value(1400.0f);
  if (mouseSpeed <= 0.0f)
    return;

  float mouseDeadzone = configuration->get("controllerMouseDeadzone").optFloat().value(0.20f);
  float aimingDeadzone = mouseDeadzone * 2.0f;
  bool lockAimDirection = isActionTaken(InterfaceAction::PlayerControllerMoveOnly);

  if (m_state > MainAppState::Title && m_player && m_universeClient && m_universeClient->worldClient()) {
    Vec2F rightStick;
    rightStick[0] = applyControllerAxisResponse(m_controllerRightStickRaw[0], mouseDeadzone, 1.0f);
    rightStick[1] = applyControllerAxisResponse(m_controllerRightStickRaw[1], mouseDeadzone, 1.0f);

    if (stickAxisActive(rightStick)) {
      Vec2F mouseDelta = Vec2F(rightStick[0], -rightStick[1]) * mouseSpeed * dt;
      Vec2F nextMousePosition = m_input->mousePosition() + mouseDelta;

      Vec2F screenSize = Vec2F(renderer()->screenSize());
      nextMousePosition[0] = clamp(nextMousePosition[0], 0.0f, screenSize[0] - 1.0f);
      nextMousePosition[1] = clamp(nextMousePosition[1], 0.0f, screenSize[1] - 1.0f);

      if ((nextMousePosition - m_input->mousePosition()).magnitudeSquared() > 0.0f) {
        Vec2I cursorPosition = Vec2I::round(nextMousePosition);
        cursorPosition[1] = (int)screenSize[1] - 1 - cursorPosition[1];
        appController()->setCursorPosition(cursorPosition);
      }
      return;
    }

    float leftStickMagnitude = m_controllerLeftStick.magnitude();
    if (stickAxisActive(m_controllerLeftStick, aimingDeadzone) || (lockAimDirection && m_controllerLockedAimDirectionValid)) {
      Maybe<Vec2F> liveStickDirection;
      if (stickAxisActive(m_controllerLeftStick, aimingDeadzone))
        liveStickDirection = Vec2F(m_controllerLeftStick[0], -m_controllerLeftStick[1]) / leftStickMagnitude;

      Vec2F stickDirection;
      if (lockAimDirection && m_controllerLockedAimDirectionValid) {
        stickDirection = m_controllerLockedAimDirection;
      } else if (liveStickDirection) {
        stickDirection = *liveStickDirection;
        m_controllerLockedAimDirection = stickDirection;
        m_controllerLockedAimDirectionValid = true;
      } else {
        return;
      }

      Vec2F playerPosition = m_player->position();
      Vec2F castStart = playerPosition + stickDirection * 0.5f;
      Vec2F projectedCursorWorld = playerPosition + stickDirection * 12.0f;

      if (auto collision = m_universeClient->worldClient()->lineCollision(Line2F(castStart, projectedCursorWorld))) {
        auto geometry = m_universeClient->worldClient()->geometry();
        Vec2F collisionPoint = geometry.nearestTo(castStart, collision->first);
        Vec2F collisionDelta = geometry.diff(collisionPoint, castStart);
        float forwardDistance = collisionDelta[0] * stickDirection[0] + collisionDelta[1] * stickDirection[1];
        if (forwardDistance > 0.0f)
          projectedCursorWorld = collisionPoint;
      }

      Vec2F screenCursor = m_worldPainter->camera().worldToScreen(projectedCursorWorld);
      Vec2F screenSize = Vec2F(renderer()->screenSize());
      screenCursor[0] = clamp(screenCursor[0], 0.0f, screenSize[0] - 1.0f);
      screenCursor[1] = clamp(screenCursor[1], 0.0f, screenSize[1] - 1.0f);

      Vec2I cursorPosition = Vec2I::round(screenCursor);
      cursorPosition[1] = (int)screenSize[1] - 1 - cursorPosition[1];
      appController()->setCursorPosition(cursorPosition);
      return;
    }

    if (!lockAimDirection)
      m_controllerLockedAimDirectionValid = false;
  }

  return;
}

void ClientApplication::render() {
  m_framesSkipped = 0;
  auto config = m_root->configuration();
  auto assets = m_root->assets();
  auto& renderer = Application::renderer();

  renderer->setMultiSampling(config->get("antiAliasing").optBool().value(false) ? 4 : 0);
  renderer->switchEffectConfig("interface");

  if (auto interfaceScale = config->get("interfaceScale").optFloat().value(); interfaceScale != 0)
    m_guiContext->setInterfaceScale(interfaceScale);
  else if (m_guiContext->windowWidth() >= m_crossoverRes[0] && m_guiContext->windowHeight() >= m_crossoverRes[1])
    m_guiContext->setInterfaceScale(m_maxInterfaceScale);
  else
    m_guiContext->setInterfaceScale(m_minInterfaceScale);

  if (m_state == MainAppState::Mods || m_state == MainAppState::Splash) {
    m_cinematicOverlay->render();

  } else if (m_state == MainAppState::Title) {
    m_titleScreen->render();
    m_cinematicOverlay->render();

  } else if (m_state > MainAppState::Title) {
    WorldClientPtr worldClient = m_universeClient->worldClient();
    if (worldClient) {
      auto totalStart = Time::monotonicMicroseconds();
      renderer->switchEffectConfig("world");
      auto clientStart = totalStart;
      worldClient->render(m_renderData, TilePainter::BorderTileSize);
      LogMap::set("client_render_world_client", strf(u8"{:05d}\u00b5s", Time::monotonicMicroseconds() - clientStart));

      auto paintStart = Time::monotonicMicroseconds();
      m_worldPainter->render(m_renderData, [&]() -> bool {
        return worldClient->waitForLighting(&m_renderData);
      });
      LogMap::set("client_render_world_painter", strf(u8"{:05d}\u00b5s", Time::monotonicMicroseconds() - paintStart));
      LogMap::set("client_render_world_total", strf(u8"{:05d}\u00b5s", Time::monotonicMicroseconds() - totalStart));
      
      auto size = Vec2F(renderer->screenSize());
      auto quad = renderFlatRect(RectF::withSize(size / -2, size), Vec4B::filled(0), 0.0f);
      for (auto& layer : m_postProcessLayers) {
        if (layer.group ? layer.group->enabled : true) {
          for (unsigned i = 0; i < layer.passes; i++) {
            for (auto& effect : layer.effects) {
              renderer->switchEffectConfig(effect);
              renderer->render(quad);
            }
          }
        }
      }
    }
    renderer->switchEffectConfig("interface");
    auto start = Time::monotonicMicroseconds();
    m_mainInterface->renderInWorldElements();
    m_mainInterface->render();
    renderHotbarWheelOverlay();
    renderPanelWheelOverlay();
    m_cinematicOverlay->render();
    LogMap::set("client_render_interface", strf(u8"{:05d}\u00b5s", Time::monotonicMicroseconds() - start));
  }

  if (!m_errorScreen->accepted())
    m_errorScreen->render();
}

void ClientApplication::renderHotbarWheelOverlay() {
  if (!m_hotbarWheelActive || !m_player || !m_guiContext)
    return;

  auto inventory = m_player->inventory();
  int slotCount = hotbarWheelSlotCount(inventory);
  if (slotCount <= 0)
    return;

  Vec2F center = Vec2F(m_guiContext->windowInterfaceSize()) / 2.0f;
  float radius = 56.0f;
  float slotSize = 20.0f;
  float fullCircle = 6.28318530718f;

  for (int index = 0; index < slotCount; ++index) {
    float angle = ((index + 0.5f) / slotCount) * fullCircle;
    Vec2F slotCenter = center + Vec2F(cos(angle), -sin(angle)) * radius;

    auto slotSelection = hotbarWheelSelectionFromIndex(inventory, index);
    bool selected = m_hotbarWheelSelection && *m_hotbarWheelSelection == slotSelection;

    Vec4B bgColor = selected ? Vec4B(15, 15, 15, 220) : Vec4B(5, 5, 5, 170);
    m_guiContext->drawInterfaceQuad(RectF::withCenter(slotCenter, Vec2F::filled(slotSize)), bgColor);

    if (selected)
      m_guiContext->drawInterfacePolyLines(PolyF(RectF::withCenter(slotCenter, Vec2F::filled(slotSize + 6.0f))), Vec4B(255, 236, 170, 255), 1.5f);

    if (auto item = hotbarWheelItemForSelection(inventory, slotSelection)) {
      Vec4B iconColor = selected ? Vec4B::filled(255) : Vec4B(220, 220, 220, 255);
      for (auto const& drawable : item->iconDrawables())
        m_guiContext->drawInterfaceDrawable(drawable, slotCenter, iconColor);
    }
  }
}

void ClientApplication::renderPanelWheelOverlay() {
  if (!m_panelWheelActive || !m_guiContext)
    return;

  auto const& options = panelWheelOptions();
  int optionCount = options.size();
  if (optionCount <= 0)
    return;

  Vec2F center = Vec2F(m_guiContext->windowInterfaceSize()) / 2.0f;
  float radius = 62.0f;
  float slotSize = 24.0f;
  float fullCircle = 6.28318530718f;

  for (int index = 0; index < optionCount; ++index) {
    auto option = options.at(index);
    float angle = ((index + 0.5f) / optionCount) * fullCircle;
    Vec2F slotCenter = center + Vec2F(cos(angle), -sin(angle)) * radius;

    bool selected = m_panelWheelSelection && *m_panelWheelSelection == option;
    Vec4B bgColor = selected ? Vec4B(25, 25, 25, 225) : Vec4B(5, 5, 5, 170);
    m_guiContext->drawInterfaceQuad(RectF::withCenter(slotCenter, Vec2F::filled(slotSize)), bgColor);

    if (selected)
      m_guiContext->drawInterfacePolyLines(PolyF(RectF::withCenter(slotCenter, Vec2F::filled(slotSize + 6.0f))), Vec4B(170, 220, 255, 255), 1.5f);

    m_guiContext->renderInterfaceText(panelWheelOptionLabel(option), {slotCenter, HorizontalAnchor::HMidAnchor, VerticalAnchor::VMidAnchor});
  }
}

void ClientApplication::getAudioData(int16_t* sampleData, size_t frameCount) {
  if (m_mainMixer) {
    m_mainMixer->read(sampleData, frameCount, [&](int16_t* buffer, size_t frames, unsigned channels) {
      if (m_voice)
        m_voice->mix(buffer, frames, channels);
    });
  }
}

auto postProcessGroupsRoot = "postProcessGroups";

void ClientApplication::renderReload() {
  auto assets = m_root->assets();
  auto renderer = Application::renderer();

  auto loadEffectConfig = [&](String const& name) {
    String path = strf("/rendering/effects/{}.config", name);
    if (assets->assetExists(path)) {
      StringMap<String> shaders;
      auto config = assets->json(path);
      auto shaderConfig = config.getObject("effectShaders");
      for (auto& entry : shaderConfig) {
        if (entry.second.isType(Json::Type::String)) {
          String shader = entry.second.toString();
          if (!shader.hasChar('\n')) {
            auto shaderBytes = assets->bytes(AssetPath::relativeTo(path, shader));
            shader = std::string(shaderBytes->ptr(), shaderBytes->size());
          }
          shaders[entry.first] = shader;
        }
      }

      renderer->loadEffectConfig(name, config, shaders);
    } else
      Logger::warn("No rendering config found for renderer with id '{}'", renderer->rendererId());
  };

  renderer->loadConfig(assets->json("/rendering/opengl.config"));
  
  loadEffectConfig("world");
  
  // define post process groups and set them to be enabled/disabled based on config
  
  auto config = m_root->configuration();
  if (!config->get(postProcessGroupsRoot).isType(Json::Type::Object))
    config->set(postProcessGroupsRoot, JsonObject());
  auto groupsConfig = config->get(postProcessGroupsRoot);
  
  m_postProcessGroups.clear();
  auto postProcessGroups = assets->json("/client.config:postProcessGroups").toObject();
  for (auto& pair : postProcessGroups) {
    auto name = pair.first;
    auto groupConfig = groupsConfig.opt(name);
    auto def = pair.second.getBool("enabledDefault",true);
    if (!groupConfig)
      config->setPath(strf("{}.{}", postProcessGroupsRoot, name),JsonObject());
    m_postProcessGroups.add(name,PostProcessGroup{ groupConfig ? groupConfig.value().getBool("enabled", def) : def });
  }
  
  // define post process layers and optionally assign them to groups
  m_postProcessLayers.clear();
  m_labelledPostProcessLayers.clear();
  auto postProcessLayers = assets->json("/client.config:postProcessLayers").toArray();
  for (auto& layer : postProcessLayers) {
    auto effects = jsonToStringList(layer.getArray("effects"));
    for (auto& effect : effects)
      loadEffectConfig(effect);
    PostProcessGroup* group = nullptr;
    auto gname = layer.optString("group");
    if (gname) {
      group = &m_postProcessGroups.get(gname.value());
    }
    // I'd think a string map for all of these would be better, but the order does matter here, and does make sense to depend on mod priority, so...
    // guess a string map of indices works
    // I tried pointers but for whatever reason the behaviour was highly inconsistent and only worked after reload...
    auto label = layer.optString("name");
    if (label) {
      m_labelledPostProcessLayers.add(label.value(),m_postProcessLayers.count());
    }
    m_postProcessLayers.append(PostProcessLayer{ std::move(effects), (unsigned)layer.getUInt("passes", 1), group });
  }

  loadEffectConfig("interface");
}

void ClientApplication::setPostProcessLayerPasses(String const& layer, unsigned const& passes) {
  m_postProcessLayers.at(m_labelledPostProcessLayers.get(layer)).passes = passes;
}
void ClientApplication::setPostProcessGroupEnabled(String const& group, bool const& enabled, Maybe<bool> const& save) {
  m_postProcessGroups.get(group).enabled = enabled;
  if (save && save.value())
    m_root->configuration()->setPath(strf("{}.{}.enabled", postProcessGroupsRoot, group),enabled);
}
bool ClientApplication::postProcessGroupEnabled(String const& group) {
  return m_postProcessGroups.get(group).enabled;
}

Json ClientApplication::postProcessGroups() {
  return m_root->assets()->json("/client.config:postProcessGroups");
}

unsigned ClientApplication::framesSkipped() const {
  return m_framesSkipped;
}

void ClientApplication::changeState(MainAppState newState) {
  MainAppState oldState = m_state;
  m_state = newState;
  auto& app = appController();

  if (m_state == MainAppState::Quit)
    app->quit();

  if (newState == MainAppState::Mods)
    m_cinematicOverlay->load(m_root->assets()->json("/cinematics/mods/modloading.cinematic"));

  if (newState == MainAppState::Splash) {
    m_cinematicOverlay->load(m_root->assets()->json("/cinematics/splash.cinematic"));
    m_rootLoader = Thread::invoke("Async root loader", [this]() {
        m_root->fullyLoad();
      });
  }

  if (oldState > MainAppState::Title && m_state <= MainAppState::Title) {
    if (m_universeClient)
      m_universeClient->disconnect();

    if (m_universeServer) {
      m_universeServer->stop();
      m_universeServer->join();
      m_universeServer.reset();
    }
    m_cinematicOverlay->stop();

    // Clear HTTP trust request callback
    LuaBindings::clearHttpTrustRequestCallback();

    m_mainInterface.reset();

    m_voice->clearSpeakers();

    if (auto p2pNetworkingService = app->p2pNetworkingService()) {
      p2pNetworkingService->setJoinUnavailable();
      p2pNetworkingService->setAcceptingP2PConnections(false);
    }
  }

  if (oldState > MainAppState::Title && m_state == MainAppState::Title) {
    m_titleScreen->resetState();
    m_mainMixer->setUniverseClient({});
  }
  if (oldState >= MainAppState::Title && m_state < MainAppState::Title) {
    m_playerStorage.reset();

    if (m_statistics) {
      m_statistics->writeStatistics();
      m_statistics.reset();
    }

    m_universeClient.reset();
    m_mainMixer->setUniverseClient({});
    m_titleScreen.reset();
  }

  if (oldState < MainAppState::Title && m_state >= MainAppState::Title) {
    if (m_rootLoader)
      m_rootLoader.finish();

    m_cinematicOverlay->stop();

    m_playerStorage = make_shared<PlayerStorage>(m_root->toStoragePath("player"));
    m_statistics = make_shared<Statistics>(m_root->toStoragePath("player"), app->statisticsService());
    m_universeClient = make_shared<UniverseClient>(m_playerStorage, m_statistics);

    m_universeClient->setLuaCallbacks("input", LuaBindings::makeInputCallbacks());
    m_universeClient->setLuaCallbacks("voice", LuaBindings::makeVoiceCallbacks());
    m_universeClient->setLuaCallbacks("camera", LuaBindings::makeCameraCallbacks(&m_worldPainter->camera()));
    m_universeClient->setLuaCallbacks("renderer", LuaBindings::makeRenderingCallbacks(this));

    Json alwaysAllow = m_root->configuration()->getPath("safe.alwaysAllowClipboard");
    m_universeClient->setLuaCallbacks("clipboard", LuaBindings::makeClipboardCallbacks(app, alwaysAllow && alwaysAllow.toBool()));
    const bool luaHttpEnabled = m_root->configuration()->getPath("safe.luaHttp.enabled").optBool().value(false);

    m_universeClient->setLuaCallbacks("http", LuaBindings::makeHttpCallbacks(luaHttpEnabled));

    auto heldScriptPanes = make_shared<List<MainInterface::ScriptPaneInfo>>();

    m_universeClient->playerReloadPreCallback() = [&, heldScriptPanes](bool resetInterface) {
      if (!resetInterface)
        return;

      m_mainInterface->takeScriptPanes(*heldScriptPanes);
    };

    m_universeClient->playerReloadCallback() = [&, heldScriptPanes](bool resetInterface) {
      auto paneManager = m_mainInterface->paneManager();
      if (auto inventory = paneManager->registeredPane<InventoryPane>(MainInterfacePanes::Inventory))
        inventory->clearChangedSlots();

      if (resetInterface) {
        m_mainInterface->reviveScriptPanes(*heldScriptPanes);
        heldScriptPanes->clear();
      }
    };

    m_mainMixer->setUniverseClient(m_universeClient);
    m_titleScreen = make_shared<TitleScreen>(m_playerStorage, m_mainMixer->mixer(), m_universeClient);
    if (auto renderer = Application::renderer())
      m_titleScreen->renderInit(renderer);
  }

  if (m_state == MainAppState::Title) {
    auto configuration = m_root->configuration();

    if (m_pendingMultiPlayerConnection) {
      if (auto address = m_pendingMultiPlayerConnection->server.ptr<HostAddressWithPort>()) {
        m_titleScreen->setMultiPlayerAddress(toString(address->address()));
        m_titleScreen->setMultiPlayerPort(toString(address->port()));
        m_titleScreen->setMultiPlayerAccount(configuration->getPath("title.multiPlayerAccount").toString());
        m_titleScreen->setMultiPlayerForceLegacy(configuration->getPath("title.multiPlayerForceLegacy").optBool().value(false));
        m_titleScreen->goToMultiPlayerSelectCharacter(false);
      } else {
        m_titleScreen->goToMultiPlayerSelectCharacter(true);
      }
    } else {
      m_titleScreen->setMultiPlayerAddress(configuration->getPath("title.multiPlayerAddress").toString());
      m_titleScreen->setMultiPlayerPort(configuration->getPath("title.multiPlayerPort").toString());
      m_titleScreen->setMultiPlayerAccount(configuration->getPath("title.multiPlayerAccount").toString());
      m_titleScreen->setMultiPlayerForceLegacy(configuration->getPath("title.multiPlayerForceLegacy").optBool().value(false));
    }
  }

  if (m_state > MainAppState::Title) {
    if (m_titleScreen->currentlySelectedPlayer()) {
      m_player = m_titleScreen->currentlySelectedPlayer();
    } else {
      if (auto uuid = m_playerStorage->playerUuidAt(0))
        m_player = m_playerStorage->loadPlayer(*uuid);

      if (!m_player) {
        setError("Error loading player!");
        return;
      }
    }

    m_mainMixer->setUniverseClient(m_universeClient);
    m_universeClient->setMainPlayer(m_player);
    m_cinematicOverlay->setPlayer(m_player);
    m_timeSinceJoin = (int64_t)Time::millisecondsSinceEpoch() / 1000;

    auto assets = m_root->assets();
    String loadingCinematic = assets->json("/client.config:loadingCinematic").toString();
    m_cinematicOverlay->load(assets->json(loadingCinematic));
    if (!m_player->log()->introComplete()) {
      String introCinematic = assets->json("/client.config:introCinematic").toString();
      introCinematic = introCinematic.replaceTags(StringMap<String>{{"species", m_player->species()}});
      m_player->setPendingCinematic(Json(introCinematic));
    } else {
      m_player->setPendingCinematic(Json());
    }

    if (m_state == MainAppState::MultiPlayer) {
      PacketSocketUPtr packetSocket;

      auto multiPlayerConnection = m_pendingMultiPlayerConnection.take();

      if (auto address = multiPlayerConnection.server.ptr<HostAddressWithPort>()) {
        try {
          packetSocket = TcpPacketSocket::open(TcpSocket::connectTo(*address));
        } catch (StarException const& e) {
          setError(strf("Join failed! Error connecting to '{}'", *address), e);
          return;
        }

      } else {
        auto p2pPeerId = multiPlayerConnection.server.ptr<P2PNetworkingPeerId>();

        if (auto p2pNetworkingService = app->p2pNetworkingService()) {
          auto result = p2pNetworkingService->connectToPeer(*p2pPeerId);
          if (result.isLeft()) {
            setError(strf("Cannot join peer: {}", result.left()));
            return;
          } else {
            packetSocket = P2PPacketSocket::open(std::move(result.right()));
          }
        } else {
          setError("Internal error, no p2p networking service when joining p2p networking peer");
          return;
        }
      }

      bool allowAssetsMismatch = m_root->configuration()->get("allowAssetsMismatch").toBool();
      if (auto errorMessage = m_universeClient->connect(UniverseConnection(std::move(packetSocket)), allowAssetsMismatch,
            multiPlayerConnection.account, multiPlayerConnection.password, multiPlayerConnection.forceLegacy)) {
        setError(*errorMessage);
        return;
      }

      if (auto address = multiPlayerConnection.server.ptr<HostAddressWithPort>())
        m_currentRemoteJoin = *address;
      else
        m_currentRemoteJoin.reset();

    } else {
      if (!m_universeServer) {
        try {
          m_universeServer = make_shared<UniverseServer>(m_root->toStoragePath("universe"));
          m_universeServer->start();
        } catch (StarException const& e) {
          setError("Unable to start local server", e);
          return;
        }
      }

      if (auto errorMessage = m_universeClient->connect(m_universeServer->addLocalClient(), "", "")) {
        setError(strf("Error connecting locally: {}", *errorMessage));
        return;
      }
    }

    m_titleScreen->stopMusic();

    m_universeClient->restartLua();
    m_mainInterface = make_shared<MainInterface>(m_universeClient, m_worldPainter, m_cinematicOverlay);
    m_universeClient->setLuaCallbacks("interface", LuaBindings::makeInterfaceCallbacks(m_mainInterface.get()));
    m_universeClient->setLuaCallbacks("chat", LuaBindings::makeChatCallbacks(m_mainInterface.get(), m_universeClient.get()));
    m_universeClient->setLuaCallbacks("celestial", LuaBindings::makeCelestialCallbacks(m_universeClient.get()));
    m_universeClient->setLuaCallbacks("team", LuaBindings::makeTeamClientCallbacks(m_universeClient->teamClient().get()));
    m_universeClient->setLuaCallbacks("world", LuaBindings::makeWorldCallbacks(m_universeClient->worldClient().get()));

    LuaBindings::setHttpTrustRequestCallback([mainInterface = m_mainInterface.get()](String const& domain) {
      const auto paneManager = mainInterface->paneManager();
      const auto httpTrustDialog = paneManager->registeredPane<HttpTrustDialog>(MainInterfacePanes::HttpTrustDialog);

      httpTrustDialog->displayRequest(domain, [domain](const HttpTrustReply reply, bool remember) {
        const bool allowed = (reply == HttpTrustReply::Allow);
        LuaBindings::handleHttpTrustReply(domain, allowed);
      });
      paneManager->displayRegisteredPane(MainInterfacePanes::HttpTrustDialog);
    });


    m_mainInterface->displayDefaultPanes();
    m_universeClient->startLuaScripts();

    m_mainMixer->setWorldPainter(m_worldPainter);

    if (auto renderer = Application::renderer()) {
      m_worldPainter->renderInit(renderer);
    }
  }
}

void ClientApplication::setError(String const& error) {
  Logger::error(error.utf8Ptr());
  m_errorScreen->setMessage(error);
  m_titleScreen->resetState();
  changeState(MainAppState::Title);
}

void ClientApplication::setError(String const& error, std::exception const& e) {
  Logger::error("{}\n{}", error, outputException(e, true));
  m_errorScreen->setMessage(strf("{}\n{}", error, outputException(e, false)));
  m_titleScreen->resetState();
  changeState(MainAppState::Title);
}

void ClientApplication::loadMods() {
  auto ugcService = appController()->userGeneratedContentService();
  auto configuration = m_root->configuration();
  bool includeUGC = configuration->get("includeUGC", m_root->settings().includeUGC).toBool();
  if (ugcService && includeUGC) {
    StringList modDirectories;
    Logger::info("Checking for user generated content...");
    for (auto& contentId : ugcService->subscribedContentIds()) {
      if (auto contentDirectory = ugcService->contentDownloadDirectory(contentId)) {
        Logger::info("Loading mods from user generated content with id '{}' from directory '{}'", contentId, *contentDirectory);
        modDirectories.append(*contentDirectory);
      } else {
        Logger::warn("User generated content with id '{}' is not available", contentId);
      }
    }

    if (modDirectories.empty()) {
      Logger::info("No subscribed user generated content");
    } else {
      Root::singleton().loadMods(modDirectories, false);
      auto assets = m_root->assets();
    }
  }
}

void ClientApplication::updateSteamFlatpakWarning(float) {
  if (m_errorScreen->accepted())
    changeState(MainAppState::Mods);
}

void ClientApplication::updateMods(float dt) {
  m_cinematicOverlay->update(dt);
  auto ugcService = appController()->userGeneratedContentService();
  auto configuration = m_root->configuration();
  bool includeUGC = configuration->get("includeUGC", m_root->settings().includeUGC).toBool();
  if (ugcService && includeUGC) {
    // Prevent unnecessary log spam when UGC needs to be downloaded
    if (!m_loggedUGCCheck) {
      Logger::info("Checking for user generated content updates...");
      m_loggedUGCCheck = true;
    }
    
    if (ugcService->triggerContentDownload() == UserGeneratedContentService::UGCState::NoDownload) {
      changeState(MainAppState::Splash);
    } else {
      if (ugcService->triggerContentDownload() == UserGeneratedContentService::UGCState::Finished) {
        Logger::info("Loading updated user generated content...");
        StringList modDirectories;
        for (auto& contentId : ugcService->subscribedContentIds()) {
          if (auto contentDirectory = ugcService->contentDownloadDirectory(contentId)) {
            Logger::info("Loading mods from user generated content with id '{}' from directory '{}'", contentId, *contentDirectory);
            modDirectories.append(*contentDirectory);
          } else {
            Logger::warn("User generated content with id '{}' is not available", contentId);
          }
        }

        if (modDirectories.empty()) {
          changeState(MainAppState::Splash);
        } else {
          Logger::info("Reloading to include updated user generated content");
          Root::singleton().loadMods(modDirectories);

          // We've just reloaded, so make sure to grab our config again!
          // If we don't do this, we'll be able to read modsWarningShown
          // just fine, but we won't be able to write it back to the file.
          configuration = m_root->configuration();
        }

        auto assets = m_root->assets();

        if (configuration->get("modsWarningShown").optBool().value()) {
          changeState(MainAppState::Splash);
        } else {
          configuration->set("modsWarningShown", true);
          m_errorScreen->setMessage(assets->json("/interface.config:modsWarningMessage").toString());
          changeState(MainAppState::ModsWarning);
        }
      }
    }
  } else {
    changeState(MainAppState::Splash);
  }
}

void ClientApplication::updateModsWarning(float) {
  if (m_errorScreen->accepted())
    changeState(MainAppState::Splash);
}

void ClientApplication::updateSplash(float dt) {
  m_cinematicOverlay->update(dt);
  if (!m_rootLoader.isRunning() && (m_cinematicOverlay->completable() || m_cinematicOverlay->completed()))
    changeState(MainAppState::Title);
}

void ClientApplication::updateError(float) {
  if (m_errorScreen->accepted())
    changeState(MainAppState::Title);
}

void ClientApplication::updateTitle(float dt) {
  m_cinematicOverlay->update(dt);

  m_titleScreen->update(dt);
  m_mainMixer->update(dt);
  m_mainMixer->setSpeed(GlobalTimescale);

  auto& app = appController();
  bool inputActive = m_titleScreen->textInputActive();
  m_input->setTextInputActive(inputActive);
  if (inputActive)
    app->setTextArea(m_titleScreen->paneManager()->keyboardCapturedWidget()->keyboardCaptureArea());
  else
    app->setTextArea();
  app->setAcceptingTextInput(inputActive);

  auto p2pNetworkingService = app->p2pNetworkingService();
  if (p2pNetworkingService) {
    auto getStateString = [](TitleState state) -> const char* {
      switch (state) {
        case TitleState::Main:
          return "In Main Menu";
        case TitleState::Options:
          return "In Options";
        case TitleState::Mods:
          return "In Mods";
        case TitleState::SinglePlayerSelectCharacter:
          return "Selecting a character for singleplayer";
        case TitleState::SinglePlayerCreateCharacter:
          return "Creating a character for singleplayer";
        case TitleState::MultiPlayerSelectCharacter:
          return "Selecting a character for multiplayer";
        case TitleState::MultiPlayerCreateCharacter:
          return "Creating a character for multiplayer";
        case TitleState::MultiPlayerConnect:
          return "Awaiting multiplayer connection info";
        case TitleState::StartSinglePlayer:
          return "Loading Singleplayer";
        case TitleState::StartMultiPlayer:
          return "Connecting to Multiplayer";
        default:
          return "";
      }
    };

    p2pNetworkingService->setActivityData("Not In Game", getStateString(m_titleScreen->currentState()), 0, {});
  }

  if (m_titleScreen->currentState() == TitleState::StartSinglePlayer) {
    changeState(MainAppState::SinglePlayer);

  } else if (m_titleScreen->currentState() == TitleState::StartMultiPlayer) {
    if (!m_pendingMultiPlayerConnection || m_pendingMultiPlayerConnection->server.is<HostAddressWithPort>()) {
      auto addressString = m_titleScreen->multiPlayerAddress().trim();
      auto portString = m_titleScreen->multiPlayerPort().trim();
      portString = portString.empty() ? toString(m_root->configuration()->get("gameServerPort").toUInt()) : portString;
      if (auto port = maybeLexicalCast<uint16_t>(portString)) {
        auto address = HostAddressWithPort::lookup(addressString, *port);
        if (address.isLeft()) {
          setError(address.left());
        } else {
          m_pendingMultiPlayerConnection = PendingMultiPlayerConnection{
            address.right(),
            m_titleScreen->multiPlayerAccount(),
            m_titleScreen->multiPlayerPassword(),
            m_titleScreen->multiPlayerForceLegacy()
          };

          auto configuration = m_root->configuration();
          configuration->setPath("title.multiPlayerAddress", m_titleScreen->multiPlayerAddress());
          configuration->setPath("title.multiPlayerPort", m_titleScreen->multiPlayerPort());
          configuration->setPath("title.multiPlayerAccount", m_titleScreen->multiPlayerAccount());
          configuration->setPath("title.multiPlayerForceLegacy", m_titleScreen->multiPlayerForceLegacy());

          changeState(MainAppState::MultiPlayer);
        }
      } else {
        setError(strf("invalid port: {}", portString));
      }
    } else {
      changeState(MainAppState::MultiPlayer);
    }

  } else if (m_titleScreen->currentState() == TitleState::Quit) {
    changeState(MainAppState::Quit);
  }
}

void ClientApplication::updateRunning(float dt) {
  try {
    auto& app = appController();
    auto worldClient = m_universeClient->worldClient();
    auto p2pNetworkingService = app->p2pNetworkingService();
    bool clientIPJoinable = m_root->configuration()->get("clientIPJoinable").toBool();
    bool clientP2PJoinable = m_root->configuration()->get("clientP2PJoinable").toBool();
    Maybe<pair<uint16_t, uint16_t>> party = make_pair(m_universeClient->players(), m_universeClient->maxPlayers());

    if (m_state == MainAppState::MultiPlayer) {
      if (p2pNetworkingService) {
        p2pNetworkingService->setAcceptingP2PConnections(false);
        if (clientP2PJoinable && m_currentRemoteJoin)
          p2pNetworkingService->setJoinRemote(*m_currentRemoteJoin);
        else
          p2pNetworkingService->setJoinUnavailable();
      }
    } else {
      m_universeServer->setListeningTcp(clientIPJoinable);
      if (p2pNetworkingService) {
        p2pNetworkingService->setAcceptingP2PConnections(clientP2PJoinable);
        if (clientP2PJoinable) {
          p2pNetworkingService->setJoinLocal(m_universeServer->maxClients());
        } else {
          p2pNetworkingService->setJoinUnavailable();
          party = {};
        }
      }
    }

    bool movementLockedByAimOnly = isActionTaken(InterfaceAction::PlayerControllerAimOnly);
    bool panelWheelHeld = isActionTaken(InterfaceAction::InterfacePanelWheelHold);
    bool hotbarWheelHeld = isActionTaken(InterfaceAction::InterfaceHotbarWheelHold);

    if (m_panelWheelActive && !panelWheelHeld) {
      if (m_panelWheelSelection && m_mainInterface) {
        switch (*m_panelWheelSelection) {
          case PanelWheelOption::Inventory:
            m_mainInterface->paneManager()->toggleRegisteredPane(MainInterfacePanes::Inventory);
            break;
          case PanelWheelOption::Crafting:
            m_mainInterface->togglePlainCraftingWindow();
            break;
          case PanelWheelOption::Codex:
            m_mainInterface->paneManager()->toggleRegisteredPane(MainInterfacePanes::Codex);
            break;
          case PanelWheelOption::QuestLog:
            m_mainInterface->paneManager()->toggleRegisteredPane(MainInterfacePanes::QuestLog);
            break;
          case PanelWheelOption::MmUpgrade:
            m_mainInterface->paneManager()->toggleRegisteredPane(MainInterfacePanes::MmUpgrade);
            break;
          case PanelWheelOption::Collections:
            m_mainInterface->paneManager()->toggleRegisteredPane(MainInterfacePanes::Collections);
            break;
          case PanelWheelOption::EscapeMenu:
            m_mainInterface->paneManager()->toggleRegisteredPane(MainInterfacePanes::EscapeDialog);
            break;
        }
      }

      m_panelWheelActive = false;
      m_panelWheelSelection.reset();
    } else if (!m_panelWheelActive && panelWheelHeld) {
      m_panelWheelActive = true;
      m_panelWheelSelection.reset();
    }

    if (m_panelWheelActive) {
      float wheelDeadzone = m_root->configuration()->get("controllerMouseDeadzone").optFloat().value(0.20f);
      Vec2F rightStick;
      rightStick[0] = applyControllerAxisResponse(m_controllerRightStickRaw[0], wheelDeadzone, 1.0f);
      rightStick[1] = applyControllerAxisResponse(m_controllerRightStickRaw[1], wheelDeadzone, 1.0f);

      if (stickAxisActive(rightStick)) {
        float fullCircle = 6.28318530718f;
        float angle = atan2(rightStick[1], rightStick[0]);
        if (angle < 0.0f)
          angle += fullCircle;

        int optionCount = panelWheelOptions().size();
        int index = (int)floor((angle / fullCircle) * optionCount) % optionCount;
        m_panelWheelSelection = panelWheelOptionFromIndex(index);
      } else {
        m_panelWheelSelection.reset();
      }
    }

    if (m_panelWheelActive)
      hotbarWheelHeld = false;

    if (m_hotbarWheelActive && !hotbarWheelHeld) {
      if (m_hotbarWheelSelection)
        m_player->inventory()->selectActionBarLocation(*m_hotbarWheelSelection);

      m_hotbarWheelActive = false;
      m_hotbarWheelSelection.reset();
    } else if (!m_hotbarWheelActive && hotbarWheelHeld) {
      m_hotbarWheelActive = true;
      m_hotbarWheelSelection.reset();
    }

    if (m_hotbarWheelActive) {
      float wheelDeadzone = m_root->configuration()->get("controllerMouseDeadzone").optFloat().value(0.20f);
      Vec2F rightStick;
      rightStick[0] = applyControllerAxisResponse(m_controllerRightStickRaw[0], wheelDeadzone, 1.0f);
      rightStick[1] = applyControllerAxisResponse(m_controllerRightStickRaw[1], wheelDeadzone, 1.0f);

      if (stickAxisActive(rightStick)) {
        auto inventory = m_player->inventory();
        int slotCount = hotbarWheelSlotCount(inventory);
        if (slotCount > 0) {
          float fullCircle = 6.28318530718f;
          float angle = atan2(rightStick[1], rightStick[0]);
          if (angle < 0.0f)
            angle += fullCircle;

          int index = (int)floor((angle / fullCircle) * slotCount) % slotCount;
          m_hotbarWheelSelection = hotbarWheelSelectionFromIndex(inventory, index);
        }
      } else {
        m_hotbarWheelSelection.reset();
      }
    }
    
    if (p2pNetworkingService) {
      auto getActivityDetail = [&](String const& tag) -> String {
        if (tag == "playerName")
          return Text::stripEscapeCodes(m_player->name());
        if (tag == "playerHealth")
          return toString(m_player->health());
        if (tag == "playerMaxHealth")
          return toString(m_player->maxHealth());
        if (tag == "playerEnergy")
          return toString(m_player->energy());
        if (tag == "playerMaxEnergy")
          return toString(m_player->maxEnergy());
        if (tag == "playerBreath")
          return toString(m_player->breath());
        if (tag == "playerMaxBreath")
          return toString(m_player->maxBreath());
        if (tag == "playerXPos")
          return toString(round(m_player->position().x()));
        if (tag == "playerYPos")
          return toString(round(m_player->position().y()));
        if (tag == "worldName") {
          if (m_universeClient->clientContext()->playerWorldId().is<ClientShipWorldId>())
            return "Player Ship";
          else if (WorldTemplate const* worldTemplate = worldClient ? worldClient->currentTemplate().get() : nullptr) {
            auto worldName = worldTemplate->worldName();
            if (worldName.empty())
              return "In World";
            else
              return Text::stripEscapeCodes(worldName);
          }
          else
            return "Nowhere";
        }
        return "";
      };

      String finalDetails = "";
      Json activityDetails = m_root->configuration()->getPath("discord.activityDetails");
      if (activityDetails.isType(Json::Type::Array)) {
        StringList detailsList;
        for (auto& detail : activityDetails.iterateArray())
          detailsList.append(getActivityDetail(*detail.stringPtr()));
        finalDetails = detailsList.join("\n");
      } else if (activityDetails.isType(Json::Type::String))
        finalDetails = activityDetails.toString().lookupTags(getActivityDetail);

      p2pNetworkingService->setActivityData("In Game", finalDetails.utf8Ptr(), m_timeSinceJoin, party);
    }

    bool panelModeActive = panelInteractionModeActive();
    if (m_lastPanelModeActive && !panelModeActive)
      m_panelControlReenableTimer = max(m_panelControlReenableTimer, 0.1f);
    m_lastPanelModeActive = panelModeActive;

    if (m_panelControlReenableTimer > 0.0f)
      m_panelControlReenableTimer = max(0.0f, m_panelControlReenableTimer - dt);

    if (m_panelFocusRecenterTimer > 0.0f)
      m_panelFocusRecenterTimer = max(0.0f, m_panelFocusRecenterTimer - dt);

    bool controlsSuppressed = panelModeActive || m_panelControlReenableTimer > 0.0f;

    if (!controlsSuppressed && !m_mainInterface->inputFocus() && !m_cinematicOverlay->suppressInput()) {

      if (!movementLockedByAimOnly && isActionTaken(InterfaceAction::PlayerRight))
        m_player->moveRight();
      if (!movementLockedByAimOnly && isActionTaken(InterfaceAction::PlayerLeft))
        m_player->moveLeft();
      if (!movementLockedByAimOnly && isActionTaken(InterfaceAction::PlayerUp))
        m_player->moveUp();
      if (!movementLockedByAimOnly && isActionTaken(InterfaceAction::PlayerDown))
        m_player->moveDown();
      if (isActionTaken(InterfaceAction::PlayerJump))
        m_player->jump();

      if (isActionTaken(InterfaceAction::PlayerTechAction1))
        m_player->special(1);
      if (isActionTaken(InterfaceAction::PlayerTechAction2))
        m_player->special(2);
      if (isActionTaken(InterfaceAction::PlayerTechAction3))
        m_player->special(3);

      if (isActionTakenEdge(InterfaceAction::PlayerInteract))
        m_player->beginTrigger();
      else if (!isActionTaken(InterfaceAction::PlayerInteract))
        m_player->endTrigger();

      if (isActionTakenEdge(InterfaceAction::PlayerDropItem))
        m_player->dropItem();

      if (isActionTakenEdge(InterfaceAction::EmoteBlabbering))
        m_player->addEmote(HumanoidEmote::Blabbering);
      if (isActionTakenEdge(InterfaceAction::EmoteShouting))
        m_player->addEmote(HumanoidEmote::Shouting);
      if (isActionTakenEdge(InterfaceAction::EmoteHappy))
        m_player->addEmote(HumanoidEmote::Happy);
      if (isActionTakenEdge(InterfaceAction::EmoteSad))
        m_player->addEmote(HumanoidEmote::Sad);
      if (isActionTakenEdge(InterfaceAction::EmoteNeutral))
        m_player->addEmote(HumanoidEmote::NEUTRAL);
      if (isActionTakenEdge(InterfaceAction::EmoteLaugh))
        m_player->addEmote(HumanoidEmote::Laugh);
      if (isActionTakenEdge(InterfaceAction::EmoteAnnoyed))
        m_player->addEmote(HumanoidEmote::Annoyed);
      if (isActionTakenEdge(InterfaceAction::EmoteOh))
        m_player->addEmote(HumanoidEmote::Oh);
      if (isActionTakenEdge(InterfaceAction::EmoteOooh))
        m_player->addEmote(HumanoidEmote::OOOH);
      if (isActionTakenEdge(InterfaceAction::EmoteBlink))
        m_player->addEmote(HumanoidEmote::Blink);
      if (isActionTakenEdge(InterfaceAction::EmoteWink))
        m_player->addEmote(HumanoidEmote::Wink);
      if (isActionTakenEdge(InterfaceAction::EmoteEat))
        m_player->addEmote(HumanoidEmote::Eat);
      if (isActionTakenEdge(InterfaceAction::EmoteSleep))
        m_player->addEmote(HumanoidEmote::Sleep);

      if (isActionTakenEdge(InterfaceAction::InterfaceToggleControllerMouse)) {
        auto configuration = m_root->configuration();
        bool controllerMouseEnabled = configuration->get("controllerMouseEnabled").optBool().value(true);
        configuration->set("controllerMouseEnabled", !controllerMouseEnabled);
      }

      if (int newZoomDirection = (int)m_input->bindHeld("opensb", "zoomIn") - (int)m_input->bindHeld("opensb", "zoomOut"))
        m_cameraZoomDirection = newZoomDirection;
    }
    if (m_cameraZoomDirection != 0) {
      const float threshold = 0.01f;
      bool goingIn = m_cameraZoomDirection == 1;
      auto config = m_root->configuration();
      float curZoom = config->get("zoomLevel").toFloat(),
            newZoom = max(1.f, curZoom * powf(1.f + (float)m_cameraZoomDirection * 0.5f, min(1.f, dt * 5.f))),
            intZoom = max(1.f, (goingIn ? floor(curZoom) : ceil(curZoom)) + m_cameraZoomDirection);
      bool pastInt = goingIn ? newZoom + threshold > intZoom
                             : newZoom - threshold < intZoom;
      if (pastInt) {
        float intNewZoom = goingIn ? ceil(newZoom) : floor(newZoom);
        newZoom = lerp(clamp(abs(intZoom - newZoom) - 1.f, 0.f, 1.f), intZoom, intNewZoom);
        m_cameraZoomDirection = 0;
      }
      config->set("zoomLevel", min(1000000.f, newZoom));
    }

    if (m_player) {
      float currentHealth = m_player->health();
      if (panelModeActive && m_mainInterface && m_lastPlayerHealthValid && currentHealth < m_lastPlayerHealth - 0.001f)
        m_mainInterface->paneManager()->dismissAllPanes({PaneLayer::ModalWindow, PaneLayer::Window});
      m_lastPlayerHealth = currentHealth;
      m_lastPlayerHealthValid = true;
    }

    if (!controlsSuppressed) {
      m_panelFocusHeld = false;
      m_panelInventoryFocusToggle = false;
      m_panelDragMouseHeld = false;

      float leftStickMagnitude = m_controllerLeftStick.magnitude();
      if (m_controllerInput && stickAxisActive(m_controllerLeftStick) && !movementLockedByAimOnly)
        m_player->setMoveVector(m_controllerLeftStick);
      else
        m_player->setMoveVector(Vec2F());

      bool controllerSoftWalk = m_controllerInput && stickAxisActive(m_controllerLeftStick) && leftStickMagnitude < 0.85f && !movementLockedByAimOnly;
      m_player->setShifting(isActionTaken(InterfaceAction::PlayerShifting) || controllerSoftWalk);
    } else {
      m_player->setMoveVector(Vec2F());
      m_player->setShifting(false);
    }

    m_voice->setInput(m_input->bindHeld("opensb", "pushToTalk"));
    DataStreamBuffer voiceData;
    voiceData.setByteOrder(ByteOrder::LittleEndian);
    //voiceData.writeBytes(VoiceBroadcastPrefix.utf8Bytes()); transmitting with SE compat for now
    bool needstoSendVoice = m_voice->send(voiceData, 5000);

    auto checkDisconnection = [this]() {
      if (!m_universeClient->isConnected()) {
        m_cinematicOverlay->stop();
        String errMessage;
        if (auto disconnectReason = m_universeClient->disconnectReason())
          errMessage = strf("You were disconnected from the server for the following reason:\n{}", *disconnectReason);
        else
          errMessage = "Client-server connection no longer valid!";
        setError(errMessage);
        changeState(MainAppState::Title);
        return true;
      }

      return false;
    };

    if (checkDisconnection())
      return;

    m_mainInterface->preUpdate(dt);
    m_universeClient->update(dt);

    if (checkDisconnection())
      return;

    if (worldClient) {
      m_worldPainter->update(dt);
      auto& broadcastCallback = worldClient->broadcastCallback();
      if (!broadcastCallback) {
        broadcastCallback = [&](PlayerPtr player, StringView broadcast) -> bool {
          auto& view = broadcast.utf8();
          if (view.rfind(VoiceBroadcastPrefix.utf8(), 0) != NPos) {
            auto entityId = player->entityId();
            auto speaker = m_voice->speaker(connectionForEntity(entityId));
            speaker->entityId = entityId;
            speaker->name = player->name();
            speaker->position = player->mouthPosition();
            m_voice->receive(speaker, view.substr(VoiceBroadcastPrefix.utf8Size()));
          }
          return true;
        };
      }

      if (worldClient->inWorld()) {
        if (needstoSendVoice) {
          auto signature = Curve25519::sign(voiceData.ptr(), voiceData.size());
          std::string_view signatureView((char*)signature.data(), signature.size());
          std::string_view audioDataView(voiceData.ptr(), voiceData.size());
          auto broadcast = strf("data\0voice\0{}{}"s, signatureView, audioDataView);
          worldClient->sendSecretBroadcast(broadcast, true, false); // Already compressed by Opus.
        }
        if (auto mainPlayer = m_universeClient->mainPlayer()) {
          auto localSpeaker = m_voice->localSpeaker();
          localSpeaker->position = mainPlayer->position();
          localSpeaker->entityId = mainPlayer->entityId();
          localSpeaker->name = mainPlayer->name();
        }
        m_voice->setLocalSpeaker(worldClient->connection());
      }
      worldClient->setInteractiveHighlightMode(isActionTaken(InterfaceAction::ShowLabels));
    }
    updateCamera(dt);

    m_cinematicOverlay->update(dt);
    m_mainInterface->update(dt);
    m_mainMixer->update(dt, m_cinematicOverlay->muteSfx(), m_cinematicOverlay->muteMusic());
    m_mainMixer->setSpeed(GlobalTimescale);

    bool inputActive = m_mainInterface->textInputActive();
    m_input->setTextInputActive(inputActive);
    if (inputActive)
      app->setTextArea(m_mainInterface->paneManager()->keyboardCapturedWidget()->keyboardCaptureArea());
    else
      app->setTextArea();
    app->setAcceptingTextInput(inputActive);

    for (auto const& interactAction : m_player->pullInteractActions())
      m_mainInterface->handleInteractAction(interactAction);

    if (m_universeServer) {
      if (auto p2pNetworkingService = app->p2pNetworkingService()) {
        for (auto& p2pClient : p2pNetworkingService->acceptP2PConnections())
          m_universeServer->addClient(UniverseConnection(P2PPacketSocket::open(std::move(p2pClient))));
      }

      m_universeServer->setPause(m_mainInterface->escapeDialogOpen());
    }

    Vec2F aimPosition = m_player->aimPosition();
    float fps = app->renderFps();
    LogMap::set("client_render_rate", strf("{:4.2f} FPS ({:4.2f}ms)", fps, (1.0f / app->renderFps()) * 1000.0f));
    LogMap::set("client_update_rate", strf("{:4.2f}Hz", app->updateRate()));
    LogMap::set("player_pos", strf("[ ^#f45;{:4.2f}^reset;, ^#49f;{:4.2f}^reset; ]", m_player->position()[0], m_player->position()[1]));
    LogMap::set("player_vel", strf("[ ^#f45;{:4.2f}^reset;, ^#49f;{:4.2f}^reset; ]", m_player->velocity()[0], m_player->velocity()[1]));
    LogMap::set("player_aim", strf("[ ^#f45;{:4.2f}^reset;, ^#49f;{:4.2f}^reset; ]", aimPosition[0], aimPosition[1]));
    if (auto world = m_universeClient->worldClient()) {
      auto aim = Vec2I::floor(aimPosition);
      LogMap::set("tile_liquid_level", toString(world->liquidLevel(aim).level));
      LogMap::set("tile_dungeon_id", world->isTileProtected(aim) ? strf("^red;{}", world->dungeonId(aim)) : toString(world->dungeonId(aim)));
    }

    if (m_mainInterface->currentState() == MainInterface::ReturnToTitle)
      changeState(MainAppState::Title);

  } catch (std::exception& e) {
    setError("Exception caught in client main-loop", e);
  }
}

bool ClientApplication::isActionTaken(InterfaceAction action) const {
  for (auto keyEvent : m_heldKeyEvents) {
    if (m_guiContext->actions(keyEvent).contains(action))
      return true;
  }

  for (auto controllerButtonEvent : m_heldControllerButtonEvents) {
    if (m_guiContext->actions(controllerButtonEvent).contains(action))
      return true;
  }

  for (auto axisState : m_controllerAxisValues) {
    if (m_guiContext->actions(axisState.first, axisState.second).contains(action))
      return true;
  }

  return false;
}

bool ClientApplication::isActionTakenEdge(InterfaceAction action) const {
  for (auto keyEvent : m_edgeKeyEvents) {
    if (m_guiContext->actions(keyEvent).contains(action))
      return true;
  }

  for (auto controllerButtonEvent : m_edgeControllerButtonEvents) {
    if (m_guiContext->actions(controllerButtonEvent).contains(action))
      return true;
  }

  if (m_edgeControllerAxisActions.contains(action))
    return true;

  return false;
}

void ClientApplication::updateCamera(float dt) {
  if (!m_universeClient->worldClient())
    return;

  WorldCamera& camera = m_worldPainter->camera();
  camera.update(dt);

  if (m_mainInterface->fixedCamera())
    return;

  auto assets = m_root->assets();

  const float triggerRadius = 100.0f;
  const float deadzone = 0.1f;
  const float panFactor = 1.5f;
  float cameraSpeedFactor = 30.0f / m_root->configuration()->get("cameraSpeedFactor").toFloat();
  cameraSpeedFactor /= (dt * 60.f);

  auto playerCameraPosition = m_player->cameraPosition();

  if (isActionTaken(InterfaceAction::CameraShift)) {
    m_snapBackCameraOffset = false;
    m_cameraOffsetDownTime += dt;
    Vec2F aim = m_universeClient->worldClient()->geometry().diff(m_mainInterface->cursorWorldPosition(), playerCameraPosition);

    float magnitude = aim.magnitude() / (triggerRadius / camera.pixelRatio());
    if (magnitude > deadzone) {
      float cameraXOffset = aim.x() / magnitude;
      float cameraYOffset = aim.y() / magnitude;
      magnitude = (magnitude - deadzone) / (1.0 - deadzone);
      if (magnitude > 1)
        magnitude = 1;
      cameraXOffset *= magnitude * 0.5f * camera.pixelRatio() * panFactor;
      cameraYOffset *= magnitude * 0.5f * camera.pixelRatio() * panFactor;
      m_cameraXOffset = (m_cameraXOffset * (cameraSpeedFactor - 1.0) + cameraXOffset) / cameraSpeedFactor;
      m_cameraYOffset = (m_cameraYOffset * (cameraSpeedFactor - 1.0) + cameraYOffset) / cameraSpeedFactor;
    }
  } else {
    if (m_cameraOffsetDownTime > 0.0f && m_cameraOffsetDownTime < 0.333333f)
      m_snapBackCameraOffset = true;
    if (m_snapBackCameraOffset) {
      m_cameraXOffset = (m_cameraXOffset * (cameraSpeedFactor - 1.0)) / cameraSpeedFactor;
      m_cameraYOffset = (m_cameraYOffset * (cameraSpeedFactor - 1.0)) / cameraSpeedFactor;
    }
    m_cameraOffsetDownTime = 0.f;
  }
  Vec2F newCameraPosition;

  newCameraPosition.setX(playerCameraPosition.x());
  newCameraPosition.setY(playerCameraPosition.y());

  auto baseCamera = newCameraPosition;

  const float cameraSmoothRadius = assets->json("/interface.config:cameraSmoothRadius").toFloat();
  const float cameraSmoothFactor = assets->json("/interface.config:cameraSmoothFactor").toFloat();

  auto cameraSmoothDistance = m_universeClient->worldClient()->geometry().diff(m_cameraPositionSmoother, newCameraPosition).magnitude();
  if (cameraSmoothDistance > cameraSmoothRadius) {
    auto cameraDelta = m_universeClient->worldClient()->geometry().diff(m_cameraPositionSmoother, newCameraPosition);
    m_cameraPositionSmoother = newCameraPosition + cameraDelta.normalized() * cameraSmoothRadius;
    m_cameraSmoothDelta = {};
  }

  auto cameraDelta = m_universeClient->worldClient()->geometry().diff(m_cameraPositionSmoother, newCameraPosition);
  if (cameraDelta.magnitude() > assets->json("/interface.config:cameraSmoothDeadzone").toFloat())
    newCameraPosition = newCameraPosition + cameraDelta * (cameraSmoothFactor - 1.0) / cameraSmoothFactor;
  m_cameraPositionSmoother = newCameraPosition;

  newCameraPosition.setX(newCameraPosition.x() + m_cameraXOffset / camera.pixelRatio());
  newCameraPosition.setY(newCameraPosition.y() + m_cameraYOffset / camera.pixelRatio());

  auto smoothDelta = newCameraPosition - baseCamera;

  m_worldPainter->setCameraPosition(m_universeClient->worldClient()->geometry(), baseCamera + (smoothDelta + m_cameraSmoothDelta) * 0.5f);
  m_cameraSmoothDelta = smoothDelta;

  m_universeClient->worldClient()->setClientWindow(camera.worldTileRect());
}

}

STAR_MAIN_APPLICATION(Star::ClientApplication);
