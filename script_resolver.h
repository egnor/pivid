#pragma once

#include <map>

#include "script_data.h"
#include "unix_system.h"

namespace pivid {

class ScriptResolver {
  public:
    virtual ~ScriptResolver() = default;
    virtual resolve(Script*) = 0;
};

struct ResolverContext {
    std::string root_dir;
    std::string origin_file;
    double start_time;
};

std::unique_ptr<ScriptResolver> make_script_resolver(ResolverContext);

}  // namespace pivid
