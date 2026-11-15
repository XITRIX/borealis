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

#pragma once

#include <borealis/platforms/sdl/sdl.hpp>

#include <borealis/core/video.hpp>

namespace brls
{

#ifdef BOREALIS_USE_D3D11
class D3D11Context;
#endif

// SDL Video Context
class SDLVideoContext : public VideoContext
{
  public:
    SDLVideoContext(std::string windowTitle, uint32_t windowWidth, uint32_t windowHeight, float windowXPos, float windowYPos);
    ~SDLVideoContext() override;

    NVGcontext* getNVGContext() override;

    void clear(NVGcolor color) override;
    void beginFrame() override;
    void endFrame() override;
#ifdef __SWITCH2__
    void waitIdle() override;
#endif
    void setSwapInterval(int interval) override;
    void resetState() override;
    void fullScreen(bool fs) override;

    SDL_Window* getSDLWindow();

  #ifdef BOREALIS_USE_D3D11
    D3D11Context* getD3D11Context();
  #endif

    double getScaleFactor() override;

  private:
    SDL_Window* window     = nullptr;
    NVGcontext* nvgContext = nullptr;
};

} // namespace brls
