// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Nikolay Enev
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Portable engine: seconds internally, milliseconds at the API; no SDK or heap.
typedef enum {
    EYES_NEUTRAL, EYES_CURIOUS, EYES_PLAYFUL, EYES_SLEEPY, EYES_ALERT,
    EYES_SURPRISED, EYES_EXCITED, EYES_DIZZY, EYES_SUSPICIOUS,
    EYES_GRUMPY, EYES_CONFUSED, EYES_RELAXED, EYES_MOOD_COUNT
} eyes_mood;
enum {
    EYES_STATIONARY = 1u << 0, EYES_TILT = 1u << 1,
    EYES_ROTATION = 1u << 2, EYES_QUICK_ROTATION = 1u << 3,
    EYES_JERK = 1u << 4, EYES_SHAKE = 1u << 5,
    EYES_STRONG_SHAKE = 1u << 6, EYES_HIGH_ACCEL = 1u << 7,
    EYES_FREE_FALL = 1u << 8, EYES_SETTLING = 1u << 9,
    EYES_SUSTAINED_TILT = 1u << 10, EYES_SMALL_MOVEMENT = 1u << 11,
    EYES_LINEAR_ACCEL = 1u << 12, EYES_ORIENTATION_CHANGE = 1u << 13
};
typedef enum { EYES_CLASSIC, EYES_OVAL, EYES_SLIT, EYES_DIAMOND, EYES_VARIANT_COUNT } eyes_variant;
typedef struct {
    eyes_variant variant;
    float pupil[2][2], body[2][2], lid[2], pupil_scale;
    eyes_mood mood;
    uint32_t blink_id, blink_at, blink_duration;
    unsigned blink_kind; // 0: shared, 1: double, 2: alternating
} eyes_pose;
typedef struct {
    uint32_t sampled_at;
    bool accel_ready, gyro_ready;
    float accel[3], gyro[3]; // m/s^2 and degrees/s, respectively
    bool animated;
    eyes_pose pose;
} eyes_snapshot;
typedef struct { float position, velocity; } eyes_spring;
typedef struct {
    bool primed, available;
    unsigned fresh_count, failures;
    float filtered[3], influence;
} eyes_channel;
typedef struct {
    eyes_channel accel, gyro;
    float gravity[3], previous_dynamic[3], tilt[2], force[2];
    float dynamic_rms, angular_speed, dizziness;
    float reference[3], settled_reference[3], rotation_force[2];
    float accel_confidence, gyro_confidence;
    bool calibrated, accel_saturated, gyro_saturated, had_motion;
    unsigned latched;
    uint32_t enter_ms[4], exit_ms[4];
    float energy[100];
    uint32_t energy_at[100], energy_duration[100];
    unsigned energy_index, energy_count, flags;
    uint32_t last_sample, last_observation, quiet_ms, rotation_ms, quick_ms, high_ms, fall_ms;
    uint32_t shake_at, jerk_at, last_motion;
    int shake_sign, shake_axis;
    unsigned shake_count;
    bool have_sample, have_observation, have_jerk;
} eyes_motion;
typedef struct {
    uint32_t rng, last_tick, accumulator, mood_at, mood_wait, glance_at, glance_wait;
    uint32_t blink_at, blink_wait, action_at, action_wait, reaction_at, reaction_ms;
    uint32_t quiet_ms, minute_at, expression_ms;
    bool reaction_active, sensor_reaction;
    uint32_t variant_at, variant_wait, glance_hold;
    unsigned variant_remaining;
    float arousal, placement;
    float body_drive[2];
    uint32_t body_drive_at[2];
    eyes_mood ambient, reaction;
    float gaze[2], blend_lid[2], blend_scale, orbit_phase;
    eyes_spring pupil[2][2], body[2][2];
    eyes_motion motion;
    eyes_pose pose;
} eyes_engine;

void eyes_engine_init(eyes_engine *engine, uint32_t seed, uint32_t now);
// At most five 20ms physics steps; missed scheduling opportunities are dropped.
void eyes_engine_update(eyes_engine *engine, const eyes_snapshot *sample, uint32_t now);
