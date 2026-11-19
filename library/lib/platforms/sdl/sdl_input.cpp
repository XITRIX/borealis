/*
    Copyright 2021 natinusala
    Copyright 2021 XITRIX

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

#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/platforms/sdl/sdl_input.hpp>

#include <algorithm>

namespace brls
{

#if defined(PLATFORM_IOS) || defined(PLATFORM_VISIONOS) || defined(PLATFORM_ANDROID)
extern void init_device_rumble();
extern void device_rumble(unsigned short lowFreqMotor, unsigned short highFreqMotor);
#else
void init_device_rumble() {}
void device_rumble(unsigned short lowFreqMotor, unsigned short highFreqMotor) {}
#endif

#define SDL_GAMEPAD_BUTTON_NONE SIZE_MAX
#define SDL_GAMEPAD_BUTTON_MAX 15
#define SDL_GAMEPAD_AXIS_MAX 6
#define SDL_STICKY 2
#if defined(__SDL3__)
#define SDL_RELEASED 0
#define SDL_PRESSED 1
#endif

static float normalizeSdlAxis(Sint16 axis)
{
    return axis < 0 ? static_cast<float>(axis) / 32768.0f : static_cast<float>(axis) / 32767.0f;
}

/// HidKeyboardScancode
/// Uses the same key codes as GLFW
static const BrlsKeyboardScancode sdlToBrlsKeyboardScancode(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return BRLS_KBD_KEY_UNKNOWN;

    // 1 - 9
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
        return (BrlsKeyboardScancode)(BRLS_KBD_KEY_1 + scancode - SDL_SCANCODE_1);

    // KP1 - KP9
    if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
        return (BrlsKeyboardScancode)(BRLS_KBD_KEY_KP_1 + scancode - SDL_SCANCODE_KP_1);

    // A - Z
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
        return (BrlsKeyboardScancode)(BRLS_KBD_KEY_A + scancode - SDL_SCANCODE_A);

    // F1 - F12
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
        return (BrlsKeyboardScancode)(BRLS_KBD_KEY_F1 + scancode - SDL_SCANCODE_F1);

    switch (scancode)
    {
        case SDL_SCANCODE_UNKNOWN:
            return BRLS_KBD_KEY_UNKNOWN;

            /* Printable keys */
        case SDL_SCANCODE_SPACE:
            return BRLS_KBD_KEY_SPACE;
        case SDL_SCANCODE_APOSTROPHE:
            return BRLS_KBD_KEY_APOSTROPHE;
        case SDL_SCANCODE_COMMA:
            return BRLS_KBD_KEY_COMMA;
        case SDL_SCANCODE_MINUS:
            return BRLS_KBD_KEY_MINUS;
        case SDL_SCANCODE_PERIOD:
            return BRLS_KBD_KEY_PERIOD;
        case SDL_SCANCODE_SLASH:
            return BRLS_KBD_KEY_SLASH;
        case SDL_SCANCODE_0:
            return BRLS_KBD_KEY_0;
        case SDL_SCANCODE_SEMICOLON:
            return BRLS_KBD_KEY_SEMICOLON;
        case SDL_SCANCODE_EQUALS:
            return BRLS_KBD_KEY_EQUAL;
        case SDL_SCANCODE_LEFTBRACKET:
            return BRLS_KBD_KEY_LEFT_BRACKET;
        case SDL_SCANCODE_BACKSLASH:
            return BRLS_KBD_KEY_BACKSLASH;
        case SDL_SCANCODE_RIGHTBRACKET:
            return BRLS_KBD_KEY_RIGHT_BRACKET;
        case SDL_SCANCODE_GRAVE:
            return BRLS_KBD_KEY_GRAVE_ACCENT;
        case SDL_SCANCODE_INTERNATIONAL1:
            return BRLS_KBD_KEY_WORLD_1;

            /* Function keys */
        case SDL_SCANCODE_ESCAPE:
            return BRLS_KBD_KEY_ESCAPE;
        case SDL_SCANCODE_RETURN:
            return BRLS_KBD_KEY_ENTER;
        case SDL_SCANCODE_TAB:
            return BRLS_KBD_KEY_TAB;
        case SDL_SCANCODE_BACKSPACE:
            return BRLS_KBD_KEY_BACKSPACE;
        case SDL_SCANCODE_INSERT:
            return BRLS_KBD_KEY_INSERT;
        case SDL_SCANCODE_DELETE:
            return BRLS_KBD_KEY_DELETE;
        case SDL_SCANCODE_RIGHT:
            return BRLS_KBD_KEY_RIGHT;
        case SDL_SCANCODE_LEFT:
            return BRLS_KBD_KEY_LEFT;
        case SDL_SCANCODE_DOWN:
            return BRLS_KBD_KEY_DOWN;
        case SDL_SCANCODE_UP:
            return BRLS_KBD_KEY_UP;
        case SDL_SCANCODE_PAGEUP:
            return BRLS_KBD_KEY_PAGE_UP;
        case SDL_SCANCODE_PAGEDOWN:
            return BRLS_KBD_KEY_PAGE_DOWN;
        case SDL_SCANCODE_HOME:
            return BRLS_KBD_KEY_HOME;
        case SDL_SCANCODE_END:
            return BRLS_KBD_KEY_END;
        case SDL_SCANCODE_CAPSLOCK:
            return BRLS_KBD_KEY_CAPS_LOCK;
        case SDL_SCANCODE_SCROLLLOCK:
            return BRLS_KBD_KEY_SCROLL_LOCK;
        case SDL_SCANCODE_NUMLOCKCLEAR:
            return BRLS_KBD_KEY_NUM_LOCK;
        case SDL_SCANCODE_PRINTSCREEN:
            return BRLS_KBD_KEY_PRINT_SCREEN;
        case SDL_SCANCODE_PAUSE:
            return BRLS_KBD_KEY_PAUSE;
        case SDL_SCANCODE_KP_0:
            return BRLS_KBD_KEY_KP_0;
        case SDL_SCANCODE_KP_DECIMAL:
            return BRLS_KBD_KEY_KP_DECIMAL;
        case SDL_SCANCODE_KP_DIVIDE:
            return BRLS_KBD_KEY_KP_DIVIDE;
        case SDL_SCANCODE_KP_MULTIPLY:
            return BRLS_KBD_KEY_KP_MULTIPLY;
        case SDL_SCANCODE_KP_MINUS:
            return BRLS_KBD_KEY_KP_SUBTRACT;
        case SDL_SCANCODE_KP_PLUS:
            return BRLS_KBD_KEY_KP_ADD;
        case SDL_SCANCODE_KP_ENTER:
            return BRLS_KBD_KEY_KP_ENTER;
        case SDL_SCANCODE_KP_EQUALS:
            return BRLS_KBD_KEY_KP_EQUAL;
        case SDL_SCANCODE_LSHIFT:
            return BRLS_KBD_KEY_LEFT_SHIFT;
        case SDL_SCANCODE_LCTRL:
            return BRLS_KBD_KEY_LEFT_CONTROL;
        case SDL_SCANCODE_LALT:
            return BRLS_KBD_KEY_LEFT_ALT;
        case SDL_SCANCODE_LGUI:
            return BRLS_KBD_KEY_LEFT_SUPER;
        case SDL_SCANCODE_RSHIFT:
            return BRLS_KBD_KEY_RIGHT_SHIFT;
        case SDL_SCANCODE_RCTRL:
            return BRLS_KBD_KEY_RIGHT_CONTROL;
        case SDL_SCANCODE_RALT:
            return BRLS_KBD_KEY_RIGHT_ALT;
        case SDL_SCANCODE_RGUI:
            return BRLS_KBD_KEY_RIGHT_SUPER;
        case SDL_SCANCODE_MENU:
            return BRLS_KBD_KEY_MENU;
        default:
            return BRLS_KBD_KEY_UNKNOWN;
    }
}

