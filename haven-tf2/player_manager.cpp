#include "pch.h"
#include "player_manager.h"
#include "sdk.h"
#include "movement_simulate.h"

#define DIST_EPSILON (0.03125f)
#define TIME_TO_TICKS(dt) ((int)(0.5f + (float)(dt) / g_interfaces.m_global_vars->m_interval_per_tick))
#define TICKS_TO_TIME(dt) (g_interfaces.m_global_vars->m_interval_per_tick * (float)(dt))

bool player_record_t::valid() const
{
    // use prediction curtime for this.
    const float curtime = TICKS_TO_TIME(g_cl.m_local->m_tick_base());

    // correct is the amount of time we have to correct game time,
    static auto* cl_interp = g_interfaces.m_cvar->find_var("cl_interp");
    static auto* cl_interp_ratio = g_interfaces.m_cvar->find_var("cl_interp_ratio");
    static auto* cl_updaterate = g_interfaces.m_cvar->find_var("cl_updaterate");
    const float lerp = std::fmaxf(cl_interp->m_value.m_float_value,
                                  cl_interp_ratio->m_value.m_float_value / cl_updaterate->m_value.m_float_value);
    float correct = lerp;

    // stupid fake latency goes into the incoming latency.
    const auto* nci = g_interfaces.m_engine->get_net_channel_info();
    if (nci)
        correct += nci->GetLatency(1);
    // check bounds [ 0, sv_maxunlag ]
    static auto sv_maxunlag = g_interfaces.m_cvar->find_var("sv_maxunlag");
    correct = std::clamp<float>(correct, 0.f, sv_maxunlag->m_value.m_float_value);

    // calculate difference between tick sent by player and our latency based tick.
    // ensure this record isn't too old.
    return std::abs(correct - (curtime - sim_time)) < 0.19f;
}

vector direction_pressed(player_record_t* record, vector last_vel)
{
    const vector vel = record->vel.normalized();
    last_vel.normalize_in_place();
    if ((vel - last_vel).length_2d() > 0.1f)
    {
        vector vel_difference = vel - last_vel;
        vel_difference.m_z = 0;
        vector dir = vel_difference.angle_to();
        dir.m_y -= record->eye_angle.m_y;
        return dir.angle_vector() * 450.f;
    }
    return vector();
}

inline float anglemod(float a)
{
    a = (360.f / 65536) * ((int)(a * (65536.f / 360.0f)) & 65535);
    return a;
}

float ApproachAngle(float target, float value, float speed)
{
    float delta = target - value;

    // Speed is assumed to be positive
    if (speed < 0)
        speed = -speed;

    if (delta < -180)
        delta += 360;
    else if (delta > 180)
        delta -= 360;

    if (delta > speed)
        value += speed;
    else if (delta < -speed)
        value -= speed;
    else
        value = target;

    return value;
}

float turn_epsilon(vector orig_vel)
{
    float max = 0.f;
    for (auto i = -1; i < 3; i++)
    {
        for (auto k = -1; k < 3; k++)
        {
            vector vel = orig_vel;
            vel.m_x += i * DIST_EPSILON;
            vel.m_y += k * DIST_EPSILON;
            float turn = rad_to_deg(std::atan2(vel.m_y, vel.m_x) - std::atan2(orig_vel.m_y, orig_vel.m_x));
            if (turn > max)
                max = turn;
        }
    }
    return max;
}

float dir_turning(vector vel, vector last_vel)
{
    if (vel.length() < 1.f)
        return 0;
    if (last_vel.length() < 1.f)
        return 0;

    float dir = rad_to_deg(std::atan2(vel.m_y, vel.m_x) - std::atan2(last_vel.m_y, last_vel.m_x));
    while (dir > 180.f)
        dir -= 360.f;
    while (dir < -180.f)
        dir += 360.f;
    if (fabsf(dir) > 165.f || fabsf(dir) < 1.f)
        return 0;
    if (fabsf(dir) <= fabsf(turn_epsilon(vel)))
        return 0;
    return dir;
}

