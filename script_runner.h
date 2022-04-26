// Interface to execute a parsed play script (which may change),
// using FrameLoader and FramePlayer.

#pragma once

#include <map>

#include "frame_loader.h"
#include "frame_player.h"
#include "media_decoder.h"
#include "script_data.h"
#include "unix_system.h"

namespace pivid {

// Interface to an asynchronous thread that executes a play script,
// loading frames and scheduling playback per the script's contents.
class ScriptRunner {
  public:
    virtual ~ScriptRunner() = default;

    // Switch to the specified play script.
    virtual void update(Script const&) = 0;

    // Returns metadata for a file (relative to the media root), with caching.
    virtual MediaFileInfo const& file_info(std::string const&) = 0;
};

// Resources and parameters need to start a ScriptRunner.
struct ScriptContext {
    std::shared_ptr<DisplayDriver> driver;
    std::shared_ptr<UnixSystem> sys;
    std::shared_ptr<SyncFlag> notify;  // Flagged on any status change.
    std::string root_dir;              // Media root for all file references.
    std::string file_base;             // Base for relative filenames.
    FrameLoaderContext loader_cx;
    std::function<std::unique_ptr<FrameLoader>(FrameLoaderContext)> loader_f;
    std::function<std::unique_ptr<FramePlayer>(uint32_t)> player_f;
};

// Creates a script runner instance for given parameters.
std::unique_ptr<ScriptRunner> make_script_runner(ScriptContext);

}  // namespace pivid