static const SDL_Scancode brlsToSdlKeyboardScancode(BrlsKeyboardScancode scancode)
{
    if (scancode == BRLS_KBD_KEY_UNKNOWN)
        return SDL_SCANCODE_UNKNOWN;

    // 1 - 9
    if (scancode >= BRLS_KBD_KEY_1 && scancode <= BRLS_KBD_KEY_9)
        return (SDL_Scancode)(SDL_SCANCODE_1 + scancode - BRLS_KBD_KEY_1);

    // KP1 - KP9
    if (scancode >= BRLS_KBD_KEY_KP_1 && scancode <= BRLS_KBD_KEY_KP_9)
        return (SDL_Scancode)(SDL_SCANCODE_KP_1 + scancode - BRLS_KBD_KEY_KP_1);

    // A - Z
    if (scancode >= BRLS_KBD_KEY_A && scancode <= BRLS_KBD_KEY_Z)
        return (SDL_Scancode)(SDL_SCANCODE_A + scancode - BRLS_KBD_KEY_A);

    // F1 - F12
    if (scancode >= BRLS_KBD_KEY_F1 && scancode <= BRLS_KBD_KEY_F12)
        return (SDL_Scancode)(SDL_SCANCODE_F1 + scancode - BRLS_KBD_KEY_F1);

    switch (scancode)
    {
        case BRLS_KBD_KEY_UNKNOWN:
            return SDL_SCANCODE_UNKNOWN;

            /* Printable keys */
        case BRLS_KBD_KEY_SPACE:
            return SDL_SCANCODE_SPACE;
        case BRLS_KBD_KEY_APOSTROPHE:
            return SDL_SCANCODE_APOSTROPHE;
        case BRLS_KBD_KEY_COMMA:
            return SDL_SCANCODE_COMMA;
        case BRLS_KBD_KEY_MINUS:
            return SDL_SCANCODE_MINUS;
        case BRLS_KBD_KEY_PERIOD:
            return SDL_SCANCODE_PERIOD;
        case BRLS_KBD_KEY_SLASH:
            return SDL_SCANCODE_SLASH;
        case BRLS_KBD_KEY_0:
            return SDL_SCANCODE_0;
        case BRLS_KBD_KEY_SEMICOLON:
            return SDL_SCANCODE_SEMICOLON;
        case BRLS_KBD_KEY_EQUAL:
            return SDL_SCANCODE_EQUALS;
        case BRLS_KBD_KEY_LEFT_BRACKET:
            return SDL_SCANCODE_LEFTBRACKET;
        case BRLS_KBD_KEY_BACKSLASH:
            return SDL_SCANCODE_BACKSLASH;
        case BRLS_KBD_KEY_RIGHT_BRACKET:
            return SDL_SCANCODE_RIGHTBRACKET;
        case BRLS_KBD_KEY_GRAVE_ACCENT:
            return SDL_SCANCODE_GRAVE;
        case BRLS_KBD_KEY_WORLD_1:
            return SDL_SCANCODE_INTERNATIONAL1;

            /* Function keys */
        case BRLS_KBD_KEY_ESCAPE:
            return SDL_SCANCODE_ESCAPE;
        case BRLS_KBD_KEY_ENTER:
            return SDL_SCANCODE_RETURN;
        case BRLS_KBD_KEY_TAB:
            return SDL_SCANCODE_TAB;
        case BRLS_KBD_KEY_BACKSPACE:
            return SDL_SCANCODE_BACKSPACE;
        case BRLS_KBD_KEY_INSERT:
            return SDL_SCANCODE_INSERT;
        case BRLS_KBD_KEY_DELETE:
            return SDL_SCANCODE_DELETE;
        case BRLS_KBD_KEY_RIGHT:
            return SDL_SCANCODE_RIGHT;
        case BRLS_KBD_KEY_LEFT:
            return SDL_SCANCODE_LEFT;
        case BRLS_KBD_KEY_DOWN:
            return SDL_SCANCODE_DOWN;
        case BRLS_KBD_KEY_UP:
            return SDL_SCANCODE_UP;
        case BRLS_KBD_KEY_PAGE_UP:
            return SDL_SCANCODE_PAGEUP;
        case BRLS_KBD_KEY_PAGE_DOWN:
            return SDL_SCANCODE_PAGEDOWN;
        case BRLS_KBD_KEY_HOME:
            return SDL_SCANCODE_HOME;
        case BRLS_KBD_KEY_END:
            return SDL_SCANCODE_END;
        case BRLS_KBD_KEY_CAPS_LOCK:
            return SDL_SCANCODE_CAPSLOCK;
        case BRLS_KBD_KEY_SCROLL_LOCK:
            return SDL_SCANCODE_SCROLLLOCK;
        case BRLS_KBD_KEY_NUM_LOCK:
            return SDL_SCANCODE_NUMLOCKCLEAR;
        case BRLS_KBD_KEY_PRINT_SCREEN:
            return SDL_SCANCODE_PRINTSCREEN;
        case BRLS_KBD_KEY_PAUSE:
            return SDL_SCANCODE_PAUSE;
        case BRLS_KBD_KEY_KP_0:
            return SDL_SCANCODE_KP_0;
        case BRLS_KBD_KEY_KP_DECIMAL:
            return SDL_SCANCODE_KP_DECIMAL;
        case BRLS_KBD_KEY_KP_DIVIDE:
            return SDL_SCANCODE_KP_DIVIDE;
        case BRLS_KBD_KEY_KP_MULTIPLY:
            return SDL_SCANCODE_KP_MULTIPLY;
        case BRLS_KBD_KEY_KP_SUBTRACT:
            return SDL_SCANCODE_KP_MINUS;
        case BRLS_KBD_KEY_KP_ADD:
            return SDL_SCANCODE_KP_PLUS;
        case BRLS_KBD_KEY_KP_ENTER:
            return SDL_SCANCODE_KP_ENTER;
        case BRLS_KBD_KEY_KP_EQUAL:
            return SDL_SCANCODE_KP_EQUALS;
        case BRLS_KBD_KEY_LEFT_SHIFT:
            return SDL_SCANCODE_LSHIFT;
        case BRLS_KBD_KEY_LEFT_CONTROL:
            return SDL_SCANCODE_LCTRL;
        case BRLS_KBD_KEY_LEFT_ALT:
            return SDL_SCANCODE_LALT;
        case BRLS_KBD_KEY_LEFT_SUPER:
            return SDL_SCANCODE_LGUI;
        case BRLS_KBD_KEY_RIGHT_SHIFT:
            return SDL_SCANCODE_RSHIFT;
        case BRLS_KBD_KEY_RIGHT_CONTROL:
            return SDL_SCANCODE_RCTRL;
        case BRLS_KBD_KEY_RIGHT_ALT:
            return SDL_SCANCODE_RALT;
        case BRLS_KBD_KEY_RIGHT_SUPER:
            return SDL_SCANCODE_RGUI;
        case BRLS_KBD_KEY_MENU:
            return SDL_SCANCODE_MENU;
        default:
            return SDL_SCANCODE_UNKNOWN;
    }
}

