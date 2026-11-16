#pragma once
#include <cstddef>
#include <switch.h>

#include <borealis/platforms/switch/half_joycon_mapping.hpp>
namespace brls
{
inline HalfJoyConSide halfJoyConSide(u32 style)
{
    if (style & HidNpadStyleSet_NpadFullCtrl)
        return HalfJoyConSide::NONE;
    if (style & HidNpadStyleTag_NpadJoyLeft)
        return HalfJoyConSide::LEFT;
    if (style & HidNpadStyleTag_NpadJoyRight)
        return HalfJoyConSide::RIGHT;
    return HalfJoyConSide::NONE;
}

inline std::array<u64, 12> halfJoyConButtonMasks(HalfJoyConSide side)
{
    if (side == HalfJoyConSide::LEFT)
        return { HidNpadButton_Right, HidNpadButton_Down, HidNpadButton_Left, HidNpadButton_Up,
            HidNpadButton_StickL, HidNpadButton_LeftSL, HidNpadButton_LeftSR, HidNpadButton_Minus,
            0, HidNpadButton_L, HidNpadButton_ZL, 0 };
    return { HidNpadButton_Y, HidNpadButton_X, HidNpadButton_A, HidNpadButton_B,
        HidNpadButton_StickR, HidNpadButton_RightSL, HidNpadButton_RightSR, HidNpadButton_Plus,
        0, HidNpadButton_R, HidNpadButton_ZR, 0 };
}

inline void applyHalfJoyConMapping(ControllerState* state, const HalfJoyConMapping& mapping,
    HalfJoyConSide side, u64 buttons, bool systemPressed, bool chatPressed)
{
    const bool menuGuide = state->buttons[BUTTON_GUIDE];
    for (int i = 0; i < BUTTON_NAV_UP; ++i)
        state->buttons[i] = false;
    const auto masks = halfJoyConButtonMasks(side);
    for (std::size_t i = 0; i < mapping.size(); ++i)
    {
        bool pressed = (buttons & masks[i]) != 0;
        if (i == static_cast<std::size_t>(HalfJoyConButton::SYSTEM))
            pressed = systemPressed;
        if (i == static_cast<std::size_t>(HalfJoyConButton::CHAT))
        {
#ifdef __SWITCH2__
            pressed = side == HalfJoyConSide::RIGHT && chatPressed;
#else
            pressed = false;
            (void)chatPressed;
#endif
        }
        if (pressed && mapping[i] >= 0 && validHalfJoyConAction(mapping[i]))
            state->buttons[mapping[i]] = true;
    }
    state->buttons[BUTTON_GUIDE] |= menuGuide;
}
} // namespace brls
