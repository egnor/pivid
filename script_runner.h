#pragma once

#include <map>

#include "frame_loader.h"
#include "frame_player.h"
#include "script_data.h"
#include "unix_system.h"

namespace pivid {

struct ScriptStatus {
    double update_time;
    std::map<std::string, DisplayMode> screen_mode;
    std::map<std::string, double> media_eof;
};

class ScriptRunner {
  public:
    virtual ~ScriptRunner() = default;
    virtual ScriptStatus update(
        Script const&, std::shared_ptr<SyncFlag> = {}
    ) = 0;
};

struct ScriptContext {
    std::shared_ptr<DisplayDriver> driver;
    std::shared_ptr<UnixSystem> sys;
    std::function<std::unique_ptr<FrameLoader>(std::string const&)> loader_f;
    std::function<std::unique_ptr<FramePlayer>(uint32_t, DisplayMode)> player_f;
    std::string root_dir;
    std::string file_base;
    double start_time = 0.0;
};

std::unique_ptr<ScriptRunner> make_script_runner(ScriptContext);

}  // namespace pivid