// LT and RT do not exist here because they are axes
static const size_t SDL_BUTTONS_MAPPING[SDL_GAMEPAD_BUTTON_MAX] = {
    BUTTON_A, // SDL_CONTROLLER_BUTTON_A
    BUTTON_B, // SDL_CONTROLLER_BUTTON_B
    BUTTON_X, // SDL_CONTROLLER_BUTTON_X
    BUTTON_Y, // SDL_CONTROLLER_BUTTON_Y
    BUTTON_BACK, // SDL_CONTROLLER_BUTTON_BACK
    BUTTON_GUIDE, // SDL_CONTROLLER_BUTTON_GUIDE
    BUTTON_START, // SDL_CONTROLLER_BUTTON_START
    BUTTON_LSB, //    SDL_CONTROLLER_BUTTON_LEFTSTICK
    BUTTON_RSB, //    SDL_CONTROLLER_BUTTON_RIGHTSTICK
    BUTTON_LB, //    SDL_CONTROLLER_BUTTON_LEFTSHOULDER
    BUTTON_RB, //    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
    BUTTON_UP, //    SDL_CONTROLLER_BUTTON_DPAD_UP
    BUTTON_DOWN, //    SDL_CONTROLLER_BUTTON_DPAD_DOWN
    BUTTON_LEFT, //    SDL_CONTROLLER_BUTTON_DPAD_LEFT
    BUTTON_RIGHT, //    SDL_CONTROLLER_BUTTON_DPAD_RIGHT
};

