#include "script_runner.h"

namespace pivid {

namespace {

class ScriptRunnerDef : public ScriptRunner {
  public:
    virtual void update(Script const& script) {
    }

    void init(
        std::shared_ptr<DisplayDriver> driver,
        std::shared_ptr<UnixSystem> sys,
        std::function<std::unique_ptr<FrameLoader>(std::string const&)> ml,
        std::function<std::unique_ptr<FramePlayer>(uint32_t, DisplayMode)> mp
    ) {
        this->driver = driver;
        this->sys = sys;
        this->make_l = std::move(ml);
        this->make_p = std::move(mp);
    }

  private:
    std::shared_ptr<DisplayDriver> driver;
    std::shared_ptr<UnixSystem> sys;
    std::function<std::unique_ptr<FrameLoader>(std::string const&)> make_l;
    std::function<std::unique_ptr<FramePlayer>(uint32_t, DisplayMode)> make_p;

    std::map<std::string, std::unique_ptr<FrameLoader>> loaders;
    
};

}  // anonymous namespace

std::unique_ptr<ScriptRunner> make_script_runner(
    std::shared_ptr<DisplayDriver> driver,
    std::shared_ptr<UnixSystem> sys,
    std::function<std::unique_ptr<FrameLoader>(std::string const&)> make_l,
    std::function<std::unique_ptr<FramePlayer>(uint32_t, DisplayMode)> make_p
) {
    auto runner = std::make_unique<ScriptRunnerDef>();
    return runner;
}

}  // namespace pivid
