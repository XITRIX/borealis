#pragma once

#include <switch.h>

#include <algorithm>
#include <cmath>

namespace brls
{

struct SwitchMouseState
{
    float x = 0, y = 0;
    float delta_x = 0, delta_y = 0;
    float wheel_delta_x = 0, wheel_delta_y = 0;
    u32 buttons = 0, attributes = 0;
    // Menu gestures consume pixels; stream events consume wheel clicks.
    float menu_scroll_x = 0, menu_scroll_y = 0;
};

// HID pointer coordinates use a fixed area; the UI maps it to its current scale.
class SwitchMouse
{
  public:
    static constexpr int WIDTH                           = 1280;
    static constexpr int HEIGHT                          = 720;
    static constexpr float JOYCON_SCROLL_UNITS_PER_CLICK = 120.0f;

    Result initialize()
    {
        hidInitializeMouse();
#ifdef __SWITCH2__
        padInitializeDefault(&this->pointerPad);
        Result rc = padPointerInitialize(&this->pointer, WIDTH, HEIGHT);
        if (R_FAILED(rc))
            return rc;
        this->pointerInitialized = true;

        // L/R clicks, ZL/ZR right-clicks, and pressing the stick middle-clicks.
        const HidNmouseButtonConfigJoyLeftJcms left   = { { 1, 2, 0, 0, 0, 0, 3, 0 } };
        const HidNmouseButtonConfigJoyRightJcms right = { { 1, 2, 0, 0, 0, 0, 3, 0 } };
        rc                                            = hidSetNmouseButtonConfigJoyLeftJcms(&left);
        if (R_SUCCEEDED(rc))
            rc = hidSetNmouseButtonConfigJoyRightJcms(&right);
        if (R_FAILED(rc))
        {
            padPointerExit(&this->pointer);
            this->pointerInitialized = false;
        }
        return rc;
#else
        return 0;
#endif
    }

    ~SwitchMouse()
    {
#ifdef __SWITCH2__
        if (this->pointerInitialized)
            padPointerExit(&this->pointer);
#endif
    }