static std::unordered_map<SDL_Scancode, int> keyboardKeys {};

static const size_t SDL_AXIS_MAPPING[SDL_GAMEPAD_AXIS_MAX] = {
    LEFT_X,
    LEFT_Y,
    RIGHT_X,
    RIGHT_Y,
    LEFT_Z,
    RIGHT_Z,
};

std::vector<std::pair<SDL_JoystickID, SDL_GameController*>> controllers;

static int mouseButtons[3] = { 0 };

static inline int getMouseButtonState(int buttonIndex)
{
    if (mouseButtons[buttonIndex - 1] == SDL_STICKY)
    {
        mouseButtons[buttonIndex - 1] = SDL_RELEASED;
        return SDL_PRESSED;
    }
    else
    {
        return mouseButtons[buttonIndex - 1];
    }
}

static inline int getKeyboardKeys(SDL_Scancode code)
{
    if (keyboardKeys.find(code) == keyboardKeys.end())
        return SDL_RELEASED;
    // Using SDL_STICKY to ensure that no input is lost
    // It's very helpful for devices with frame drops such as PSV
    if (keyboardKeys[code] == SDL_STICKY)
    {
        keyboardKeys[code] = SDL_RELEASED;
        return SDL_PRESSED;
    }
    else
    {
        return keyboardKeys[code];
    }
}

#if defined(__SDL3__)
static bool sdlEventWatcher(void* data, SDL_Event* event)
#else
static int sdlEventWatcher(void* data, SDL_Event* event)
#endif
{
    if (event->type == SDL_CONTROLLERDEVICEADDED)
    {
#if defined(__SDL3__)
        SDL_GameController* controller = SDL_GameControllerOpen(event->gdevice.which);
#else
        SDL_GameController* controller = SDL_GameControllerOpen(event->cdevice.which);
#endif
        if (controller)
        {
#if defined(__SDL3__)
            SDL_JoystickID jid = event->gdevice.which;
#else
            SDL_JoystickID jid = SDL_JoystickGetDeviceInstanceID(event->cdevice.which);
#endif
            Logger::info("Controller connected: {}/{}", jid, SDL_GameControllerName(controller));
            controllers.push_back({ jid, controller });
        }
    }
    else if (event->type == SDL_CONTROLLERDEVICEREMOVED)
    {
#if defined(__SDL3__)
        Logger::info("Controller disconnected: {}", event->gdevice.which);
        SDL_JoystickID jid = event->gdevice.which;
#else
        Logger::info("Controller disconnected: {}", event->cdevice.which);
        SDL_JoystickID jid = event->cdevice.which;
#endif
        controllers.erase(std::remove_if(controllers.begin(), controllers.end(), [jid](auto x) {
            if (x.first != jid)
                return false;

            SDL_GameControllerClose(x.second);
            return true;
        }), controllers.end());
    }
    else if (event->type == SDL_MOUSEBUTTONDOWN)
    {
        if (event->button.button <= 3)
            mouseButtons[event->button.button - 1] = SDL_PRESSED;
    }
    else if (event->type == SDL_MOUSEBUTTONUP)
    {
        if (event->button.button <= 3)
            mouseButtons[event->button.button - 1] = SDL_STICKY;
    }
    else if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP)
    {
#if defined(__SDL3__)
        keyboardKeys[event->key.scancode] = event->type == SDL_KEYDOWN ? SDL_PRESSED : SDL_STICKY;
#else
        keyboardKeys[event->key.keysym.scancode] = event->type == SDL_KEYDOWN ? SDL_PRESSED : SDL_STICKY;
#endif
    }
    Application::setActiveEvent(true);
