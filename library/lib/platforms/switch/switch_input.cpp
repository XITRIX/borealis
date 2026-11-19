/*
    Copyright 2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <chrono>
#include <borealis/core/thread.hpp>
#include <borealis/core/application.hpp>
#include <borealis/platforms/switch/switch_input.hpp>
#ifdef __SWITCH2__
#include <borealis/platforms/switch/switch_touch_coordinates.hpp>
#include <borealis/platforms/switch/switch_chat_button.hpp>
#endif
#include <borealis/core/application.hpp>

// Avoid namespace collision
#define NXEvent ::Event

using namespace std::chrono_literals;

namespace brls
{

static const uint64_t HidNpadButton_None = ((uint64_t)1 << (63));

static const uint64_t SWITCH_BUTTONS_FULL_MAPPING[_BUTTON_MAX] = {
    HidNpadButton_ZL, // BUTTON_LT
    HidNpadButton_L, // BUTTON_LB

    HidNpadButton_StickL, // BUTTON_LSB

    HidNpadButton_Up, // BUTTON_UP
    HidNpadButton_Right, // BUTTON_RIGHT
    HidNpadButton_Down, // BUTTON_DOWN
    HidNpadButton_Left, // BUTTON_LEFT

    HidNpadButton_Minus, // BUTTON_BACK
    HidNpadButton_None, // BUTTON_GUIDE
    HidNpadButton_Plus, // BUTTON_START

    HidNpadButton_StickR, // BUTTON_RSB

    HidNpadButton_Y, // BUTTON_Y
    HidNpadButton_B, // BUTTON_B
    HidNpadButton_A, // BUTTON_A
    HidNpadButton_X, // BUTTON_X

    HidNpadButton_R, // BUTTON_RB
    HidNpadButton_ZR, // BUTTON_RT

    HidNpadButton_AnyUp, // BUTTON_NAV_UP
    HidNpadButton_AnyRight, // BUTTON_NAV_RIGHT
    HidNpadButton_AnyDown, // BUTTON_NAV_DOWN
    HidNpadButton_AnyLeft, // BUTTON_NAV_LEFT
};

static const uint64_t SWITCH_BUTTONS_HALF_MAPPING[_BUTTON_MAX] = {
    HidNpadButton_None, // BUTTON_LT
    HidNpadButton_AnySL, // BUTTON_LB

    HidNpadButton_StickL | HidNpadButton_StickR, // BUTTON_LSB

    // HidNpadButton_StickLRight | HidNpadButton_StickRLeft, // BUTTON_UP
    // HidNpadButton_StickLDown  | HidNpadButton_StickRUp, // BUTTON_RIGHT
    // HidNpadButton_StickLLeft  | HidNpadButton_StickRRight, // BUTTON_DOWN
    // HidNpadButton_StickLUp    | HidNpadButton_StickRDown, // BUTTON_LEFT

    HidNpadButton_None, // BUTTON_UP
    HidNpadButton_None, // BUTTON_RIGHT
    HidNpadButton_None, // BUTTON_DOWN
    HidNpadButton_None, // BUTTON_LEFT

    HidNpadButton_None, // BUTTON_BACK
    HidNpadButton_None, // BUTTON_GUIDE
    HidNpadButton_Plus | HidNpadButton_Minus, // BUTTON_START

    HidNpadButton_None, // BUTTON_RSB

    HidNpadButton_B | HidNpadButton_Up, // BUTTON_Y
    HidNpadButton_A | HidNpadButton_Left, // BUTTON_B
    HidNpadButton_X | HidNpadButton_Down, // BUTTON_A
    HidNpadButton_Y | HidNpadButton_Right, // BUTTON_X

    HidNpadButton_AnySR, // BUTTON_RB
    HidNpadButton_None, // BUTTON_RT

    HidNpadButton_StickLRight | HidNpadButton_StickRLeft, // BUTTON_NAV_UP
    HidNpadButton_StickLDown | HidNpadButton_StickRUp, // BUTTON_NAV_RIGHT
    HidNpadButton_StickLLeft | HidNpadButton_StickRRight, // BUTTON_NAV_DOWN
    HidNpadButton_StickLUp | HidNpadButton_StickRDown, // BUTTON_NAV_LEFT
};

static const size_t SWITCH_AXIS_MAPPING[_AXES_MAX] = {
    LEFT_X,
    LEFT_Y,
    RIGHT_X,
    RIGHT_Y,
};

SwitchInputManager::SwitchInputManager()
{
    this->system_button_thread = std::jthread(&SwitchInputManager::system_button_thread_fn, this);

    padConfigureInput(GAMEPADS_MAX, HidNpadStyleSet_NpadStandard);
    hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);
    hidSetNpadHandheldActivationMode(HidNpadHandheldActivationMode_Single);

    padInitialize(&this->padStateHandheld, HidNpadIdType_Handheld);
    hidInitializeVibrationDevices(m_vibration_device_handheld, 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    padUpdate(&this->padStateHandheld);

    for (int i = 0; i < GAMEPADS_MAX; i++)
    {
        padInitialize(&this->padsState[i], (HidNpadIdType)i);
        padUpdate(&this->padsState[i]);
        padsStyleSet[i] = this->padsState[i].style_set;
        clearVibration(i);
    }

    const Result mouseResult = this->mouse.initialize();
#ifdef __SWITCH2__
    if (R_FAILED(mouseResult))
        Logger::warning("SWITCH2_JOYCON_MOUSE: initialization failed rc=0x{:x}; USB mouse remains available", mouseResult);
    else
        Logger::info("SWITCH2_JOYCON_MOUSE: initialized; L/R=left ZL/ZR=right stick=middle");
#else
    (void)mouseResult;
#endif
    hidInitializeKeyboard();
    hidInitializeKeyboardMap();

    m_hid_keyboard_state.assign(256, false);

    // It's necessary to initialize these separately as they all have different handle values
    hidGetSixAxisSensorHandles(&this->m_six_axis_sensor_handle[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    hidGetSixAxisSensorHandles(&this->m_six_axis_sensor_handle[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
    hidGetSixAxisSensorHandles(&this->m_six_axis_sensor_handle[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
    hidStartSixAxisSensor(this->m_six_axis_sensor_handle[0]);
    hidStartSixAxisSensor(this->m_six_axis_sensor_handle[1]);
    hidStartSixAxisSensor(this->m_six_axis_sensor_handle[2]);
    hidStartSixAxisSensor(this->m_six_axis_sensor_handle[3]);
}

SwitchInputManager::~SwitchInputManager()
{
    this->system_button_thread.request_stop();
    if (this->system_button_thread.joinable())
        this->system_button_thread.join();

    if (this->cursorTexture != 0)
    {
        NVGcontext* vg = Application::getNVGContext();
        nvgDeleteImage(vg, this->cursorTexture);
        this->cursorTexture = 0;
    }

    hidStopSixAxisSensor(this->m_six_axis_sensor_handle[0]);
    hidStopSixAxisSensor(this->m_six_axis_sensor_handle[1]);
    hidStopSixAxisSensor(this->m_six_axis_sensor_handle[2]);
    hidStopSixAxisSensor(this->m_six_axis_sensor_handle[3]);

}

void SwitchInputManager::clearVibration(int controller)
{
    Logger::debug("Vibration clear #{}", controller);
    hidInitializeVibrationDevices(m_vibration_device_handles[controller], 2, (HidNpadIdType)controller, HidNpadStyleTag_NpadJoyDual);
    sendRumbleInternal(m_vibration_device_handles[controller], m_vibration_values[controller], 160.0f, 320.0f, 0.0f, 0.0f);
}

void SwitchInputManager::updateUnifiedControllerState(ControllerState* state)
{
    for (size_t i         = 0; i < _BUTTON_MAX; i++)
        state->buttons[i] = false;

    for (size_t i      = 0; i < _AXES_MAX; i++)
        state->axes[i] = 0;

    auto connected = getControllersConnectedCount();
    for (int i = 0; i < connected; i++)
    {
        ControllerState localState;
        updateControllerState(&localState, i);
        for (size_t j = 0; j < _BUTTON_MAX; j++)
            state->buttons[j] |= localState.buttons[j];

        for (size_t j = 0; j < _AXES_MAX; j++)
        {
            state->axes[j] += localState.axes[j];

            if (state->axes[j] < -1)
                state->axes[j] = -1;
            else if (state->axes[j] > 1)
                state->axes[j] = 1;
        }
    }

    state->buttons[BUTTON_NAV_UP] |= getKeyboardKeyState(BRLS_KBD_KEY_UP);
    state->buttons[BUTTON_NAV_RIGHT] |= getKeyboardKeyState(BRLS_KBD_KEY_RIGHT);
    state->buttons[BUTTON_NAV_DOWN] |= getKeyboardKeyState(BRLS_KBD_KEY_DOWN);
    state->buttons[BUTTON_NAV_LEFT] |= getKeyboardKeyState(BRLS_KBD_KEY_LEFT);

    if (Application::isSwapInputKeys())
    {
        state->buttons[BUTTON_B] |= getKeyboardKeyState(BRLS_KBD_KEY_ENTER);
        state->buttons[BUTTON_A] |= getKeyboardKeyState(BRLS_KBD_KEY_ESCAPE);
    }
    else
    {
        state->buttons[BUTTON_A] |= getKeyboardKeyState(BRLS_KBD_KEY_ENTER);
        state->buttons[BUTTON_B] |= getKeyboardKeyState(BRLS_KBD_KEY_ESCAPE);
    }
}

short SwitchInputManager::getControllersConnectedCount()
{
    padUpdate(&this->padStateHandheld);
    int extra       = padStateHandheld.active_handheld ? 1 : 0;
    int controllers = extra;
    for (int i = 0; i < GAMEPADS_MAX - extra; i++)
    {
        padUpdate(&this->padsState[i]);
        if (padsState[i].style_set == 0)
            break;
        controllers++;
    }
    return controllers;
}

void SwitchInputManager::updateControllerState(ControllerState* state, int controller)
{
    padUpdate(&this->padStateHandheld);
    if (controller == 0 && padStateHandheld.active_handheld)
    {
        updateControllerStateInner(state, &padStateHandheld);
        return;
    }

    int localController = padStateHandheld.active_handheld ? controller - 1 : controller;
    PadState* pad       = &this->padsState[localController];
    updateControllerStateInner(state, pad);

    if (padsStyleSet[localController] != pad->style_set)
    {
        padsStyleSet[localController] = pad->style_set;
        clearVibration(localController);
    }
}

void SwitchInputManager::updateControllerStateInner(ControllerState* state, PadState* pad)
{
    padUpdate(pad);
    uint64_t keysDown = padGetButtons(pad);
#ifdef __SWITCH2__
    const auto mouseSide = this->mouse.mouseSide(pad);
    if (mouseSide == PadPointerSide_Left)
        keysDown &= ~(HidNpadButton_L | HidNpadButton_ZL | HidNpadButton_StickL
            | HidNpadButton_StickLLeft | HidNpadButton_StickLRight | HidNpadButton_StickLUp | HidNpadButton_StickLDown);
    else if (mouseSide == PadPointerSide_Right)
        keysDown &= ~(HidNpadButton_R | HidNpadButton_ZR | HidNpadButton_StickR
            | HidNpadButton_StickRLeft | HidNpadButton_StickRRight | HidNpadButton_StickRUp | HidNpadButton_StickRDown);
#endif

    bool full = pad->style_set & HidNpadStyleSet_NpadFullCtrl;

    for (size_t i = 0; i < _BUTTON_MAX; i++)
    {
        uint64_t switchKey = full ? SWITCH_BUTTONS_FULL_MAPPING[i] : SWITCH_BUTTONS_HALF_MAPPING[i];
        state->buttons[i]  = keysDown & switchKey;
    }

    state->buttons[BUTTON_GUIDE] = false;

    if (m_screenshotButtonMode == ButtonOverrideMode::GUIDE_BUTTON)
        state->buttons[BUTTON_GUIDE] |= isScreenshotButtonPressed();

    if (m_homeButtonMode == ButtonOverrideMode::GUIDE_BUTTON)
        state->buttons[BUTTON_GUIDE] |= isHomeButtonPressed();
#ifdef __SWITCH2__
    if (m_chatButtonMode == ButtonOverrideMode::GUIDE_BUTTON)
        state->buttons[BUTTON_GUIDE] |= isChatButtonPressed();
#endif

    applyHalfMapping(state, pad, keysDown);

    HidAnalogStickState analog_stick_l = padGetStickPos(pad, 0);
    HidAnalogStickState analog_stick_r = padGetStickPos(pad, 1);
#ifdef __SWITCH2__
    if (mouseSide == PadPointerSide_Left)
        analog_stick_l = {};
    else if (mouseSide == PadPointerSide_Right)
        analog_stick_r = {};
#endif

    if (full)
    {
        state->axes[LEFT_X]  = (float)analog_stick_l.x / (float)0x7FFF;
        state->axes[LEFT_Y]  = (float)analog_stick_l.y / (float)0x7FFF * -1.0f;
        state->axes[RIGHT_X] = (float)analog_stick_r.x / (float)0x7FFF;
        state->axes[RIGHT_Y] = (float)analog_stick_r.y / (float)0x7FFF * -1.0f;
    }
    else
    {
        auto swap = Application::isSwapHalfJoyconStickToDpad();
        auto x = (float)analog_stick_l.y / (float)0x7FFF * -1.0f + (float)analog_stick_r.y / (float)0x7FFF;
        auto y = (float)analog_stick_l.x / (float)0x7FFF * -1.0f + (float)analog_stick_r.x / (float)0x7FFF;
           
        if (!swap) {
            state->axes[LEFT_X] = x;
            state->axes[LEFT_Y] = y;
        } else {
            state->buttons[BUTTON_UP] = y < -0.3f;
            state->buttons[BUTTON_DOWN] = y > 0.3f;
            state->buttons[BUTTON_RIGHT] = x > 0.3f;
            state->buttons[BUTTON_LEFT] = x < -0.3f;
        }
        state->axes[RIGHT_X] = 0;
        state->axes[RIGHT_Y] = 0;
    }

    state->axes[LEFT_Z]  = 0; // SWITCH NOT SUPPORT ZL AXIS
    state->axes[RIGHT_Z] = 0; // SWITCH NOT SUPPORT ZR AXIS
}

bool SwitchInputManager::getKeyboardKeyState(BrlsKeyboardScancode key)
{
    return m_hid_keyboard_state[m_brls_hid_map[key]];
}

void SwitchInputManager::updateTouchStates(std::vector<RawTouchState>* states)
{
    // Get touchscreen state
    static HidTouchScreenState hidState;

    if (hidGetTouchScreenStates(&hidState, 1))
    {
        for (int i = 0; i < hidState.count; i++)
        {
            RawTouchState state;
            state.pressed    = true;
            state.fingerId   = hidState.touches[i].finger_id;
#ifdef __SWITCH2__
            state.position.x = switchTouchCoordinate(hidState.touches[i].x, 1280.0f,
                Application::windowWidth, Application::windowScale);
            state.position.y = switchTouchCoordinate(hidState.touches[i].y, 720.0f,
                Application::windowHeight, Application::windowScale);
#else
            state.position.x = hidState.touches[i].x / Application::windowScale;
            state.position.y = hidState.touches[i].y / Application::windowScale;
#endif
            states->push_back(state);
        }
    }
}

void SwitchInputManager::sendRumbleInternal(HidVibrationDeviceHandle vibration_device[2], HidVibrationValue vibration_values[2], unsigned short lowFreqMotor, unsigned short highFreqMotor)
{
     vibration_values[0].amp_low = 
    vibration_values[0].amp_high = lowFreqMotor == 0 ? 0.0f : 320.0f;
    vibration_values[1].amp_low = 
    vibration_values[1].amp_high = highFreqMotor == 0 ? 0.0f : 320.0f;

    vibration_values[0].freq_low = 
    vibration_values[0].freq_high = lowFreqMotor == 0 ? 160.0f : (float) lowFreqMotor / 204;

    vibration_values[1].freq_low = highFreqMotor == 0 ? 160.0f : (float) highFreqMotor / 204;
    vibration_values[1].freq_high = highFreqMotor == 0 ? 160.0f : (float) highFreqMotor / 204;

    hidSendVibrationValues(vibration_device, vibration_values, 2);
}

void SwitchInputManager::sendRumbleInternal(HidVibrationDeviceHandle vibration_device[2], HidVibrationValue vibration_values[2], unsigned short lowFreqMotor, unsigned short highFreqMotor, unsigned short leftTriggerFreqMotor, unsigned short rightTriggerFreqMotor)
{
    vibration_values[0].amp_low = lowFreqMotor == 0 ? 0.0f : 320.0f;
    vibration_values[0].amp_high = leftTriggerFreqMotor == 0 ? 0.0f : 320.0f;
    vibration_values[1].amp_low = highFreqMotor == 0 ? 0.0f : 320.0f;
    vibration_values[1].amp_high = rightTriggerFreqMotor == 0 ? 0.0f : 320.0f;

    vibration_values[0].freq_low = lowFreqMotor == 0 ? 160.0f : (float) lowFreqMotor / 204;
    vibration_values[1].freq_low = highFreqMotor == 0 ? 160.0f : (float) highFreqMotor / 204;

    vibration_values[0].freq_high = leftTriggerFreqMotor == 0 ? 320.0f : (float) leftTriggerFreqMotor / 204;
    vibration_values[1].freq_high = rightTriggerFreqMotor == 0 ? 320.0f : (float) rightTriggerFreqMotor / 204;

    hidSendVibrationValues(vibration_device, vibration_values, 2);
}

void SwitchInputManager::sendRumble(unsigned short controller, unsigned short lowFreqMotor, unsigned short highFreqMotor)
{
    padUpdate(&this->padStateHandheld);
    if (controller == 0 && padStateHandheld.active_handheld)
    {
        sendRumbleInternal(m_vibration_device_handheld, m_vibration_values_handheld, lowFreqMotor, highFreqMotor);
        return;
    }

    int localController = padStateHandheld.active_handheld ? controller - 1 : controller;
    sendRumbleInternal(m_vibration_device_handles[localController], m_vibration_values[localController], lowFreqMotor, highFreqMotor);
}

void SwitchInputManager::sendRumble(unsigned short controller, unsigned short lowFreqMotor, unsigned short highFreqMotor, unsigned short leftTriggerFreqMotor, unsigned short rightTriggerFreqMotor)
{
    padUpdate(&this->padStateHandheld);
    if (controller == 0 && padStateHandheld.active_handheld)
    {
        sendRumbleInternal(m_vibration_device_handheld, m_vibration_values_handheld, lowFreqMotor, highFreqMotor, leftTriggerFreqMotor, rightTriggerFreqMotor);
        return;
    }

    int localController = padStateHandheld.active_handheld ? controller - 1 : controller;
    sendRumbleInternal(m_vibration_device_handles[localController], m_vibration_values[localController], lowFreqMotor, highFreqMotor, leftTriggerFreqMotor, rightTriggerFreqMotor);
}

void SwitchInputManager::updateMouseStates(RawMouseState* state)
{
    *state          = { };
    state->position = lastCursorPosition;
    if (currentMouseState.attributes & HidMouseAttribute_IsConnected)
    {
#ifdef __SWITCH2__
        state->position = Point(
            switchTouchCoordinate(currentMouseState.x, SwitchMouse::WIDTH, Application::windowWidth, Application::windowScale),
            switchTouchCoordinate(currentMouseState.y, SwitchMouse::HEIGHT, Application::windowHeight, Application::windowScale));
#else
        state->position = Point(currentMouseState.x, currentMouseState.y);
#endif
        state->offset = Point(currentMouseState.delta_x, currentMouseState.delta_y);
#ifdef __SWITCH2__
        state->scroll = Point(currentMouseState.menu_scroll_x * menuScrollSensitivity,
            currentMouseState.menu_scroll_y * menuScrollSensitivity);
#else
        state->scroll = Point(currentMouseState.wheel_delta_x, currentMouseState.wheel_delta_y);
#endif
        state->leftButton   = currentMouseState.buttons & HidMouseButton_Left;
        state->middleButton = currentMouseState.buttons & HidMouseButton_Middle;
        state->rightButton  = currentMouseState.buttons & HidMouseButton_Right;
        lastCursorPosition  = state->position;
    }
}

void SwitchInputManager::runloopStart()
{
    refreshHalfJoyConClaims();
    upToDateMouseState();
    handleMouse();
    handleKeyboard();
    handleControllerSensors();
}

void SwitchInputManager::upToDateMouseState()
{
    this->currentMouseState = this->mouse.update();
#ifdef __SWITCH2__
    const auto& pointer = this->mouse.joyCon();
    if (!this->joyConMotionLogged && (pointer.delta_x || pointer.delta_y))
    {
        Logger::info("SWITCH2_JOYCON_MOUSE: first motion side={} dx={} dy={}", static_cast<int>(pointer.side), pointer.delta_x, pointer.delta_y);
        this->joyConMotionLogged = true;
    }
    if (pointer.buttons_cur != pointer.buttons_old)
        Logger::info("SWITCH2_JOYCON_MOUSE: buttons=0x{:x} previous=0x{:x}", pointer.buttons_cur, pointer.buttons_old);
    if (!this->joyConScrollLogged && pointer.wheel_delta_y)
    {
        Logger::info("SWITCH2_JOYCON_MOUSE: first scroll raw={} wheelClicks={:.4f}",
            pointer.wheel_delta_y, pointer.wheel_delta_y / SwitchMouse::JOYCON_SCROLL_UNITS_PER_CLICK);
        this->joyConScrollLogged = true;
    }
#endif
}

void SwitchInputManager::handleMouse()
{
    if (currentMouseState.attributes & HidMouseAttribute_IsConnected)
    {
        getMouseCusorOffsetChanged()->fire(Point(currentMouseState.delta_x, currentMouseState.delta_y));
        getMouseScrollOffsetChanged()->fire(Point(currentMouseState.wheel_delta_x, currentMouseState.wheel_delta_y));
    }
}

void SwitchInputManager::handleKeyboard()
{
    HidKeyboardState state;

    if (hidGetKeyboardStates(&state, 1))
    {
        for (int i = 0; i < 256; ++i)
        {
            auto is_pressed = (state.keys[i / 64] & (1ul << (i % 64))) != 0;
            if (m_hid_keyboard_state[i] != is_pressed)
            {
                m_hid_keyboard_state[i]            = is_pressed;
                const BrlsKeyboardScancode brlsKey = m_hid_brls_map[i];

                KeyState keyState{};
                keyState.key     = brlsKey;
                keyState.pressed = is_pressed;

                if (state.modifiers & (HidKeyboardModifier_LeftAlt | HidKeyboardModifier_RightAlt))
                    keyState.mods |= BRLS_KBD_MODIFIER_ALT;

                if (state.modifiers & HidKeyboardModifier_Control)
                    keyState.mods |= BRLS_KBD_MODIFIER_CTRL;

                if (state.modifiers & HidKeyboardModifier_Shift)
                    keyState.mods |= BRLS_KBD_MODIFIER_SHIFT;

                if (state.modifiers & HidKeyboardModifier_Gui)
                    keyState.mods |= BRLS_KBD_MODIFIER_META;

                getKeyboardKeyStateChanged()->fire(keyState);
            }
        }
    }
    else
    {
        Logger::debug("Keyboard failed!");
    }
}

void SwitchInputManager::handleControllerSensors()
{
    HidSixAxisSensorState sixaxis = { 0 };

    uint64_t style_set = padGetStyleSet(&padsState[0]);
    if (padStateHandheld.active_handheld)
        hidGetSixAxisSensorStates(this->m_six_axis_sensor_handle[0], &sixaxis, 1);
    else if (style_set & HidNpadStyleTag_NpadFullKey)
        hidGetSixAxisSensorStates(this->m_six_axis_sensor_handle[1], &sixaxis, 1);
    else if (style_set & HidNpadStyleTag_NpadJoyDual)
    {
        // For JoyDual, read from either the Left or Right Joy-Con depending on which is/are connected
        u64 attrib = padGetAttributes(&padsState[0]);
        if (attrib & HidNpadAttribute_IsLeftConnected)
            hidGetSixAxisSensorStates(this->m_six_axis_sensor_handle[2], &sixaxis, 1);
        else if (attrib & HidNpadAttribute_IsRightConnected)
            hidGetSixAxisSensorStates(this->m_six_axis_sensor_handle[3], &sixaxis, 1);
    }

    auto accelState = SensorEvent{
        0,
        SensorEventType::ACCEL,
        { -sixaxis.acceleration.x,
          -sixaxis.acceleration.z,
          sixaxis.acceleration.y
        },
        0
    };
    auto gyroState = SensorEvent{
        0,
        SensorEventType::GYRO,
        { static_cast<float>(sixaxis.angular_velocity.x * 2.0f * M_PI),
          static_cast<float>(sixaxis.angular_velocity.z * 2.0f * M_PI),
          static_cast<float>(-sixaxis.angular_velocity.y * 2.0f * M_PI)
        },
        0 };
    getControllerSensorStateChanged()->fire(accelState);
    getControllerSensorStateChanged()->fire(gyroState);
    Application::setActiveEvent(true);
}

void SwitchInputManager::setPointerLock(bool lock)
{
    pointerLocked = lock;
}

void SwitchInputManager::drawCursor(NVGcontext* vg)
{
    initCursor(vg);
    if (!pointerLocked)
    {
        this->paint.xform[4] = lastCursorPosition.x;
        this->paint.xform[5] = lastCursorPosition.y;

        nvgBeginPath(vg);
        nvgRect(vg, lastCursorPosition.x, lastCursorPosition.y, this->cursorWidth, this->cursorHeight);
        nvgFillPaint(vg, this->paint);
        nvgFill(vg);
    }
}

void SwitchInputManager::initCursor(NVGcontext* vg)
{
    if (cursorInited)
        return;
    if (vg)
    {
#ifdef USE_LIBROMFS
        auto image          = romfs::get("img/sys/cursor.png");
        this->cursorTexture = nvgCreateImageMem(vg, NVG_IMAGE_NEAREST, (unsigned char*)image.data(), image.size());
#else
        this->cursorTexture = nvgCreateImage(vg, BRLS_ASSET("img/sys/cursor.png"), NVG_IMAGE_NEAREST);
#endif

        int width, height;
        nvgImageSize(vg, cursorTexture, &width, &height);
        float aspect       = (float)height / (float)width;
        this->cursorWidth  = 18;
        this->cursorHeight = 18 * aspect;

        this->paint        = nvgImagePattern(vg, 0, 0, this->cursorWidth, this->cursorHeight, 0, this->cursorTexture, 1.0f);
        this->cursorInited = true;
    }
}

static BrlsKeyboardScancode hidKey2BrlsKey(int key)
{
    if (KBD_A <= key && key <= KBD_Z)
    {
        return (BrlsKeyboardScancode)(key - KBD_A + BRLS_KBD_KEY_A);
    }
    if (KBD_1 <= key && key <= KBD_9)
    {
        return (BrlsKeyboardScancode)(key - KBD_1 + BRLS_KBD_KEY_1);
    }
    if (KBD_F1 <= key && key <= KBD_F12)
    {
        return (BrlsKeyboardScancode)(key - KBD_F1 + BRLS_KBD_KEY_F1);
    }
    if (KBD_F13 <= key && key <= KBD_F24)
    {
        return (BrlsKeyboardScancode)(key - KBD_F13 + BRLS_KBD_KEY_F13);
    }
    if (KBD_KP1 <= key && key <= KBD_KP9)
    {
        return (BrlsKeyboardScancode)(key - KBD_KP1 + BRLS_KBD_KEY_KP_1);
    }

    switch (key)
    {
        case KBD_0:
            return BRLS_KBD_KEY_0;
        case KBD_SPACE:
            return BRLS_KBD_KEY_SPACE;
        case KBD_APOSTROPHE:
            return BRLS_KBD_KEY_APOSTROPHE;
        case KBD_COMMA:
            return BRLS_KBD_KEY_COMMA;
        case KBD_MINUS:
            return BRLS_KBD_KEY_MINUS;
        case KBD_DOT:
            return BRLS_KBD_KEY_PERIOD;
        case KBD_SLASH:
            return BRLS_KBD_KEY_SLASH;
        case KBD_SEMICOLON:
            return BRLS_KBD_KEY_SEMICOLON;
        case KBD_EQUAL:
            return BRLS_KBD_KEY_EQUAL;
        case KBD_LEFTBRACE:
            return BRLS_KBD_KEY_LEFT_BRACKET;
        case KBD_RIGHTBRACE:
            return BRLS_KBD_KEY_RIGHT_BRACKET;
        case KBD_BACKSLASH:
        case KBD_HASHTILDE:
            return BRLS_KBD_KEY_BACKSLASH;
        case KBD_GRAVE:
            return BRLS_KBD_KEY_GRAVE_ACCENT;
        case KBD_ESC:
            return BRLS_KBD_KEY_ESCAPE;
        case KBD_ENTER:
            return BRLS_KBD_KEY_ENTER;
        case KBD_TAB:
            return BRLS_KBD_KEY_TAB;
        case KBD_BACKSPACE:
            return BRLS_KBD_KEY_BACKSPACE;
        case KBD_CAPSLOCK:
            return BRLS_KBD_KEY_CAPS_LOCK;
        case KBD_LEFTSHIFT:
            return BRLS_KBD_KEY_LEFT_SHIFT;
        case KBD_LEFTCTRL:
            return BRLS_KBD_KEY_LEFT_CONTROL;
        case KBD_LEFTALT:
            return BRLS_KBD_KEY_LEFT_ALT;
        case KBD_LEFTMETA:
            return BRLS_KBD_KEY_LEFT_SUPER;
        case KBD_RIGHTSHIFT:
            return BRLS_KBD_KEY_RIGHT_SHIFT;
        case KBD_RIGHTCTRL:
            return BRLS_KBD_KEY_RIGHT_CONTROL;
        case KBD_RIGHTALT:
            return BRLS_KBD_KEY_RIGHT_ALT;
        case KBD_RIGHTMETA:
            return BRLS_KBD_KEY_RIGHT_SUPER;
        case KBD_LEFT:
            return BRLS_KBD_KEY_LEFT;
        case KBD_RIGHT:
            return BRLS_KBD_KEY_RIGHT;
        case KBD_UP:
            return BRLS_KBD_KEY_UP;
        case KBD_DOWN:
            return BRLS_KBD_KEY_DOWN;

        case KBD_SYSRQ:
            return BRLS_KBD_KEY_PRINT_SCREEN;
        case KBD_SCROLLLOCK:
            return BRLS_KBD_KEY_SCROLL_LOCK;
        case KBD_PAUSE:
            return BRLS_KBD_KEY_PAUSE;
        case KBD_INSERT:
            return BRLS_KBD_KEY_INSERT;
        case KBD_HOME:
            return BRLS_KBD_KEY_HOME;
        case KBD_PAGEUP:
            return BRLS_KBD_KEY_PAGE_UP;
        case KBD_DELETE:
            return BRLS_KBD_KEY_DELETE;
        case KBD_END:
            return BRLS_KBD_KEY_END;
        case KBD_PAGEDOWN:
            return BRLS_KBD_KEY_PAGE_DOWN;

        case KBD_NUMLOCK:
            return BRLS_KBD_KEY_NUM_LOCK;
        case KBD_KPSLASH:
            return BRLS_KBD_KEY_KP_DIVIDE;
        case KBD_KPASTERISK:
            return BRLS_KBD_KEY_KP_MULTIPLY;
        case KBD_KPMINUS:
            return BRLS_KBD_KEY_KP_SUBTRACT;
        case KBD_KPPLUS:
            return BRLS_KBD_KEY_KP_ADD;
        case KBD_KPENTER:
            return BRLS_KBD_KEY_KP_ENTER;
        case KBD_KPDOT:
            return BRLS_KBD_KEY_KP_DECIMAL;
        case KBD_KP0:
            return BRLS_KBD_KEY_KP_0;

        case KBD_102ND:
            return BRLS_KBD_KEY_WORLD_1;
        case KBD_COMPOSE:
            return BRLS_KBD_KEY_MENU;

        default:
            return BRLS_KBD_KEY_UNKNOWN;
    }
}

void SwitchInputManager::hidInitializeKeyboardMap()
{
    m_hid_brls_map[KBD_NONE]             = BRLS_KBD_KEY_UNKNOWN;
    m_brls_hid_map[BRLS_KBD_KEY_UNKNOWN] = KBD_NONE;

    for (int i = 0; i < 256; ++i)
    {
        const auto brlsKey = hidKey2BrlsKey(i);
        if (brlsKey == BRLS_KBD_KEY_UNKNOWN)
            continue;
        m_hid_brls_map[i] = brlsKey;
        m_brls_hid_map.try_emplace(brlsKey, i);
    }
}

void SwitchInputManager::setHalfJoyConMapping(HalfJoyConSide side, const HalfJoyConMapping& mapping)
{
    if (side == HalfJoyConSide::NONE)
        return;
    auto& target = halfMappings[side == HalfJoyConSide::LEFT ? 0 : 1];
    for (size_t i = 0; i < mapping.size(); ++i)
        target[i] = validHalfJoyConAction(mapping[i]) ? mapping[i] : HALF_JOYCON_UNMAPPED;
    refreshHalfJoyConClaims();
}

void SwitchInputManager::setHalfJoyConMappingEnabled(bool enabled)
{
    halfMappingEnabled = enabled;
    refreshHalfJoyConClaims();
    if (!enabled)
    {
        if (halfSystemConsumed[0].exchange(false))
            m_isScreenshotPressed = false;
        if (halfSystemConsumed[1].exchange(false))
            m_isHomePressed = false;
#ifdef __SWITCH2__
        if (halfSystemConsumed[2].exchange(false))
            m_isChatPressed = false;
#endif
    }
}

bool SwitchInputManager::isHalfJoyConMappingActive(int controller) const
{
    if (!halfMappingEnabled || controller < 0 || controller >= GAMEPADS_MAX)
        return false;
    if (padStateHandheld.active_handheld)
    {
        if (controller == 0)
            return false;
        --controller;
    }
    return halfJoyConSide(padsState[controller].style_set) != HalfJoyConSide::NONE;
}

void SwitchInputManager::refreshHalfJoyConClaims()
{
    u32 combined                  = 0;
    std::array<u64, 3> candidates = { };
    if (halfMappingEnabled)
    {
        padUpdate(&padStateHandheld);
        if (padStateHandheld.active_handheld)
            for (auto& candidate : candidates)
                candidate |= 1ULL << HidNpadIdType_Handheld;
    }
    for (int i = 0; i < GAMEPADS_MAX; ++i)
    {
        u32 claims = 0;
        if (halfMappingEnabled)
        {
            padUpdate(&padsState[i]);
            const auto side = halfJoyConSide(padsState[i].style_set);
            if (padsState[i].style_set)
            {
                const u32 supported = side == HalfJoyConSide::LEFT ? 1U : side == HalfJoyConSide::RIGHT ? 6U
                                                                                                        : 7U;
                for (size_t button = 0; button < candidates.size(); ++button)
                    if (supported & (1U << button))
                        candidates[button] |= 1ULL << i;
            }
            if (side != HalfJoyConSide::NONE)
                claims = halfJoyConSystemClaims(halfMappings[side == HalfJoyConSide::LEFT ? 0 : 1], side);
        }
        if (halfSystemClaims[i].exchange(claims) != claims)
            Logger::info("HALF_JOYCON: pad={} claims=0x{:x}", i, claims);
        combined |= claims;
    }
    halfAnyClaims = combined;
    for (size_t button = 0; button < candidates.size(); ++button)
        halfSourceCandidates[button] = candidates[button];
}

void SwitchInputManager::applyHalfMapping(ControllerState* state, PadState* pad, u64 buttons)
{
    const auto side = halfJoyConSide(pad->style_set);
    if (!halfMappingEnabled || side == HalfJoyConSide::NONE)
        return;
    int owner = -1;
    for (int i = 0; i < GAMEPADS_MAX; ++i)
        if (pad->id_mask & (1U << i))
        {
            owner = i;
            break;
        }
    const int system         = side == HalfJoyConSide::LEFT ? 0 : 1;
    const bool held          = system == 0 ? m_isScreenshotPressed.load() : m_isHomePressed.load();
    const bool systemPressed = held && halfSystemConsumed[system] && systemButtonOwners[system] == owner;
    bool chatPressed         = false;
#ifdef __SWITCH2__
    chatPressed = side == HalfJoyConSide::RIGHT && m_isChatPressed && halfSystemConsumed[2] && systemButtonOwners[2] == owner;
#endif
    applyHalfJoyConMapping(state, halfMappings[side == HalfJoyConSide::LEFT ? 0 : 1], side, buttons, systemPressed, chatPressed);
}

void SwitchInputManager::processSystemButtonEvent(::Event* event, int button)
{
    std::atomic<ButtonOverrideMode>* mode = button == 0 ? &m_screenshotButtonMode : &m_homeButtonMode;
    std::atomic<bool>* held               = button == 0 ? &m_isScreenshotPressed : &m_isHomePressed;
#ifdef __SWITCH2__
    if (button == 2)
    {
        mode = &m_chatButtonMode;
        held = &m_isChatPressed;
    }
#endif
    const bool wasPressed = held->load();
    bool halfConsumed     = halfSystemConsumed[button];
    int owner             = systemButtonOwners[button];
    if (!wasPressed)
    {
        if (mode->load() == ButtonOverrideMode::NONE && !(halfAnyClaims.load() & (1U << button)))
            return;
        u32 active               = 0;
        const Result result      = hidsysGetLastActiveNpad(&active);
        owner                    = R_SUCCEEDED(result) ? static_cast<int>(active) : -1;
        const u64 candidates     = halfSourceCandidates[button];
        const bool possibleOwner = owner >= 0 && owner < 64 && (candidates & (1ULL << owner));
        if (halfMappingEnabled && (R_FAILED(result) || !possibleOwner))
        {
            // A right half has no Capture button, for example. Infer ownership
            // only if exactly one connected controller can produce this event.
            owner = -1;
            if (candidates && !(candidates & (candidates - 1)))
                for (int i = 0; i < 64; ++i)
                    if (candidates & (1ULL << i))
                    {
                        owner = i;
                        break;
                    }
        }
        halfConsumed = halfMappingEnabled && owner >= 0 && owner < GAMEPADS_MAX
            && (halfSystemClaims[owner].load() & (1U << button));
    }
    if (!halfConsumed && mode->load() == ButtonOverrideMode::NONE)
        return;
    eventClear(event);
    systemButtonOwners[button] = owner;
    halfSystemConsumed[button] = !wasPressed && halfConsumed;
    *held                      = !wasPressed;
    Logger::info("SWITCH_SYSTEM_BUTTON: button={} owner={} pressed={} half={} mode={}",
        button, owner, !wasPressed, halfConsumed, static_cast<int>(mode->load()));
}

void SwitchInputManager::system_button_thread_fn(std::stop_token token)
{
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x20);
    NXEvent screenshot_evt = { };
    if (auto rc = hidsysAcquireCaptureButtonEventHandle(&screenshot_evt, false); R_FAILED(rc))
    {
        Logger::error("Failed to acquire the screenshot button event: {}", rc);
        return;
    }
    DEFER([&screenshot_evt]
        { eventClose(&screenshot_evt); });
    NXEvent home_evt = { };
    if (auto rc = hidsysAcquireHomeButtonEventHandle(&home_evt, false); R_FAILED(rc))
    {
        Logger::error("Failed to acquire the home button event: {}", rc);
        return;
    }
    DEFER([&home_evt]
        { eventClose(&home_evt); });
#ifdef __SWITCH2__
    NXEvent chat_evt        = { };
    const Result chatResult = acquireSwitchChatButtonEvent(&chat_evt, false);
    const bool hasChatEvent = R_SUCCEEDED(chatResult);
    DEFER([&]
        { if (hasChatEvent) eventClose(&chat_evt); });
    Logger::info("SWITCH2_CHAT_BUTTON: acquireEvent rc=0x{:x} available={}", chatResult, hasChatEvent);
    if (hasChatEvent)
        eventClear(&chat_evt);
#endif
    eventClear(&screenshot_evt);
    eventClear(&home_evt);
    while (!token.stop_requested())
    {
        s32 idx               = -1;
        const Waiter events[] = {
            waiterForEvent(&screenshot_evt),
            waiterForEvent(&home_evt),
#ifdef __SWITCH2__
            waiterForEvent(&chat_evt),
#endif
        };
#ifdef __SWITCH2__
        const int eventCount = hasChatEvent ? 3 : 2;
#else
        const int eventCount = 2;
#endif
        const Result rc = waitObjects(&idx, events, eventCount, std::chrono::nanoseconds(50ms).count());
        if (rc == KERNELRESULT(TimedOut))
            continue;
        if (R_FAILED(rc))
        {
            Logger::warning("System button event wait failed: 0x{:x}", rc);
            break;
        }
        if (idx == 0)
            processSystemButtonEvent(&screenshot_evt, 0);
        else if (idx == 1)
            processSystemButtonEvent(&home_evt, 1);
#ifdef __SWITCH2__
        else if (idx == 2)
            processSystemButtonEvent(&chat_evt, 2);
#endif
    }
    m_isScreenshotPressed = false;
    m_isHomePressed       = false;
#ifdef __SWITCH2__
    m_isChatPressed = false;
#endif
}

} // namespace brls
