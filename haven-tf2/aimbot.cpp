#include "aimbot.h"
#include "sdk.h"
#include "player_manager.h"

class trace_filter_one_player : public i_trace_filter
{
public:
    virtual bool should_hit_entity(c_base_entity* pEntityHandle, int contentsMask)
    {
        return (pEntityHandle->entindex() == 0 ||
                pEntityHandle->entindex() > g_interfaces.m_engine->get_max_clients()) ||
               pEntityHandle == pHit;
    }

    virtual TraceType_t get_type() const
    {
        return TraceType_t::TRACE_EVERYTHING;
    }
    c_base_entity* pHit;
};

#define TIME_TO_TICKS(dt) ((int)(0.5f + (float)(dt) / g_interfaces.m_global_vars->m_interval_per_tick))
#define TICKS_TO_TIME(dt) (g_interfaces.m_global_vars->m_interval_per_tick * (float)(dt))
void c_aimbot::other(c_base_player* local, usercmd_t* cmd)
{
    if (local->m_class() != TF2_Scout && local->m_class() != TF2_Heavy)
        return;
    auto* weapon = local->get_active_weapon();
    if (!weapon || weapon->GetSlot() != EWeaponSlots::SLOT_PRIMARY ||
        (TICKS_TO_TIME(local->m_tick_base() + 1) < weapon->m_next_primary_attack() && local->m_class() != TF2_Heavy))
        return;

    const vector eye_pos = local->m_vec_origin() + local->m_view_offset();
    float distance = FLT_MAX;

    for (auto i = 1; i <= g_interfaces.m_engine->get_max_clients(); i++)
    {
        const auto player = g_interfaces.m_entity_list->get_entity<c_base_player>(i);

        if (!player->is_valid(local))
            continue;

        auto head = player->get_hitbox_pos(4);
        const auto cur_distance = fabsf((head - eye_pos).delta(cmd->m_viewangles));
        if (cur_distance > 100.f || distance <= cur_distance)
            continue;

        const auto backup_mins = player->mins();
        const auto backup_maxs = player->maxs();

        player->mins() *= 10.f;
        player->maxs() *= 10.f;

        ray_t ray;
        ray.initialize(eye_pos, head);
        trace_filter traceFilter;
        traceFilter.skip = local;
        trace_t trace;
        g_interfaces.m_engine_trace->trace_ray(ray, MASK_SHOT, &traceFilter, &trace);
        if (trace.m_entity == player)
        {
            cmd->m_viewangles = eye_pos.look(head);
            cmd->buttons_ |= IN_ATTACK;
            distance = cur_distance;
        }

        player->mins() = backup_mins;
        player->maxs() = backup_maxs;
    }
}
void c_aimbot::run(c_base_player* local, usercmd_t* cmd)
{
    if ((TICKS_TO_TIME(local->m_tick_base() + 1) < local->m_next_attack() && local->m_class() != TF2_Heavy))
        return;
    if (local->m_class() != TF2_Sniper)
    {
        other(local, cmd);
        return;
    }
    vector eye_pos = local->m_vec_origin() + local->m_view_offset();
    auto* weapon = local->get_active_weapon();
    if (!weapon || weapon->GetSlot() != EWeaponSlots::SLOT_PRIMARY)
        return;
    int item_idx = weapon->item_index();
    if (item_idx == WPN_Huntsman || item_idx == WPN_CompoundBow ||
        TICKS_TO_TIME(local->m_tick_base() + 1) < weapon->m_next_primary_attack())
        return;

    float damage = weapon->get_charge_damage() + 150.0f;
    float distance = FLT_MAX;
    static auto* cl_interp = g_interfaces.m_cvar->find_var("cl_interp");
    static auto* cl_interp_ratio = g_interfaces.m_cvar->find_var("cl_interp_ratio");
    static auto* cl_updaterate = g_interfaces.m_cvar->find_var("cl_updaterate");
    float lerp = std::fmaxf(cl_interp->m_value.m_float_value,
                            cl_interp_ratio->m_value.m_float_value / cl_updaterate->m_value.m_float_value);
    for (auto i = 1; i <= g_interfaces.m_engine->get_max_clients(); i++)
    {
        auto player = g_interfaces.m_entity_list->get_entity<c_base_player>(i);

        if (!player->is_valid(local))
            continue;

        auto target = g_player_manager.players[player->entindex() - 1];
        if (target.player != player || target.m_records.empty())
            continue;
        auto record = target.m_records[0];
        if (!record || !record->built)
            continue;
        auto cur_distance = fabsf((player->m_vec_origin() - eye_pos).delta(cmd->m_viewangles));
        if (cur_distance > 100.f || distance <= cur_distance)
            continue;

        if (damage < player->m_i_health())
            continue;

        auto backup_mins = player->mins();
        auto backup_maxs = player->maxs();

        player->mins() *= 10.f;
        player->maxs() *= 10.f;

        auto head = player->get_hitbox_pos(0, record->bones);
        ray_t ray;
        ray.initialize(eye_pos, head);
        trace_filter traceFilter;
        traceFilter.skip = local;
        trace_t trace;
        g_interfaces.m_engine_trace->trace_ray(ray, MASK_SHOT, &traceFilter, &trace);
        if (trace.m_entity == player && trace.m_hitbox == 0)
        {
            cmd->m_viewangles = eye_pos.look(head);
            cmd->buttons_ |= IN_ATTACK;
            cmd->tick_count_ = TIME_TO_TICKS(target.m_sim_time + lerp);
            distance = cur_distance;
        }

        player->mins() = backup_mins;
        player->maxs() = backup_maxs;
    }
}