#if defined(__SDL3__)
    return true;
#else
    return 0;
#endif
}

SDLInputManager::SDLInputManager(SDL_Window* window)
    : window(window)
{

    int32_t flags = SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER;
#ifndef __WINRT__
    flags |= SDL_INIT_HAPTIC;
#endif
#if defined(__SDL3__)
    if (!SDL_Init(flags))
#else
    if (SDL_Init(flags) < 0)
#endif
    {
        brls::fatal("Couldn't initialize joystick: " + std::string(SDL_GetError()));
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#if defined(__APPLE__) && !defined(PLATFORM_IOS) && !defined(PLATFORM_TVOS) && !defined(PLATFORM_VISIONOS)
#if defined(__SDL3__)
    SDL_SetHint(SDL_HINT_MOUSE_EMULATE_WARP_WITH_RELATIVE, "0");
#else
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
#endif
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "1");
#endif

    SDL_GameControllerUpdate();

#if defined(__SDL3__)
    int controllersCount = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&controllersCount);
#else
    int controllersCount = SDL_NumJoysticks();
#endif
    brls::Logger::info("joystick num: {}", controllersCount);

    for (int i = 0; i < controllersCount; i++)
    {
#if defined(__SDL3__)
        SDL_JoystickID jid = gamepads[i];
        Logger::info("sdl: joystick {}: \"{}\"", jid, SDL_GetGamepadNameForID(jid));
        SDL_GameController* controller = SDL_GameControllerOpen(jid);
#else
        SDL_JoystickID jid = SDL_JoystickGetDeviceInstanceID(i);
        Logger::info("sdl: joystick {}: \"{}\"", jid, SDL_JoystickNameForIndex(i));

        if (!SDL_IsGameController(i))
        {
            Logger::warning("sdl: joystick {} is not a game controller, skipping", jid);
            continue;
        }

        SDL_GameController* controller = SDL_GameControllerOpen(i);
#endif
        if (controller == nullptr)
        {
            Logger::warning("sdl: failed to open controller {}: {}", jid, SDL_GetError());
            continue;
        }

        controllers.push_back({ jid, controller });
    }
#if defined(__SDL3__)
    SDL_free(gamepads);
#endif

    SDL_AddEventWatch(sdlEventWatcher, this->window);

    Application::getRunLoopEvent()->subscribe([this]()
        {
        if(fabs(scrollOffset.y) < 1) scrollOffset.y = 0;
        else scrollOffset.y *= 0.8;
        if(fabs(scrollOffset.x) < 1) scrollOffset.x = 0;
        else scrollOffset.x *= 0.8;

        pointerOffset.x = 0;
        pointerOffset.y = 0; });

    init_device_rumble();
}

SDLInputManager::~SDLInputManager()
{
#if defined(__SDL3__)
    SDL_RemoveEventWatch(sdlEventWatcher, this->window);
#else
    SDL_DelEventWatch(sdlEventWatcher, this->window);
#endif
    for (auto i : controllers)
    {
        SDL_GameControllerClose(i.second);
    }
    controllers.clear();
}

short SDLInputManager::getControllersConnectedCount()
{
    return controllers.size();
}

