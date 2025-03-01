#include "sdk.h"

void c_interfaces::gather()
{
    const auto client = g_modules.get("client.dll");
    {
        this->m_client = client.get_interface("VClient0").as<i_base_client_dll>();
        this->m_prediction = client.get_interface("VClientPrediction001", true).as<c_prediction>();
        this->m_entity_list = client.get_interface("VClientEntityList0").as<i_client_entity_list>();
        this->m_global_vars = **(c_global_vars_base***)((*(DWORD**)this->m_client)[11] + 0x5);
        ;
        this->m_game_movement = client.get_interface("GameMovement001", true).as<c_game_movement>();
        ;
        this->m_game_rules = **client.get_sig("8B 0D ? ? ? ? 83 C4 10 C7 45").as<game_rules***>(0x2);
    }

    const auto engine = g_modules.get("engine.dll");
    {
        this->m_engine = engine.get_interface("VEngineClient0").as<iv_engine_client>();
        this->m_engine_sound = engine.get_interface("IEngineSoundClient0").as<i_engine_sound>();
        this->m_engine_vgui = engine.get_interface("VEngineVGui0").as<i_engine_vgui>();
        this->m_debug_overlay = engine.get_interface("VDebugOverlay0").as<iv_debug_overlay>();
        this->m_engine_trace = engine.get_interface("EngineTraceClient003", true).as<i_engine_trace>();
        this->m_model_info = engine.get_interface("VModelInfoClient006", true).as<i_model_info>();
    }

    const auto vgui_mat_surface = g_modules.get("vguimatsurface.dll");
    {
        this->m_surface = vgui_mat_surface.get_interface("VGUI_Surface0").as<i_surface>();
    }

    const auto vstdlib = g_modules.get("vstdlib.dll");
    {
        this->m_cvar = vstdlib.get_interface("VEngineCvar0").as<i_cvar>();
    }

    const auto vgui2 = g_modules.get("vgui2.dll");
    {
        this->m_localize = vgui2.get_interface("VGUI_Localize005").as<i_localize>();
    }

    m_mem_alloc = *reinterpret_cast<mem_alloc_t**>(GetProcAddress(GetModuleHandleA("tier0.dll"), "g_pMemAlloc"));
}

void c_interfaces::init()
{
    this->gather();
}