    SwitchMouseState update()
    {
        HidMouseState usb       = { };
        const bool connected    = hidGetMouseStates(&usb, 1) > 0 && (usb.attributes & HidMouseAttribute_IsConnected);
        const bool fresh        = connected && (!this->usbConnected || usb.sampling_number != this->usbSamplingNumber);
        this->usbConnected      = connected;
        this->usbSamplingNumber = usb.sampling_number;
        if (!connected)
            usb = { };
        else if (!fresh)
            usb.delta_x = usb.delta_y = usb.wheel_delta_x = usb.wheel_delta_y = 0;

        const bool usbActivity = usb.delta_x || usb.delta_y || usb.wheel_delta_x || usb.wheel_delta_y || usb.buttons != this->usbButtons;
        this->usbButtons       = usb.buttons;
        if (usbActivity)
            this->joyConCursor = false;

        SwitchMouseState state = { static_cast<float>(usb.x), static_cast<float>(usb.y),
            static_cast<float>(usb.delta_x), static_cast<float>(usb.delta_y),
            static_cast<float>(usb.wheel_delta_x), static_cast<float>(usb.wheel_delta_y), usb.buttons, usb.attributes };
        state.menu_scroll_x    = usb.wheel_delta_x;
        state.menu_scroll_y    = usb.wheel_delta_y;

#ifdef __SWITCH2__
        if (this->pointerInitialized)
        {
            padUpdate(&this->pointerPad);
            // The SDK consumes sample history once here, never in updateMouseStates.
            padPointerUpdate(&this->pointer, &this->pointerPad);
            const u32 previousButtons = this->mousePointer.buttons_cur;
            this->mousePointer        = this->pointer;
            HidNmouseState sample     = { };
            size_t count              = 0;
            const bool tracking       = this->pointer.active
                && R_SUCCEEDED(hidGetNmouseStates(this->pointer.current, &sample, 1, &count))
                && count > 0 && (sample.flags & 2);
            // A sample/handle alone is not mouse mode. Suppress ordinary pad
            // clicks while lifted, but keep a drag held until its release.
            const bool sameDevice          = this->pointer.current.handle == this->previousHandle;
            this->mousePointer.active      = this->pointer.active && (tracking || (sameDevice && this->pointerEngaged && this->pointer.buttons_cur));
            this->pointerEngaged           = this->mousePointer.active;
            this->previousHandle           = this->pointer.current.handle;
            this->mousePointer.buttons_old = previousButtons;
            if (!this->mousePointer.active)
            {
                this->mousePointer.buttons_cur = 0;
                this->mousePointer.delta_x = this->mousePointer.delta_y = this->mousePointer.wheel_delta_y = 0;
            }
            if (this->mousePointer.active)
            {
                const bool activity = this->mousePointer.delta_x || this->mousePointer.delta_y || this->mousePointer.wheel_delta_y
                    || this->mousePointer.buttons_cur != this->mousePointer.buttons_old;
                if (activity || !connected)
                    this->joyConCursor = true;
                if (this->joyConCursor)
                {
                    // Integrate relative motion so the SDK's absolute pointer
                    // cannot bypass sensitivity or lose fractional movement.
                    this->x = std::clamp(this->x + this->mousePointer.delta_x * JOYCON_SPEED * this->cursorSpeed, 0.0f, float(WIDTH - 1));
                    this->y = std::clamp(this->y + this->mousePointer.delta_y * JOYCON_SPEED * this->cursorSpeed, 0.0f, float(HEIGHT - 1));
                }
                // Streaming applies its existing speed multiplier after this
                // event; cursorSpeed belongs only to the local UI position.
                state.delta_x += this->mousePointer.delta_x * JOYCON_SPEED;
                state.delta_y += this->mousePointer.delta_y * JOYCON_SPEED;
                // Native Joy-Con scrolling is much finer than a USB wheel
                // click. Preserve fractions before applying application gain.
                state.wheel_delta_y += this->mousePointer.wheel_delta_y / JOYCON_SCROLL_UNITS_PER_CLICK;
                state.menu_scroll_y += this->mousePointer.wheel_delta_y;
                state.buttons |= this->mousePointer.buttons_cur;
                state.attributes |= HidMouseAttribute_IsConnected;
            }
            else
                this->joyConCursor = false;
        }
#endif
        if (connected && !this->joyConCursor)
        {
            this->x = usb.x;
            this->y = usb.y;
        }
        state.x = this->x;
        state.y = this->y;
        return state;
    }

#ifdef __SWITCH2__
    const PadPointer& joyCon() const { return this->mousePointer; }

    void setCursorSpeed(float speed)
    {
        this->cursorSpeed = std::isfinite(speed) ? std::clamp(speed, 0.5f, 2.0f) : 1.0f;
    }

    PadPointerSide mouseSide(const PadState* pad) const
    {
        if (!this->mousePointer.active)
            return PadPointerSide_None;
        HidNpadIdType id;
        if (R_FAILED(hidGetNmouseNpadIdType(this->pointer.current, &id)))
            return PadPointerSide_None;
        const bool matches = id == HidNpadIdType_Handheld ? pad->active_handheld
                                                          : id <= HidNpadIdType_No8 && (pad->active_id_mask & (1U << id));
        return matches ? this->mousePointer.side : PadPointerSide_None;
    }
#endif

  private:
    bool usbConnected     = false;
    bool joyConCursor     = false;
    u64 usbSamplingNumber = 0;
    u32 usbButtons        = 0;
    float x               = WIDTH / 2.0f;
    float y               = HEIGHT / 2.0f;
#ifdef __SWITCH2__
    static constexpr float JOYCON_SPEED = 2.0f / 3.0f;
    float cursorSpeed                   = 1.0f;
    bool pointerInitialized             = false;
    PadState pointerPad                 = { };
    PadPointer pointer                  = { };
    PadPointer mousePointer             = { };
    bool pointerEngaged                 = false;
    u64 previousHandle                  = 0;
#endif
};

} // namespace brls