void SDLInputManager::updateUnifiedControllerState(ControllerState* state)
{
    for (bool& button : state->buttons)
        button = false;

    for (float& axe : state->axes)
        axe = 0;

    for (size_t index = 0; index < controllers.size(); index++)
    {
        ControllerState localState {};
        updateControllerState(&localState, index);

        for (size_t i = 0; i < _BUTTON_MAX; i++)
            state->buttons[i] |= localState.buttons[i];

        for (size_t i = 0; i < _AXES_MAX; i++)
        {
            state->axes[i] += localState.axes[i];

            if (state->axes[i] < -1)
                state->axes[i] = -1;
            else if (state->axes[i] > 1)
                state->axes[i] = 1;
        }
    }

    // Add keyboard keys on top of gamepad buttons
    if (Application::isSwapInputKeys())
    {
        state->buttons[BUTTON_B] |= getKeyboardKeys(SDL_SCANCODE_KP_ENTER);
        state->buttons[BUTTON_B] |= getKeyboardKeys(SDL_SCANCODE_RETURN);
        state->buttons[BUTTON_A] |= getKeyboardKeys(SDL_SCANCODE_ESCAPE);
        state->buttons[BUTTON_A] |= getKeyboardKeys(SDL_SCANCODE_AC_BACK);
    }
    else
    {
        state->buttons[BUTTON_A] |= getKeyboardKeys(SDL_SCANCODE_KP_ENTER);
        state->buttons[BUTTON_A] |= getKeyboardKeys(SDL_SCANCODE_RETURN);
        state->buttons[BUTTON_B] |= getKeyboardKeys(SDL_SCANCODE_ESCAPE);
        state->buttons[BUTTON_B] |= getKeyboardKeys(SDL_SCANCODE_AC_BACK);
    }

    // Android tv remote control
    state->buttons[BUTTON_X] |= getKeyboardKeys(SDL_SCANCODE_MENU);

    state->buttons[BUTTON_UP] |= getKeyboardKeys(SDL_SCANCODE_UP);
    state->buttons[BUTTON_RIGHT] |= getKeyboardKeys(SDL_SCANCODE_RIGHT);
    state->buttons[BUTTON_DOWN] |= getKeyboardKeys(SDL_SCANCODE_DOWN);
    state->buttons[BUTTON_LEFT] |= getKeyboardKeys(SDL_SCANCODE_LEFT);

    state->buttons[BUTTON_NAV_UP] |= state->buttons[BUTTON_UP];
    state->buttons[BUTTON_NAV_RIGHT] |= state->buttons[BUTTON_RIGHT];
    state->buttons[BUTTON_NAV_DOWN] |= state->buttons[BUTTON_DOWN];
    state->buttons[BUTTON_NAV_LEFT] |= state->buttons[BUTTON_LEFT];
}

void SDLInputManager::updateControllerState(ControllerState* state, int controller)
{
    if (controllers.size() <= controller) return;

    SDL_GameController* c = controllers[controller].second;
    if (c == nullptr || !SDL_GameControllerGetAttached(c)) return;

    for (size_t i = 0; i < SDL_GAMEPAD_BUTTON_MAX; i++)
    {
        // Translate SDL gamepad to borealis controller
        size_t brlsButton          = SDL_BUTTONS_MAPPING[i];
        state->buttons[brlsButton] = (bool)SDL_GameControllerGetButton(c, (SDL_GameControllerButton)i);
    }

    state->buttons[BUTTON_LT] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 3276.7f;
    state->buttons[BUTTON_RT] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 3276.7f;

    state->buttons[BUTTON_NAV_UP]    = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) < -16383.5f || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY) < -16383.5f || state->buttons[BUTTON_UP];
    state->buttons[BUTTON_NAV_RIGHT] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) > 16383.5f || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX) > 16383.5f || state->buttons[BUTTON_RIGHT];
    state->buttons[BUTTON_NAV_DOWN]  = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) > 16383.5f || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY) > 16383.5f || state->buttons[BUTTON_DOWN];
    state->buttons[BUTTON_NAV_LEFT]  = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) < -16383.5f || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX) < -16383.5f || state->buttons[BUTTON_LEFT];

    for (size_t i = 0; i < SDL_GAMEPAD_AXIS_MAX; i++)
    {
        state->axes[SDL_AXIS_MAPPING[i]] = normalizeSdlAxis(SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)i));
    }
}

bool SDLInputManager::getKeyboardKeyState(BrlsKeyboardScancode key)
{
    return getKeyboardKeys(brlsToSdlKeyboardScancode(key));
}

