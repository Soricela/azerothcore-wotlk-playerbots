#include "NoFatigue.h"
#include "Config.h"
#include "Log.h"
#include "ScriptMgr.h"

namespace
{
    bool g_FatigueEnabled = true;
}

namespace Acore::NoFatigue
{
    bool IsFatigueEnabled()
    {
        return g_FatigueEnabled;
    }

    void LoadConfig()
    {
        g_FatigueEnabled = sConfigMgr->GetOption<bool>("NoFatigue.EnableFatigue", true);
        LOG_INFO("module", "mod-no-fatigue: NoFatigue.EnableFatigue = {}", g_FatigueEnabled ? "true" : "false");
    }
}

class NoFatigueWorldScript : public WorldScript
{
public:
    NoFatigueWorldScript() : WorldScript("NoFatigueWorldScript") { }

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        Acore::NoFatigue::LoadConfig();
    }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        Acore::NoFatigue::LoadConfig();
    }
};

void Addmod_no_fatigueScripts()
{
    new NoFatigueWorldScript();
}
