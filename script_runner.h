#pragma once

#include "script_data.h"

namespace pivid {

class ScriptRunner {
  public:
    virtual void set_script(Script const&) = 0;
    virtual void update() = 0;
};

std::unique_ptr<ScriptRunner> make_script_runner(
    DisplayDriver*,
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> =
        open_media_decoder
);

}  // namespace pivid