void SDLInputManager::updateTouchStates(std::vector<RawTouchState>* states)
{
#if defined(__SDL3__)
    int devices = 0;
    SDL_TouchID* touchDevices = SDL_GetTouchDevices(&devices);
#else
    int devices = SDL_GetNumTouchDevices();
#endif
    if (devices == 0)
    {
#if defined(__SDL3__)
        SDL_free(touchDevices);
#endif
        return;
    }

    for (int deviceID = 0; deviceID < devices; deviceID++)
    {
#if defined(TVOS)
#if defined(__SDL3__)
        SDL_TouchDeviceType deviceType = SDL_GetTouchDeviceType(touchDevices[deviceID]);
#else
        SDL_TouchDeviceType deviceType = SDL_GetTouchDeviceType(deviceID);
#endif
        if (deviceType == SDL_TOUCH_DEVICE_INVALID) continue;
#endif
        
#if defined(__SDL3__)
        SDL_TouchID device = touchDevices[deviceID];
        int touchesCount = 0;
        SDL_Finger** fingers = SDL_GetTouchFingers(device, &touchesCount);
#else
        SDL_TouchID device = SDL_GetTouchDevice(deviceID);
        int touchesCount   = SDL_GetNumTouchFingers(device);
#endif
        for (int touchID = 0; touchID < touchesCount; touchID++)
        {
#if defined(__SDL3__)
            SDL_Finger* finger = fingers[touchID];
#else
            SDL_Finger* finger = SDL_GetTouchFinger(device, touchID);
#endif

            RawTouchState state;
            state.pressed    = true;
            state.fingerId   = (int)finger->id;
            state.position.x = Application::contentWidth * finger->x;
            state.position.y = Application::contentHeight * finger->y;
            state.pressure   = finger->pressure;
            states->push_back(state);
        }
#if defined(__SDL3__)
        SDL_free(fingers);
#endif
    }
#if defined(__SDL3__)
    SDL_free(touchDevices);
#endif
}

void SDLInputManager::updateMouseStates(RawMouseState* state)
{
#if defined(__SDL3__)
    float x, y;
#else
    int x, y;
#endif
    SDL_GetMouseState(&x, &y);

    state->leftButton   = getMouseButtonState(SDL_BUTTON_LEFT);
    state->middleButton = getMouseButtonState(SDL_BUTTON_MIDDLE);
    state->rightButton  = getMouseButtonState(SDL_BUTTON_RIGHT);

#if defined(BOREALIS_USE_D3D11)
    // D3D11 keeps the app framebuffer in pixel space while SDL reports the mouse in logical window space.
    double scaleFactor = brls::Application::getPlatform()->getVideoContext()->getScaleFactor();
    state->position.x  = x * scaleFactor / Application::windowScale;
    state->position.y  = y * scaleFactor / Application::windowScale;
#elif defined(BOREALIS_USE_METAL)
    // Metal keeps pointer coordinates in logical window space.
    state->position.x = x / Application::windowScale;
    state->position.y = y / Application::windowScale;
#else
    double scaleFactor = brls::Application::getPlatform()->getVideoContext()->getScaleFactor();
    state->position.x  = x * scaleFactor / Application::windowScale;
    state->position.y  = y * scaleFactor / Application::windowScale;
#endif

    state->offset = pointerOffset;
    state->scroll = scrollOffset;
}

void SDLInputManager::setPointerLock(bool lock)
{
    pointerLocked = lock;
    pointerOffset       = { 0, 0 };
    pointerOffsetBuffer = { 0, 0 };

#if defined(__SDL3__)
    if (!SDL_SetWindowRelativeMouseMode(window, lock))
#else
    if (SDL_SetRelativeMouseMode(lock ? SDL_TRUE : SDL_FALSE) < 0)
#endif
    {
        brls::Logger::warning("sdl: failed to set relative mouse mode: {}", SDL_GetError());
    }

#if defined(__SDL3__)
    lock ? SDL_HideCursor() : SDL_ShowCursor();
#else
    SDL_ShowCursor(lock ? SDL_FALSE : SDL_TRUE);
#endif
}

void SDLInputManager::runloopStart()
{
    pointerOffset         = pointerOffsetBuffer;
    pointerOffsetBuffer.x = 0;
    pointerOffsetBuffer.y = 0;
}

