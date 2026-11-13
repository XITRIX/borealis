#pragma once

namespace brls
{
// HID touch coordinates use the 1280x720 sensor space independently of the
// drawable size. Convert to physical pixels before applying the UI transform.
constexpr float switchTouchCoordinate(float coordinate, float sensorExtent,
    float framebufferExtent, float uiScale)
{
    return coordinate * framebufferExtent / (sensorExtent * uiScale);
}
}