void c_player_manager::update_players()
{

    for (auto i = 1; i <= g_interfaces.m_engine->get_max_clients(); i++)
    {

        const auto target = g_interfaces.m_entity_list->get_entity<c_base_player>(i);
        auto* player = &players[i - 1];
        if (player->player != target)
        {
            player->player = target;
            player->m_sim_time = -1.f;
        }

        if (!player->player)
            continue;

        if (!target->is_valid(g_cl.m_local, false))
        {
            player->m_records.clear();
            continue;
        }

        const float new_time = target->sim_time();
        if (player->m_sim_time < target->sim_time())
        {
            player_record_t new_record;
            new_record.eye_angle = target->m_eye_angles();
            new_record.m_lag =
                (player->m_sim_time == -1.f)
                    ? 1
                    : static_cast<int>(
                          0.5f + (float)(new_time - player->m_sim_time) /
                                     g_interfaces.m_global_vars
                                         ->m_interval_per_tick); // TIME_TO_TICKS( ( new_time - player->sim_time ) );
            player->m_sim_time = new_time;
            new_record.origin = target->m_vec_origin();
            const vector dif = new_record.origin - player->pred_origin;
            if (fabsf(dif.m_x) <= 0.03125f)
                new_record.origin.m_x = player->pred_origin.m_x;
            if (fabsf(dif.m_y) <= 0.03125f)
                new_record.origin.m_y = player->pred_origin.m_y;
            if (fabsf(dif.m_z) <= 0.03125f)
                new_record.origin.m_z = player->pred_origin.m_z;
            new_record.vel = target->m_velocity();
            // new_record.dir = 0;
            new_record.player = player;
            new_record.sim_time = new_time;
            new_record.on_ground = target->flags() & (1 << 0);
            new_record.mins = target->mins();
            new_record.maxs = target->maxs();
            if (new_record.on_ground)
                new_record.vel.m_z = 0;

            const auto backup_abs_origin = target->get_abs_origin();

            target->set_abs_origin(target->m_vec_origin());

            const auto bones_ac = player->player->bone_cache();
            if (!bones_ac)
                return;

            bones_ac->UpdateBones(new_record.bones, 128, -1);

            new_record.built = target->setup_bones(new_record.bones, 128, 0x100, target->sim_time());

            target->set_abs_origin(backup_abs_origin);
            player_info_t info;
            if (g_interfaces.m_engine->get_player_info(target->entindex(), &info))
                if (!player->m_records.empty())
                {
                    const auto path = g_movement.path;
                    new_record.vel =
                        (new_record.origin - player->m_records[0]->origin) * (1.f / TICKS_TO_TIME(new_record.m_lag));

                    if (new_record.vel.length() < 5.f || new_record.origin == player->pred_origin)
                    {
                        new_record.dir = 0;
                        new_record.ground_dir = 0;
                    }
                    else
                    {
                        float max_turn = 720.f * g_interfaces.m_global_vars->m_interval_per_tick;
                        float turn = (new_record.eye_angle.m_y - player->m_records[0]->eye_angle.m_y);
                        while (turn > 180.f)
                            turn -= 360.f;
                        while (turn < -180.f)
                            turn += 360.f;
                        turn /= new_record.m_lag;
                        // turn = fminf( max_turn, fmaxf( -max_turn, turn ) );
                        // if ( ( turn > 0 && turn > player->m_records[ 0 ]->dir ) || ( turn < 0 && turn <
                        // player->m_records[ 0 ]->dir ) ) {
                        new_record.dir = ApproachAngle(turn, player->m_records[0]->dir,
                                                       max_turn * g_interfaces.m_global_vars->m_interval_per_tick);
                        //}
                        // else new_record.dir = turn;

                        // if ( new_record.on_ground && player->m_records.size() > 3 ) {
                        g_movement.setup_mv(new_record.vel, player->player, g_cl.m_local->entindex());
                        new_record.move_data = g_movement.estimate_walking_dir(
                            new_record.vel, player->m_records[0]->vel, new_record.eye_angle, new_record.origin);
                        turn = dir_turning(new_record.move_data, player->m_records[0]->move_data);
                        while (turn > 180.f)
                            turn -= 360.f;
                        while (turn < -180.f)
                            turn += 360.f;
                        new_record.ground_dir = turn / new_record.m_lag;
                        //	auto count = 1;
                        //	for ( auto i = 0; i < 4; i++ ) {
                        //		new_record.ground_dir += player->m_records[ i ]->ground_dir;
                        //		count++;
                        //	}
                        //	//turn = fminf( max_turn, fmaxf( -max_turn, turn) );
                        //	//if ( ( turn > 0 && turn > player->m_records[ 0 ]->ground_dir ) || ( turn < 0 && turn <
                        //player->m_records[ 0 ]->ground_dir ) ) { 	new_record.ground_dir = new_record.ground_dir /
                        //count;
                        //}
                        // else
                        //	new_record.ground_dir = 0.f;
                        //}
                        // else new_record.ground_dir = turn;

                        // new_record.ground_dir = ApproachAngle( dir_turning( new_record.vel, player->m_records[ 0
                        // ]->vel ) / new_record.m_lag, 	player->m_records[ 0 ]->ground_dir, 	( fmaxf( fabsf( dir ) *
                        //g_interfaces.m_global_vars->m_interval_per_tick, 720.f * (
                        //g_interfaces.m_global_vars->m_interval_per_tick *
                        //g_interfaces.m_global_vars->m_interval_per_tick ))) );
                    }
                    g_movement.setup_mv(new_record.vel, player->player, g_cl.m_local->entindex());
                    g_movement.mv.m_ground_dir = 0.f;
                    g_movement.mv.m_dir = 0.f;
                    player->pred_origin = g_movement.run();
                    g_movement.path = path;
                    // if ( fabsf( new_record.dir ) > 0.1f )
                    //	new_record.dir_decay = std::clamp<float>( new_record.dir_decay + 0.1f, 0.8f, 1.f );
                    // else
                    //	new_record.dir_decay = std::clamp<float>( new_record.dir_decay - 0.1f, 0.8f, 1.f );
                }

            player->m_records.push_front(std::make_shared<player_record_t>(new_record));
        }
        while (player->m_records.size() > 256)
            player->m_records.pop_back();
    }
}

player_record_t::~player_record_t()
{
    // g_interfaces.m_mem_alloc->free( bones );
}

bool player_record_t::cache()
{
    if (!built)
        return false;
    const auto bones_ac = player->player->bone_cache();
    if (!bones_ac)
        return false;
    bones_ac->UpdateBones(bones, 128, player->player->sim_time());
    // memcpy( bones_ac->m_pBones, bones, sizeof( matrix_3x4 ) * 128 );
}

void player_record_t::restore()
{
    const auto bones_ac = player->player->bone_cache();
    if (!bones_ac)
        return;
    bones_ac->UpdateBones(bones, 128, -1);
}
