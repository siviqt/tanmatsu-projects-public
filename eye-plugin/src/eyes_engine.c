// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Nikolay Enev
#include "eyes_engine.h"
#include <math.h>
#include <string.h>

static float clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float norm3(const float v[3]) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
static bool vector_valid(const float v[3], float limit) {
    for (int i=0;i<3;++i) if (!isfinite(v[i]) || fabsf(v[i])>limit) return false;
    return true;
}
static uint32_t random_u32(eyes_engine *e) {
    uint32_t x=e->rng; x^=x<<13; x^=x>>17; x^=x<<5; return e->rng=x;
}
static uint32_t interval(eyes_engine *e, uint32_t lo, uint32_t hi) {
    return lo+random_u32(e)%(hi-lo+1);
}
static float random_unit(eyes_engine *e) { return (float)(random_u32(e)%2001)/1000.0f-1; }

// Ten contiguous fresh samples re-prime a failed channel; no zero-filled evidence.
static bool channel(eyes_channel *c, const float v[3], bool valid, float scale, float alpha, float dt) {
    if (!valid) {
        c->fresh_count=0;
        c->influence=0;
        if (c->failures<5) ++c->failures;
        if (c->failures>=5) c->available=c->primed=false;
        return false;
    }
    c->failures=0;
    if (c->fresh_count<10) ++c->fresh_count;
    if (!c->primed) {
        for (int i=0;i<3;++i) c->filtered[i]=v[i]*scale;
        c->primed=true;
    } else {
        for (int i=0;i<3;++i) c->filtered[i]+=alpha*(v[i]*scale-c->filtered[i]);
    }
    c->available=c->fresh_count>=10;
    if (c->available) c->influence=clamp(c->influence+dt/.5f,0,1);
    return c->available;
}
// Schmitt trigger with independent entry/exit dwell; invalid evidence clears it.
static void evidence(eyes_motion *m, unsigned slot, unsigned flag, bool valid,
                     bool enter, bool leave, uint32_t in_ms, uint32_t out_ms, uint32_t dt) {
    if (!valid) { m->latched&=~flag; m->enter_ms[slot]=m->exit_ms[slot]=0; return; }
    if (!(m->latched&flag)) {
        m->enter_ms[slot]=enter?m->enter_ms[slot]+dt:0;
        if (m->enter_ms[slot]>=in_ms) { m->latched|=flag; m->exit_ms[slot]=0; }
    } else {
        m->exit_ms[slot]=leave?m->exit_ms[slot]+dt:0;
        if (m->exit_ms[slot]>=out_ms) { m->latched&=~flag; m->enter_ms[slot]=0; }
    }
}
static float direction_dot(const float a[3], const float b[3]) {
    float lengths=norm3(a)*norm3(b);
    return lengths>.01f?clamp((a[0]*b[0]+a[1]*b[1]+a[2]*b[2])/lengths,-1,1):1;
}
static void motion_update(eyes_motion *m, const eyes_snapshot *s, uint32_t now) {
    uint32_t elapsed=m->have_sample?(uint32_t)(now-m->last_sample):20;
    if (m->have_sample && elapsed==0) return;
    bool gap=elapsed>250;
    if (gap) {
        memset(m,0,sizeof(*m));
        elapsed=20;
    }
    m->have_sample=true; m->last_sample=now;
    float dt=(float)elapsed/1000;
    bool fresh=s && (uint32_t)(now-s->sampled_at)<=250 &&
        (!m->have_observation || ((uint32_t)(s->sampled_at-m->last_observation)>0 &&
         (uint32_t)(s->sampled_at-m->last_observation)<0x80000000u));
    if(fresh) { m->last_observation=s->sampled_at; m->have_observation=true; }
    bool a=channel(&m->accel,s?s->accel:(float[3]){0},
        fresh && s->accel_ready && vector_valid(s->accel,40),1/9.80665f,dt/(.06f+dt),dt);
    bool g=channel(&m->gyro,s?s->gyro:(float[3]){0},
        fresh && s->gyro_ready && vector_valid(s->gyro,2200),1,dt/(.04f+dt),dt);
    m->flags=0;
    m->accel_saturated=fresh && s->accel_ready && vector_valid(s->accel,40) &&
        (fabsf(s->accel[0])>=19.2f || fabsf(s->accel[1])>=19.2f || fabsf(s->accel[2])>=19.2f);
    m->gyro_saturated=fresh && s->gyro_ready && vector_valid(s->gyro,2200) &&
        (fabsf(s->gyro[0])>=1960 || fabsf(s->gyro[1])>=1960 || fabsf(s->gyro[2])>=1960);
    m->accel_confidence=a?m->accel.influence*(m->accel_saturated?.25f:g?1:.5f):0;
    m->gyro_confidence=g?m->gyro.influence*(m->gyro_saturated?.25f:1):0;
    for (int i=0;i<2;++i) {
        float rate=g?m->gyro.filtered[i==0?1:0]:0;
        float target=fabsf(rate)>3?clamp((i==0?-rate:rate)/180,-1,1)*m->gyro_confidence:0;
        m->rotation_force[i]+=dt/(.1f+dt)*(target-m->rotation_force[i]);
    }
    m->angular_speed=g?norm3(m->gyro.filtered):0;
    m->dizziness=clamp(m->dizziness+dt*((g?m->angular_speed/180:0)-m->dizziness/4),0,4);
    if (g && m->angular_speed>8) m->rotation_ms=(m->rotation_ms<1000)?m->rotation_ms+elapsed:1000;
    else if (!g || m->angular_speed<5) m->rotation_ms=0;
    if (g && m->angular_speed>120) m->quick_ms=(m->quick_ms<1000)?m->quick_ms+elapsed:1000;
    else if (!g || m->angular_speed<80) m->quick_ms=0;
    if (m->rotation_ms>=200) m->flags|=EYES_ROTATION;
    if (m->quick_ms>=60) m->flags|=EYES_QUICK_ROTATION;
    if (!a) {
        m->energy_count=m->energy_index=m->quiet_ms=m->high_ms=m->fall_ms=0;
        m->shake_count=0; m->shake_sign=0;
        m->dynamic_rms=0;
        m->latched=0;
        memset(m->enter_ms,0,sizeof(m->enter_ms));
        memset(m->exit_ms,0,sizeof(m->exit_ms));
        m->had_motion=false;
        m->have_jerk=false;
        memset(m->gravity,0,sizeof(m->gravity));
        memset(m->previous_dynamic,0,sizeof(m->previous_dynamic));
        for (int i=0;i<2;++i) { m->tilt[i]*=.1f/(.1f+dt); m->force[i]*=.1f/(.1f+dt); }
        return;
    }
    float magnitude=norm3(m->accel.filtered);
    if (norm3(m->gravity)<.01f) memcpy(m->gravity,m->accel.filtered,sizeof(m->gravity));
    // Relative gravity propagation, not an absolute attitude/position estimate.
    if (g) {
        float w[3], cross[3];
        for (int i=0;i<3;++i) w[i]=m->gyro.filtered[i]*.017453293f;
        cross[0]=w[1]*m->gravity[2]-w[2]*m->gravity[1];
        cross[1]=w[2]*m->gravity[0]-w[0]*m->gravity[2];
        cross[2]=w[0]*m->gravity[1]-w[1]*m->gravity[0];
        for (int i=0;i<3;++i) m->gravity[i]-=cross[i]*dt;
        float length=norm3(m->gravity);
        if (length>.01f) for (int i=0;i<3;++i) m->gravity[i]/=length;
    }
    if (magnitude>.85f && magnitude<1.15f && (!g || m->angular_speed<60))
        for (int i=0;i<3;++i) m->gravity[i]+=dt/(.5f+dt)*(m->accel.filtered[i]-m->gravity[i]);
    float dynamic[3], derivative[3];
    for (int i=0;i<3;++i) {
        dynamic[i]=m->accel.filtered[i]-m->gravity[i];
        derivative[i]=(dynamic[i]-m->previous_dynamic[i])/dt;
    }
    float energy=norm3(dynamic); energy*=energy;
    m->energy_at[m->energy_index]=now;
    m->energy_duration[m->energy_index]=elapsed;
    m->energy[m->energy_index]=energy; m->energy_index=(m->energy_index+1)%100;
    if (m->energy_count<100) ++m->energy_count;
    float sum=0; uint32_t duration=0;
    // A 500ms time-weighted RMS, including irregular worker scheduling.
    for (unsigned i=0;i<m->energy_count;++i) {
        unsigned index=(m->energy_index+99-i)%100;
        uint32_t age=now-m->energy_at[index];
        if (age>=500) break;
        uint32_t weight=m->energy_duration[index];
        if (weight>500-age) weight=500-age;
        sum+=m->energy[index]*weight; duration+=weight;
    }
    m->dynamic_rms=duration?sqrtf(sum/(float)duration):0;
    if (magnitude>.85f && magnitude<1.15f && !m->accel_saturated && m->dynamic_rms<.04f && g && m->angular_speed<3) m->quiet_ms=(m->quiet_ms<2000)?m->quiet_ms+elapsed:2000;
    else if (magnitude<=.85f || magnitude>=1.15f || m->accel_saturated || m->dynamic_rms>.07f || !g || m->angular_speed>6) m->quiet_ms=0;
    if (m->quiet_ms>=1000) {
        m->flags|=EYES_STATIONARY;
        if (!m->calibrated) {
            memcpy(m->reference,m->gravity,sizeof(m->reference));
            memcpy(m->settled_reference,m->gravity,sizeof(m->reference));
            m->calibrated=true;
        }
    }
    if (m->dynamic_rms>.08f || (g && m->angular_speed>8)) { m->last_motion=now; m->had_motion=true; }
    else if (m->had_motion && !(m->flags&EYES_STATIONARY) && m->angular_speed<8 && (uint32_t)(now-m->last_motion)>=300 && (uint32_t)(now-m->last_motion)<1000 && g)
        m->flags|=EYES_SETTLING;
    if (energy>.0225f && norm3(derivative)>4 &&
        (!m->have_jerk || (uint32_t)(now-m->jerk_at)>=500)) {
        m->flags|=EYES_JERK; m->jerk_at=now; m->have_jerk=true;
    }
    memcpy(m->previous_dynamic,dynamic,sizeof(dynamic));
    int axis=0;
    for (int i=1;i<3;++i) if (fabsf(dynamic[i])>fabsf(dynamic[axis])) axis=i;
    if ((uint32_t)(now-m->shake_at)>1000) { m->shake_count=0; m->shake_sign=0; }
    if (fabsf(dynamic[axis])>.25f) {
        int sign=dynamic[axis]>0?1:-1;
        if (axis!=m->shake_axis) { m->shake_count=0; m->shake_sign=0; }
        if (sign!=m->shake_sign) {
            if(m->shake_count<100) ++m->shake_count;
            m->shake_sign=sign; m->shake_axis=axis; m->shake_at=now;
        }
    }
    if (m->shake_count>=3 && (uint32_t)(now-m->shake_at)<500) {
        m->flags|=EYES_SHAKE;
        if (m->dynamic_rms>.55f) m->flags|=EYES_STRONG_SHAKE;
    }
    float raw=norm3(s->accel)/9.80665f;
    m->high_ms=raw>1.6f?(m->high_ms<1000?m->high_ms+elapsed:1000):0;
    m->fall_ms=raw<.25f?(m->fall_ms<1000?m->fall_ms+elapsed:1000):0;
    if (m->high_ms>=40) m->flags|=EYES_HIGH_ACCEL;
    if (m->fall_ms>=60) m->flags|=EYES_FREE_FALL;
    bool reliable=magnitude>.85f && magnitude<1.15f && !m->accel_saturated;
    float dot=direction_dot(m->gravity,m->reference);
    bool tilt_valid=m->calibrated && reliable && m->dynamic_rms<.1f;
    evidence(m,0,EYES_SUSTAINED_TILT,tilt_valid,dot<.965926f,dot>.984808f,1000,300,elapsed);
    evidence(m,1,EYES_SMALL_MOVEMENT,true,m->dynamic_rms>.06f,m->dynamic_rms<.04f,100,200,elapsed);
    evidence(m,2,EYES_LINEAR_ACCEL,!m->accel_saturated && (!g || m->angular_speed<60),
             energy>.04f,energy<.0144f,120,200,elapsed);
    float settled_dot=direction_dot(m->gravity,m->settled_reference);
    evidence(m,3,EYES_ORIENTATION_CHANGE,m->calibrated && reliable,
             settled_dot<.866025f,settled_dot>.906308f,300,300,elapsed);
    m->flags|=m->latched;
    if ((m->flags&EYES_STATIONARY) && (m->flags&EYES_ORIENTATION_CHANGE))
        memcpy(m->settled_reference,m->gravity,sizeof(m->settled_reference));
    // Preserve the hardware-accepted X/Y tilt signs from 0.1.3.
    for (int i=0;i<2;++i) {
        float target=fabsf(m->gravity[i])>.052f?clamp(m->gravity[i],-1,1)*m->accel_confidence:0;
        m->tilt[i]+=dt/(.1f+dt)*(target-m->tilt[i]);
        m->force[i]+=dt/(.1f+dt)*((fabsf(dynamic[i])>.04f?clamp(-dynamic[i],-1,1)*m->accel_confidence:0)-m->force[i]);
    }
    if (tilt_valid && dot<.996195f) m->flags|=EYES_TILT;
}

