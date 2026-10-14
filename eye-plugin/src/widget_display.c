// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Nikolay Enev
#include "eyes.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static int min_int(int a, int b) { return a < b ? a : b; }
static int max_int(int a, int b) { return a > b ? a : b; }
static int nearest(float x) { return (int)(x+(x<0?-.5f:.5f)); }
static float unit(float x) {
    if (!isfinite(x)) return 0;
    return x < -1 ? -1 : x > 1 ? 1 : x;
}

// Variant silhouettes share the same reserved envelope.
static bool inside(int x, int y, int rx, int ry, bool diamond) {
    if(diamond) return abs(x)*ry+abs(y)*rx<=rx*ry;
    return x*x*ry*ry+y*y*rx*rx<=rx*rx*ry*ry;
}

int eyes_widget_draw(eyes_widget *widget, pax_buf_t *buf,
                     int right, int top, int height,
                     const eyes_snapshot *sample, uint32_t now) {
    // Pixel access ignores the PAX transform and clip. Enforce bounds explicitly;
    // leave all incoming graphics state untouched. Unsupported formats fail closed.
    if (!widget || !buf || !buf->buf || buf->width <= 0 || buf->height <= 0 ||
        (unsigned)buf->orientation >= 8 ||
        buf->type_info.fmt_type == PAX_BUF_SUBTYPE_PALETTE ||
        right <= 0 || top < 0 || height < 20) return 0;
    // Callback, pixel-access and get_clip coordinates are logical/oriented;
    // the public width/height fields and dirty rectangle are physical storage.
    int display_width = pax_buf_get_width(buf), display_height = pax_buf_get_height(buf);
    if (right > display_width || top >= display_height || height > display_height - top) return 0;
    pax_recti clip = pax_get_clip(buf);
    if (clip.w <= 0 || clip.h <= 0 || top < clip.y ||
        (int64_t)top + height > (int64_t)clip.y + clip.h ||
        (int64_t)right > (int64_t)clip.x + clip.w) return 0;
    int left_limit = max_int(0, clip.x);
    if (right < left_limit || right - left_limit < 40) return 0;
    // Bound work even for unusually tall callback bands.
    height = min_int(height, 32);
    int width = min_int(96, min_int(2 * height + 32, right - left_limit));
    // The requested persistent widget uses the host callback's placement.
    // Background pixels are not layout metadata: probing them caused flicker.
    // Reserve the same width on every redraw, independent of sensor/content state.
    if (width < 40) return 0;

    int cell = (width - 8) / 2; // Two margins and a four-pixel center gap.
    // Restore large bodies; reserve horizontal space independently of height.
    // Normal 96x32 footprint: radius 14, +/-7px sideways and +/-1px vertically.
    int radius = min_int(14, min_int(cell / 2 - 1, height / 2 - 2));
    int travel_x = min_int(7, max_int(0, cell / 2 - radius - 1));
    int travel_y = min_int(2, max_int(0, height / 2 - radius - 1));
    if (radius < 6) return 0;
    float gx = 0, gy = 0;
    if (sample && sample->accel_ready && (uint32_t)(now - sample->sampled_at) <= 250 &&
        isfinite(sample->accel[0]) && isfinite(sample->accel[1]) && isfinite(sample->accel[2])) {
        gx = unit(sample->accel[0] / 9.80665f);
        gy = unit(sample->accel[1] / 9.80665f);
    }
    bool animated=sample && sample->animated && (uint32_t)(now-sample->sampled_at)<=250;
    const eyes_pose *pose=animated?&sample->pose:NULL;
    bool closed[2]={false,false};
    if (pose && pose->blink_id && (uint32_t)(now-pose->blink_at)<1500) {
        bool hidden=widget->has_drawn && (uint32_t)(now-widget->last_draw)>1500;
        if (hidden) {
            widget->blink_seen=pose->blink_id;
            widget->blink_presenting=false;
        } else if (widget->blink_seen!=pose->blink_id) {
            widget->blink_seen=pose->blink_id;
            widget->blink_drawn_at=now;
            widget->blink_presenting=true;
        }
        uint32_t age=now-widget->blink_drawn_at;
        uint32_t duration=pose->blink_duration<100?100:pose->blink_duration>650?650:pose->blink_duration;
        if(widget->blink_presenting) {
            closed[0]=closed[1]=age<duration;
            if(pose->blink_kind==1 && age>=duration+120 && age<2*duration+120)
                closed[0]=closed[1]=true;
            if(pose->blink_kind==2) closed[1]=age>=150 && age<duration+150;
        }
    }
    widget->last_draw=now; widget->has_drawn=true;
    // L1 projection is conservatively inside the circular pupil limit; no sqrt import.
    float norm = fabsf(gx) + fabsf(gy);
    if (norm > 1) { gx /= norm; gy /= norm; }
    // Launcher header primitives are queued by its asynchronous PAX renderer.
    // Direct pixel writes bypass that queue. Drain it FIRST, otherwise an older
    // header fill can overwrite some or all of this pair after we draw it.
    pax_join();
    for (int eye = 0; eye < 2; ++eye) {
        float bx=pose?unit(pose->body[eye][0]):gx, by=pose?unit(pose->body[eye][1]):gy;
        float ux=pose?unit(pose->pupil[eye][0]):gx, uy=pose?unit(pose->pupil[eye][1]):gy;
        float pupil_norm=fabsf(ux)+fabsf(uy);
        if(pupil_norm>1) { ux/=pupil_norm; uy/=pupil_norm; }
        eyes_variant variant=pose?pose->variant:EYES_CLASSIC;
        bool diamond=variant==EYES_DIAMOND;
        int rx=radius, ry=radius;
        if(variant==EYES_OVAL) ry=max_int(6,radius*4/5);
        if(variant==EYES_SLIT) ry=max_int(4,radius*3/5);
        int eye_pupil=max_int(2,min_int(rx,ry)*32/100);
        if(pose && isfinite(pose->pupil_scale))
            eye_pupil=max_int(2,(int)(eye_pupil*fmaxf(.6f,fminf(1,pose->pupil_scale))));
        int pupil_guard=diamond?2*eye_pupil:eye_pupil;
        int reach_x=max_int(0,rx-2-pupil_guard), reach_y=max_int(0,ry-2-pupil_guard);
        int cx = right - width + 2 + eye * (cell + 4) + cell / 2 + nearest(bx * travel_x);
        int cy = top + height / 2 + nearest(by * travel_y);
        int px = cx + (int)(ux * reach_x), py = cy + (int)(uy * reach_y);
        float aperture=pose && isfinite(pose->lid[eye])?fmaxf(0,fminf(1,pose->lid[eye])):1;
        if(closed[eye]) aperture=0;
        for (int y = cy - ry; y <= cy + ry; ++y) {
            for (int x = cx - rx; x <= cx + rx; ++x) {
                int dx = x - cx, dy = y - cy;
                if (!inside(dx,dy,rx,ry,diamond)) continue;
                int qx = x - px, qy = y - py;
                // Internal lids preserve a white rim, so the body remains visible
                // even with fully closed lids against a black header.
                bool lid=inside(dx,dy,rx-2,ry-2,diamond) && abs(dy) >= (int)(aperture*ry);
                bool pupil_pixel=qx*qx+qy*qy<=eye_pupil*eye_pupil;
                if(pose) switch(pose->variant) {
                    case EYES_OVAL: pupil_pixel=qx*qx*2+qy*qy<=eye_pupil*eye_pupil; break;
                    case EYES_SLIT: pupil_pixel=qx*qx*4+qy*qy<=eye_pupil*eye_pupil; break;
                    case EYES_DIAMOND: pupil_pixel=abs(qx)+abs(qy)<=eye_pupil; break;
                    default: break;
                }
                pax_col_t color = !inside(dx,dy,rx-1,ry-1,diamond) || lid ||
                    pupil_pixel ? 0xff000000 : 0xffffffff;
                pax_set_pixel(buf, color, x, y);
            }
        }
    }
    pax_vec2i a = pax_orient_det_vec2i(buf, (pax_vec2i){right - width, top});
    pax_vec2i b = pax_orient_det_vec2i(buf, (pax_vec2i){right - 1, top + height - 1});
    int dirty_x = min_int(a.x, b.x), dirty_y = min_int(a.y, b.y);
    pax_mark_dirty2(buf, dirty_x, dirty_y,
                    max_int(a.x, b.x) - dirty_x + 1, max_int(a.y, b.y) - dirty_y + 1);
    widget->preferred_width = width;
    return width;
}
