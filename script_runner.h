#pragma once

#include <map>

#include "frame_loader.h"
#include "frame_player.h"
#include "media_decoder.h"
#include "script_data.h"
#include "unix_system.h"

namespace pivid {

class ScriptRunner {
  public:
    virtual ~ScriptRunner() = default;
    virtual void update(Script const&) = 0;
    virtual MediaFileInfo const& file_info(std::string const&) = 0;
};

struct ScriptContext {
    std::shared_ptr<DisplayDriver> driver;
    std::shared_ptr<UnixSystem> sys;
    std::shared_ptr<SyncFlag> notify;
    std::string root_dir;
    std::string file_base;
    FrameLoaderContext loader_cx;
    std::function<std::unique_ptr<FrameLoader>(FrameLoaderContext)> loader_f;
    std::function<std::unique_ptr<FramePlayer>(uint32_t)> player_f;
};

std::unique_ptr<ScriptRunner> make_script_runner(ScriptContext);

}  // namespace pivid