static void spring(eyes_spring *s, float target, float frequency, float damping) {
    float w=6.2831853f*frequency;
    s->velocity+=(w*w*(target-s->position)-2*damping*w*s->velocity)*.02f;
    s->position+=s->velocity*.02f;
    if (s->position>1 || s->position< -1) {
        s->position=clamp(s->position,-1,1);
        if (s->position*s->velocity>0) s->velocity*= -.2f;
    }
}
static eyes_mood choose_mood(eyes_engine *e) {
    static const eyes_mood moods[]={EYES_NEUTRAL,EYES_CURIOUS,EYES_PLAYFUL,EYES_RELAXED,
        EYES_ALERT,EYES_SUSPICIOUS,EYES_GRUMPY,EYES_CONFUSED,EYES_SLEEPY};
    unsigned weights[]={30,20,15,20,5,4,3,3,0};
    weights[8]=e->quiet_ms>120000?(unsigned)clamp((e->quiet_ms-120000)/6000.0f,0,30):0;
    weights[1]+=(unsigned)(e->arousal*15);
    weights[2]+=(unsigned)(e->arousal*10);
    weights[4]+=(unsigned)(e->arousal*25);
    weights[3]+=(unsigned)(e->placement*30);
    unsigned total=0; for (unsigned i=0;i<9;++i) total+=weights[i];
    unsigned r=random_u32(e)%total;
    for (unsigned i=0;i<9;++i) { if (r<weights[i]) return moods[i]; r-=weights[i]; }
    return EYES_NEUTRAL;
}
static void reaction(eyes_engine *e, eyes_mood mood, uint32_t now, uint32_t duration) {
    e->pose.blink_at=now-1500; // Strong/major poses must not inherit a pending blink.
    e->reaction=mood; e->reaction_at=now; e->reaction_ms=duration; e->reaction_active=true;
}
static void schedule(eyes_engine *e, uint32_t now, uint32_t elapsed) {
    if ((uint32_t)(now-e->minute_at)>=60000) { e->minute_at=now; e->expression_ms=0; }
    if (e->reaction_active && (uint32_t)(now-e->reaction_at)>=e->reaction_ms) e->reaction_active=false;
    bool quiet=(e->motion.flags&EYES_STATIONARY)!=0;
    bool absent=!e->motion.accel.available && !e->motion.gyro.available;
    if (quiet || absent) {
        if (e->quiet_ms<300000) e->quiet_ms+=elapsed>100?100:elapsed;
    } else e->quiet_ms=0;
    unsigned f=e->motion.flags;
    float dt=(elapsed>100?100:elapsed)/1000.0f;
    float activity=clamp(e->motion.dynamic_rms*2+e->motion.angular_speed/180,0,1);
    e->arousal+=dt/(3+dt)*(activity-e->arousal);
    e->placement+=dt/(5+dt)*(((f&EYES_SETTLING)?1.0f:0)-e->placement);
    if (f&EYES_SETTLING) e->placement=1;
    // Strong evidence may interrupt an incidental expression, but never restart
    // a sensor reaction. Its cooldown remains measured from its original onset.
    bool strong=(f&(EYES_SHAKE|EYES_STRONG_SHAKE|EYES_JERK|EYES_HIGH_ACCEL|EYES_FREE_FALL))!=0;
    if (strong && e->reaction_active && !e->sensor_reaction) {
        e->reaction_active=false; e->reaction_at=now-1000; e->reaction_ms=0;
    }
    // Repeated input cannot continually restart a reaction. Leave a 1s cooldown.
    if (!e->reaction_active && (uint32_t)(now-e->reaction_at)>=e->reaction_ms+1000) {
        e->sensor_reaction=true;
        if (f&(EYES_SHAKE|EYES_STRONG_SHAKE)) {
            unsigned r=random_u32(e)%100;
            reaction(e,r<40?EYES_SURPRISED:r<75?EYES_EXCITED:EYES_CONFUSED,now,2000);
        } else if (f&(EYES_JERK|EYES_HIGH_ACCEL|EYES_FREE_FALL)) reaction(e,EYES_SURPRISED,now,1500);
        else if (e->motion.dizziness>1.2f) reaction(e,EYES_DIZZY,now,4000);
        else if (f&EYES_QUICK_ROTATION) reaction(e,EYES_ALERT,now,1500);
        else if (f&EYES_ORIENTATION_CHANGE) reaction(e,EYES_CURIOUS,now,2000);
    }
    if ((uint32_t)(now-e->mood_at)>=e->mood_wait) {
        e->ambient=choose_mood(e); e->mood_at=now; e->mood_wait=interval(e,30000,90000);
    }
    if ((uint32_t)(now-e->glance_at)>=e->glance_wait && !e->reaction_active) {
        e->gaze[0]=random_unit(e)*.65f; e->gaze[1]=random_unit(e)*.4f;
        e->glance_at=now; e->glance_hold=interval(e,400,1500);
        e->glance_wait=interval(e,5000,20000);
        if(e->ambient==EYES_SLEEPY || e->ambient==EYES_GRUMPY) e->glance_wait*=2;
    }
    if ((uint32_t)(now-e->action_at)>=e->action_wait && !e->reaction_active) {
        if (e->expression_ms+1500<=12000) {
            e->sensor_reaction=false;
            reaction(e,(random_u32(e)&1)?EYES_PLAYFUL:EYES_CONFUSED,now,1500);
            e->expression_ms+=1500;
        }
        e->action_at=now; e->action_wait=interval(e,20000,90000);
    }
    if ((uint32_t)(now-e->blink_at)>=e->blink_wait && !e->reaction_active) {
        ++e->pose.blink_id; if (!e->pose.blink_id) ++e->pose.blink_id;
        e->pose.blink_at=now;
        e->pose.blink_duration=e->ambient==EYES_SLEEPY?interval(e,350,650):interval(e,100,180);
        unsigned kind=random_u32(e)%100;
        e->pose.blink_kind=kind<12?1:kind<17 && e->ambient==EYES_PLAYFUL?2:0;
        e->blink_at=now; e->blink_wait=e->ambient==EYES_SLEEPY?interval(e,4000,10000):interval(e,2000,8000);
    }
    if ((uint32_t)(now-e->variant_at)>=e->variant_wait && !e->reaction_active) {
        // Random order without starving a design by repeatedly alternating two.
        if(!e->variant_remaining)
            e->variant_remaining=((1u<<EYES_VARIANT_COUNT)-1)&~(1u<<e->pose.variant);
        unsigned count=0;
        for(unsigned v=0;v<EYES_VARIANT_COUNT;++v) count+=(e->variant_remaining>>v)&1u;
        unsigned pick=random_u32(e)%count;
        for(unsigned v=0;v<EYES_VARIANT_COUNT;++v) if(e->variant_remaining&(1u<<v)) {
            if(pick--==0) {
                e->pose.variant=(eyes_variant)v;
                e->variant_remaining&=~(1u<<v);
                break;
            }
        }
        e->variant_at=now; e->variant_wait=interval(e,30000,60000);
    }
    e->pose.mood=e->reaction_active?e->reaction:e->ambient;
}
// Retain brief handling peaks long enough for the launcher's ~1Hz redraw.
// Opposing movement replaces the peak immediately; no periodic/random drive.
static void body_drive_update(eyes_engine *e, uint32_t now, uint32_t elapsed) {
    float dt=(elapsed>100?100:elapsed)/1000.0f;
    for(int axis=0;axis<2;++axis) {
        float input=clamp(e->motion.force[axis]*3+e->motion.rotation_force[axis]*1.5f,-1,1);
        if(fabsf(input)>.12f && (fabsf(input)>=fabsf(e->body_drive[axis]) ||
                               (input*e->body_drive[axis]<0 && fabsf(input)>.5f*fabsf(e->body_drive[axis])))) {
            e->body_drive[axis]=input;
            e->body_drive_at[axis]=now;
        } else if((uint32_t)(now-e->body_drive_at[axis])>=1000) {
            e->body_drive[axis]*=.15f/(.15f+dt);
        }
    }
}
static void physics(eyes_engine *e, uint32_t now) {
    eyes_mood m=e->pose.mood;
    float lids[2]={1,1}, scale=1, response=1;
    switch (m) {
        case EYES_SLEEPY: lids[0]=lids[1]=.45f; response=.6f; break;
        case EYES_SUSPICIOUS: lids[0]=lids[1]=.55f; break;
        case EYES_GRUMPY: lids[0]=lids[1]=.35f; response=.7f; break;
        case EYES_CONFUSED: lids[0]=.5f; lids[1]=.85f; break;
        case EYES_RELAXED: lids[0]=lids[1]=.85f; response=.8f; break;
        case EYES_SURPRISED: scale=.7f; response=1.2f; break;
        case EYES_ALERT: case EYES_EXCITED: response=1.2f; break;
        default: break;
    }
    e->blend_scale+=(scale-e->blend_scale)*.04f;
    e->pose.pupil_scale=e->blend_scale;
    e->orbit_phase+=.04f; if(e->orbit_phase>6.2831853f) e->orbit_phase-=6.2831853f;
    float glance=(uint32_t)(now-e->glance_at)<e->glance_hold?1:.15f;
    for (int eye=0;eye<2;++eye) {
        e->blend_lid[eye]+=(lids[eye]-e->blend_lid[eye])*.04f;
        e->pose.lid[eye]=e->blend_lid[eye];
        for (int axis=0;axis<2;++axis) {
            float target=e->motion.tilt[axis]*.65f+e->motion.force[axis]*.2f+e->motion.rotation_force[axis]*.15f+e->gaze[axis]*glance;
            if (m==EYES_SURPRISED) target=0;
            if (m==EYES_CURIOUS) target+=axis?-.25f:(eye?.12f:.25f);
            if (m==EYES_EXCITED) target+=.12f*sinf(e->orbit_phase*3+axis*1.57f);
            if (m==EYES_CONFUSED) target+=.08f*sinf(e->orbit_phase+axis*1.57f);
            if (m==EYES_SLEEPY && axis==1) target+=.3f;
            if ((m==EYES_PLAYFUL || m==EYES_CONFUSED) && axis==0) target+=(eye?-.4f:.4f);
            if (m==EYES_SUSPICIOUS && axis==0) target+=.5f;
            if (m==EYES_DIZZY) {
                float decay=1-clamp((uint32_t)(now-e->reaction_at)/(float)e->reaction_ms,0,1);
                target+=.35f*decay*(axis?sinf(e->orbit_phase):cosf(e->orbit_phase));
            }
            spring(&e->pupil[eye][axis],clamp(target,-1,1),m==EYES_DIZZY?2:5*response,m==EYES_RELAXED?1:.65f);
            // Body handling stays independent of mood gaze (including surprise),
            // while retaining spring momentum across direction/mood changes.
            float body_target=e->motion.tilt[axis]*1.2f+e->body_drive[axis];
            spring(&e->body[eye][axis],clamp(body_target,-1,1),.8f,.35f);
            e->pose.pupil[eye][axis]=e->pupil[eye][axis].position;
            e->pose.body[eye][axis]=e->body[eye][axis].position;
        }
        // Disk projection and removal of outward radial velocity.
        float x=e->pose.pupil[eye][0], y=e->pose.pupil[eye][1];
        float length=sqrtf(x*x+y*y);
        if (length>1) {
            x/=length; y/=length;
            float outward=x*e->pupil[eye][0].velocity+y*e->pupil[eye][1].velocity;
            for (int axis=0;axis<2;++axis) {
                float n=axis?y:x;
                e->pose.pupil[eye][axis]=e->pupil[eye][axis].position=n;
                if(outward>0) e->pupil[eye][axis].velocity-=outward*n;
            }
        }
    }
}
void eyes_engine_init(eyes_engine *e, uint32_t seed, uint32_t now) {
    memset(e,0,sizeof(*e)); e->rng=seed?seed:0x6d2b79f5u;
    e->last_tick=e->mood_at=e->glance_at=e->blink_at=e->action_at=e->minute_at=now;
    e->reaction_at=now-1000;
    e->mood_wait=interval(e,30000,90000); e->glance_wait=interval(e,5000,20000);
    e->blink_wait=interval(e,2000,8000); e->action_wait=interval(e,20000,90000);
    e->variant_at=now; e->variant_wait=interval(e,30000,60000); e->glance_hold=1500;
    e->variant_remaining=((1u<<EYES_VARIANT_COUNT)-1)&~(1u<<EYES_CLASSIC);
    e->pose.pupil_scale=e->blend_scale=1;
    for(int i=0;i<2;++i) e->pose.lid[i]=e->blend_lid[i]=1;
}
void eyes_engine_update(eyes_engine *e, const eyes_snapshot *s, uint32_t now) {
    uint32_t elapsed=now-e->last_tick;
    if (!elapsed) return;
    e->last_tick=now;
    motion_update(&e->motion,s,now);
    if (elapsed>250) {
        // Drop obsolete expressions and velocity instead of replaying a backlog.
        e->pose.blink_at=now-1500; e->reaction_active=false;
        e->blink_at=e->glance_at=e->action_at=e->variant_at=e->mood_at=now;
        e->gaze[0]=e->gaze[1]=0;
        memset(e->pupil,0,sizeof(e->pupil)); memset(e->body,0,sizeof(e->body));
        memset(e->body_drive,0,sizeof(e->body_drive));
        e->accumulator=0;
    }
    body_drive_update(e,now,elapsed);
    schedule(e,now,elapsed);
    e->accumulator+=elapsed>100?100:elapsed;
    while(e->accumulator>=20) { physics(e,now); e->accumulator-=20; }
}