void SDLInputManager::sendRumble(unsigned short controller, unsigned short lowFreqMotor, unsigned short highFreqMotor)
{
    if (controllers.size() <= controller) return;

#if defined(PLATFORM_ANDROID) && !defined(__SDL3__)
    if (SDL_IsAndroidTV())
    {
        device_rumble(lowFreqMotor, highFreqMotor);
        return;
    }
#endif

    SDL_GameController* c = controllers[controller].second;
    if (c == nullptr || !SDL_GameControllerGetAttached(c)) return;

#if defined(__SDL3__)
    if (!SDL_GameControllerRumble(c, lowFreqMotor, highFreqMotor, 30000)) {
#else
    if (!SDL_GameControllerHasRumble(c)) {
#endif
        device_rumble(lowFreqMotor, highFreqMotor);
        return;
    }

#if !defined(__SDL3__)
    SDL_GameControllerRumble(c, lowFreqMotor, highFreqMotor, 30000);
#endif
}

void SDLInputManager::sendRumble(unsigned short controller, unsigned short lowFreqMotor, unsigned short highFreqMotor, unsigned short leftTriggerFreqMotor, unsigned short rightTriggerFreqMotor)
{
    if (controllers.size() <= controller) return;

#if defined(PLATFORM_ANDROID) && !defined(__SDL3__)
    if (SDL_IsAndroidTV())
    {
        device_rumble(lowFreqMotor, highFreqMotor);
        return;
    }
#endif

    SDL_GameController* c = controllers[controller].second;
    if (c == nullptr || !SDL_GameControllerGetAttached(c)) return;

#if defined(__SDL3__)
    if (!SDL_GameControllerRumble(c, lowFreqMotor, highFreqMotor, 30000)) {
#else
    if (!SDL_GameControllerHasRumble(c)) {
#endif
        device_rumble(lowFreqMotor, highFreqMotor);
        return;
    }

#if !defined(__SDL3__)
    SDL_GameControllerRumble(c, lowFreqMotor, highFreqMotor, 30000);
#endif
    SDL_GameControllerRumbleTriggers(c, leftTriggerFreqMotor, rightTriggerFreqMotor, 30000);
}

void SDLInputManager::updateMouseMotion(SDL_MouseMotionEvent event)
{
    if (pointerLocked)
    {
        getMouseCusorOffsetChanged()->fire(Point(float(event.xrel), float(event.yrel)));
    }
}

void SDLInputManager::updateMouseWheel(SDL_MouseWheelEvent event)
{
#if defined(__SDL3__)
    float preciseX = event.x;
    float preciseY = event.y;
#else
    float preciseX = event.preciseX;
    float preciseY = event.preciseY;
#endif
    if (event.direction == SDL_MOUSEWHEEL_FLIPPED)
    {
        preciseX = -preciseX;
        preciseY = -preciseY;
    }

    if (preciseX == 0.0f && preciseY == 0.0f) return;

#if defined(PLATFORM_DESKTOP)
    constexpr float UI_SCROLL_DISTANCE = 30.0f;
#else
    constexpr float UI_SCROLL_DISTANCE = 4.0f;
#endif
    this->scrollOffset.x += preciseX * UI_SCROLL_DISTANCE;
    this->scrollOffset.y += preciseY * UI_SCROLL_DISTANCE;

    // Scroll events use wheel units across every backend. Consumers that need
    // Win32/Moonlight wheel deltas perform the 120-unit conversion themselves.
    this->getMouseScrollOffsetChanged()->fire(Point(preciseX, preciseY));
}

#if defined(__SDL3__)
void SDLInputManager::updateControllerSensorsUpdate(SDL_GamepadSensorEvent event)
#else
void SDLInputManager::updateControllerSensorsUpdate(SDL_ControllerSensorEvent event)
#endif
{
    auto id = event.which;
    SensorEvent state;

    switch (event.sensor)
    {
        case SDL_SENSOR_ACCEL:
            state = SensorEvent { static_cast<int>(id), SensorEventType::ACCEL, { event.data[0], event.data[1], event.data[2] },
#if defined(__SDL3__)
                static_cast<uint32_t>(SDL_NS_TO_MS(event.sensor_timestamp))
#else
                event.timestamp
#endif
            };
            getControllerSensorStateChanged()->fire(state);
            Application::setActiveEvent(true);
            break;
        case SDL_SENSOR_GYRO:
            state = SensorEvent { static_cast<int>(id), SensorEventType::GYRO, { event.data[0], event.data[1], event.data[2] },
#if defined(__SDL3__)
                static_cast<uint32_t>(SDL_NS_TO_MS(event.sensor_timestamp))
#else
                event.timestamp
#endif
            };
            getControllerSensorStateChanged()->fire(state);
            Application::setActiveEvent(true);
            break;
    }
}

void SDLInputManager::updateKeyboardState(SDL_KeyboardEvent event)
{
    auto* self = (SDLInputManager*)Application::getPlatform()->getInputManager();
    KeyState state {};
#if defined(__SDL3__)
    state.key     = sdlToBrlsKeyboardScancode(event.scancode);
#else
    state.key     = sdlToBrlsKeyboardScancode(event.keysym.scancode);
#endif
    state.pressed = event.type == SDL_KEYDOWN;
#if defined(__SDL3__)
    const auto mods = event.mod;
#else
    const auto mods = event.keysym.mod;
#endif
    state.mods |= mods & KMOD_SHIFT ? BRLS_KBD_MODIFIER_SHIFT : 0;
    state.mods |= mods & KMOD_CTRL ? BRLS_KBD_MODIFIER_CTRL : 0;
    state.mods |= mods & KMOD_ALT ? BRLS_KBD_MODIFIER_ALT : 0;
    state.mods |= mods & KMOD_GUI ? BRLS_KBD_MODIFIER_META : 0;

    self->getKeyboardKeyStateChanged()->fire(state);
    Application::setActiveEvent(true);
}

};
