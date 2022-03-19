#pragma once

#include "script.h"

namespace pivid {

class ScriptRunner {
  public:
    virtual void update(Script const&) = 0;
};

std::unique_ptr<ScriptRunner> make_script_runner(
    DisplayDriver*,
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> =
        open_media_decoder
);

}  // namespace pivid
