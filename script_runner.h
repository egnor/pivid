#pragma once

#include "script.h"

namespace pivid {

class ScriptRunner {
  public:
    virtual void update(Script const&) = 0;
};

std::unique_ptr<ScriptRunner> make_script_runner(DisplayDriver*);

}  // namespace pivid
