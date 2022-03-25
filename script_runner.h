#pragma once

#include <map>

#include "frame_loader.h"
#include "frame_player.h"
#include "script_data.h"
#include "unix_system.h"

namespace pivid {

class ScriptRunner {
  public:
    virtual ~ScriptRunner() = default;
    virtual void update(Script const&, std::shared_ptr<ThreadSignal> = {}) = 0;
};

std::unique_ptr<ScriptRunner> make_script_runner(
    std::shared_ptr<DisplayDriver>,
    std::shared_ptr<UnixSystem> = global_system(),
    std::function<std::unique_ptr<FrameLoader>(std::string const&)> = {},
    std::function<std::unique_ptr<FramePlayer>(uint32_t, DisplayMode)> = {}
);

}  // namespace pivid
