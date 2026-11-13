#pragma once
#include <memory>
#include <utility>
#include <vector>

namespace brls
{
// Keep externally owned resources alive until a submission's fence completes.
class FrameResources
{
  public:
    void retain(std::shared_ptr<void> resource) { resources.push_back(std::move(resource)); }
    void release() { resources.clear(); }

  private:
    std::vector<std::shared_ptr<void>> resources;
};
}
