#pragma once
#include <cstdint>

namespace brls
{
// A held video frame need not be presented on every refresh when VRR is enabled.
// The UI/input loop still runs, and a keepalive refresh bounds overlay staleness.
class FramePresentation
{
  public:
    void setFrameDriven(bool enabled)
    {
        frameDriven = enabled;
        lastPresent = 0;
    }
    bool isFrameDriven() const { return frameDriven; }
    bool shouldPresent(bool video, bool changed, bool resized, uint64_t now) const
    {
        return !frameDriven || !video || changed || resized || !lastPresent
            || now - lastPresent >= 34000000;
    }
    void presented(uint64_t now) { lastPresent = now; }
    uint64_t waitNs(uint64_t now) const
    {
        const uint64_t age  = now - lastPresent;
        const uint64_t left = age < 34000000 ? 34000000 - age : 0;
        return left < 8000000 ? left : 8000000;
    }

  private:
    bool frameDriven     = false;
    uint64_t lastPresent = 0;
};
}
