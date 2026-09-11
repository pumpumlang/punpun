@inject->c("""
/*
 * PunBrain 0.3.0
 * PunPun-native reimplementation of Ninjabrain Bot behavior.
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This implementation is a modified/translated work based on behavior and
 * algorithms from Ninjabrain Bot, Copyright its contributors.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#endif

extern int64_t pp_arg_count(void);
extern const char *pp_arg(int64_t index);

#define PB_VERSION "0.3.0"
#define PB_PI 3.1415926535897932384626433832795
#define PB_MAX_EYES 64
#define PB_MAX_CANDIDATES 180000
#define PB_TOP 8
#define PB_NUM_RINGS 8
#define PB_SNAPPING_RADIUS 7
#define PB_NUM_STRONGHOLDS 128
#define PB_GUI_PORT 52534
#define PB_CLIPBOARD_CAP 4096

typedef struct {
    double x;
    double z;
    double raw_angle;
    double angle;
    double vertical;
    double sigma;
    double correction;
    int correction_increments;
    int type;
} PbEye;

typedef struct {
    int cx;
    int cz;
    int ring;
    double weight;
    double p;
} PbCandidate;

typedef struct {
    PbEye eyes[PB_MAX_EYES];
    int eye_count;
    double sigma;
    double alt_sigma;
    double boat_sigma;
    double crosshair_correction;
    int target_coord;
    int locked;
    int advanced_stats;
    char last_clipboard[PB_CLIPBOARD_CAP];
    char status[256];
} PbState;

static PbCandidate *pb_candidates = NULL;
static int pb_candidate_count = 0;
static double pb_density[1540];
static int pb_density_len = 1540;
static int pb_density_ready = 0;
static PbState pb_state;

static double pb_clamp(double x, double lo, double hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static double pb_wrap(double a) {
    a = fmod(a, 360.0);
    if (a < -180.0) a += 360.0;
    if (a > 180.0) a -= 360.0;
    return a;
}

static double pb_bearing(double dx, double dz) {
    return -atan2(dx, dz) * 180.0 / PB_PI;
}

static double pb_angle_error(double a, double b) {
    return pb_wrap(a - b);
}

static double pb_correct_angle(double alpha, double crosshair) {
    alpha += crosshair;
    alpha -= 0.000824 * sin((alpha + 45.0) * PB_PI / 180.0);
    return pb_wrap(alpha);
}

static void pb_ring_counts(int out[PB_NUM_RINGS]) {
    int in_ring = 1;
    int total = 0;
    for (int ring = 0; ring < PB_NUM_RINGS; ++ring) {
        in_ring += 2 * in_ring / (ring + 1);
        if (in_ring > PB_NUM_STRONGHOLDS - total)
            in_ring = PB_NUM_STRONGHOLDS - total;
        total += in_ring;
        out[ring] = in_ring;
    }
}

static double pb_ring_inner(int ring) {
    return 32.0 * ((4.0 + ring * 6.0) - 1.25);
}

static double pb_ring_outer(int ring) {
    return 32.0 * ((4.0 + ring * 6.0) + 1.25);
}

static double pb_ring_inner_post(int ring) {
    return pb_ring_inner(ring) - (PB_SNAPPING_RADIUS + 1.0) * sqrt(2.0);
}

static double pb_ring_outer_post(int ring) {
    return pb_ring_outer(ring) + (PB_SNAPPING_RADIUS + 1.0) * sqrt(2.0);
}

static int pb_max_chunk(void) {
    return (int)ceil(pb_ring_outer_post(7));
}

static int pb_ring_for_radius(double r_chunks) {
    for (int r = 0; r < PB_NUM_RINGS; ++r)
        if (r_chunks >= pb_ring_inner_post(r) && r_chunks <= pb_ring_outer_post(r))
            return r;
    double best = 1e100;
    int which = 0;
    for (int r = 0; r < PB_NUM_RINGS; ++r) {
        double mid = 0.5 * (pb_ring_inner(r) + pb_ring_outer(r));
        double d = fabs(r_chunks - mid);
        if (d < best) { best = d; which = r; }
    }
    return which;
}

static int pb_java_floor_div4(int v) {
    if (v >= 0) return v / 4;
    return - ((-v + 3) / 4);
}

static void pb_init_density(void) {
    if (pb_density_ready) return;
    pb_density_ready = 1;
    memset(pb_density, 0, sizeof(pb_density));

    int counts[PB_NUM_RINGS];
    pb_ring_counts(counts);
    int maxc = pb_max_chunk();
    if (maxc + 5 < pb_density_len) pb_density_len = maxc + 5;

    double pre[1540];
    memset(pre, 0, sizeof(pre));
    for (int ring = 0; ring < PB_NUM_RINGS; ++ring) {
        int c0 = (int)pb_ring_inner(ring);
        int c1 = (int)pb_ring_outer(ring);
        for (int i = c0; i <= c1 && i < 1540; ++i) {
            if (i <= 0) continue;
            double rho = counts[ring] / (2.0 * PB_PI * (pb_ring_outer(ring) - pb_ring_inner(ring)) * i);
            if (i == c0 || i == c1) rho *= 0.5;
            pre[i] = rho;
        }
    }

    int offset_weights[15];
    memset(offset_weights, 0, sizeof(offset_weights));
    for (int i = -26; i <= 30; ++i) {
        int chunk_offset = pb_java_floor_div4(i);
        int key = -chunk_offset;
        if (key >= -7 && key <= 7) offset_weights[key + 7]++;
    }

    double filter[12];
    memset(filter, 0, sizeof(filter));
    double sum = 0.0;
    for (int k = -7; k <= 7; ++k) {
        int xw = offset_weights[k + 7];
        for (int l = -7; l <= 7; ++l) {
            int zw = offset_weights[l + 7];
            int w = xw * zw;
            for (int n = 0; n < 200; ++n) {
                double phi = 2.0 * PB_PI * n / 200.0;
                int dr = abs((int)llround(sqrt((double)(k*k + l*l)) * sin(phi)));
                if (dr < 12) filter[dr] += w;
                sum += (dr == 0 ? w : 2.0 * w);
            }
        }
    }
    if (sum <= 0.0) sum = 1.0;
    for (int i = 0; i < 12; ++i) filter[i] /= sum;

    for (int i = 0; i < pb_density_len; ++i) {
        double v = 0.0;
        for (int j = -11; j <= 11; ++j) {
            int src = i + j;
            if (src < 0 || src >= 1540) continue;
            int fj = abs(j);
            if (fj < 12) v += pre[src] * filter[fj];
        }
        pb_density[i] = v;
    }
}

static double pb_approx_density(double cx, double cz) {
    pb_init_density();
    double k = hypot(cx, cz);
    int i0 = (int)floor(k);
    int i1 = i0 + 1;
    if (i0 < 0 || i0 >= pb_density_len) return 0.0;
    if (i1 >= pb_density_len) return pb_density[i0];
    double t = k - i0;
    return (1.0 - t) * pb_density[i0] + t * pb_density[i1];
}

static double pb_position_variance(const PbEye *e, double tx, double tz) {
    double dx = tx - e->x;
    double dz = tz - e->z;
    double distance = hypot(dx, dz);
    if (distance < 1.0) distance = 1.0;

    double fx = e->x - floor(e->x);
    double fz = e->z - floor(e->z);
    int precise_corner =
        ((fabs(fx - 0.3) < 1e-7 || fabs(fx - 0.7) < 1e-7) &&
         (fabs(fz - 0.3) < 1e-7 || fabs(fz - 0.7) < 1e-7));
    if (precise_corner) return 0.0;

    double max_lateral_error = 0.005 * sqrt(2.0) * 180.0 / PB_PI;
    double angular = max_lateral_error / distance;
    return angular * angular / 6.0;
}

static double pb_eye_likelihood(const PbEye *e, int cx, int cz, int target_coord) {
    double tx = cx * 16.0 + target_coord;
    double tz = cz * 16.0 + target_coord;
    double gamma = pb_bearing(tx - e->x, tz - e->z);
    double delta = pb_angle_error(gamma, e->angle);
    double var = e->sigma * e->sigma + pb_position_variance(e, tx, tz);
    if (var < 1e-12) var = 1e-12;
    return exp(-(delta * delta) / (2.0 * var));
}

static int pb_add_candidate(int cx, int cz, double prior) {
    if (prior <= 0.0 || !isfinite(prior) || pb_candidate_count >= PB_MAX_CANDIDATES)
        return 0;
    PbCandidate *c = &pb_candidates[pb_candidate_count++];
    c->cx = cx;
    c->cz = cz;
    c->ring = pb_ring_for_radius(hypot((double)cx, (double)cz));
    c->weight = prior;
    c->p = 0.0;
    return 1;
}

static double pb_orth_component(double ax, double az, double ux, double uz) {
    double par = ux * ax + uz * az;
    double px = ux * par, pz = uz * par;
    double ox = px - ax, oz = pz - az;
    return uz * ox - ux * oz;
}

static double pb_project_major(double ax, double az, double ux, double uz, int major_x) {
    double p = ax * ux + az * uz;
    return major_x ? ux * p : uz * p;
}

static double pb_circle_intersection_major(double ox, double oz, double ux, double uz, double radius, int major_x) {
    double dot = ox * ux + oz * uz;
    double a = dot * dot + radius * radius - ox * ox - oz * oz;
    if (a < 0.0) return 0.0;
    double b = -dot - sqrt(a);
    return major_x ? ox + b * ux : oz + b * uz;
}

static double pb_iter_start_major(
    double om, double on, double ux, double uz, double vx, double vz,
    int major_x, int major_positive
) {
    int maxc = pb_max_chunk();
    if (om * om + on * on <= (double)maxc * maxc) return om;

    double ox = major_x ? om : on;
    double oz = major_x ? on : om;
    double uorth = pb_orth_component(-ox, -oz, ux, uz);
    double vorth = pb_orth_component(-ox, -oz, vx, vz);

    if (uorth > 0.0 && vorth < 0.0) {
        double mag = hypot(ox, oz);
        if (mag < 1e-12) return om;
        double ix = ox / mag * maxc;
        double iz = oz / mag * maxc;
        double m1 = om + pb_project_major(ix - ox, iz - oz, ux, uz, major_x);
        double m2 = om + pb_project_major(ix - ox, iz - oz, vx, vz, major_x);
        return (major_positive ^ (m1 > m2)) ? m1 : m2;
    }

    double iu = pb_circle_intersection_major(ox, oz, ux, uz, maxc, major_x);
    double iv = pb_circle_intersection_major(ox, oz, vx, vz, maxc, major_x);
    if (iu != 0.0 || iv != 0.0) {
        if (iu != 0.0 && iv != 0.0)
            return (major_positive ^ (iu > iv)) ? iu : iv;
        return iu != 0.0 ? iu : iv;
    }
    return om;
}

static void pb_build_prior(const PbEye *first, int target_coord) {
    pb_candidate_count = 0;
    double tolerance = fmin(1.0, 30.0 * first->sigma) * PB_PI / 180.0;
    if (tolerance < 0.00005) tolerance = 0.00005;
    double range = 5000.0 / 16.0;
    double phi = first->angle * PB_PI / 180.0;
    double dx = -sin(phi), dz = cos(phi);
    double ux = -sin(phi - tolerance), uz = cos(phi - tolerance);
    double vx = -sin(phi + tolerance), vz = cos(phi + tolerance);
    int major_x = cos(phi) * cos(phi) < 0.5;
    int major_positive = major_x ? (-sin(phi) > 0.0) : (cos(phi) > 0.0);

    double om = ((major_x ? first->x : first->z) - target_coord) / 16.0;
    double on = ((major_x ? first->z : first->x) - target_coord) / 16.0;
    double start = pb_iter_start_major(om, on, ux, uz, vx, vz, major_x, major_positive);
    double uk = major_x ? uz / ux : ux / uz;
    double vk = major_x ? vz / vx : vx / vz;
    int right_positive = major_positive ? (vk - uk > 0.0) : (uk - vk > 0.0);
    int i = major_positive ? (int)ceil(start) : (int)floor(start);
    int maxc = pb_max_chunk();

    while ((major_x ? (i - start) / dx : (i - start) / dz) < range) {
        if (i < -maxc - 2 || i > maxc + 2) {
            i += major_positive ? 1 : -1;
            if (abs(i) > maxc + 5000) break;
            continue;
        }
        double minor_u = on + uk * (i - om);
        double minor_v = on + vk * (i - om);
        int j = right_positive ? (int)ceil(minor_u) : (int)floor(minor_u);
        if (j < -maxc) j = -maxc;
        if (j > maxc) j = maxc;

        int guard = 0;
        while ((right_positive ? (j < minor_v) : (j > minor_v)) &&
               j <= maxc && j >= -maxc && guard++ < 2 * maxc + 4) {
            int cx = major_x ? i : j;
            int cz = major_x ? j : i;
            double weight = 0.0;
            for (int k = 0; k < 2; ++k) {
                double sx = cx - 0.5 + k;
                for (int l = 0; l < 2; ++l) {
                    double sz = cz - 0.5 + l;
                    weight += pb_approx_density(sx, sz);
                }
            }
            weight *= 0.25;
            pb_add_candidate(cx, cz, weight);
            j += right_positive ? 1 : -1;
        }
        i += major_positive ? 1 : -1;
        if (pb_candidate_count >= PB_MAX_CANDIDATES) break;
    }
}

static int pb_cmp_candidate(const void *aa, const void *bb) {
    const PbCandidate *a = (const PbCandidate *)aa;
    const PbCandidate *b = (const PbCandidate *)bb;
    if (a->weight < b->weight) return 1;
    if (a->weight > b->weight) return -1;
    if (a->cx != b->cx) return a->cx < b->cx ? -1 : 1;
    return a->cz < b->cz ? -1 : (a->cz > b->cz);
}

static int pb_solve(const PbEye *eyes, int n, int target_coord) {
    if (n < 1) return 0;
    pb_build_prior(&eyes[0], target_coord);
    if (pb_candidate_count == 0) return 0;

    for (int i = 0; i < pb_candidate_count; ++i) {
        double w = pb_candidates[i].weight;
        for (int e = 0; e < n; ++e) {
            w *= pb_eye_likelihood(&eyes[e], pb_candidates[i].cx, pb_candidates[i].cz, target_coord);
            if (w < 1e-300) { w = 0.0; break; }
        }
        pb_candidates[i].weight = w;
    }

    double total = 0.0;
    for (int i = 0; i < pb_candidate_count; ++i) total += pb_candidates[i].weight;
    if (!(total > 0.0) || !isfinite(total)) {
        double maxlog = -1e300;
        for (int i = 0; i < pb_candidate_count; ++i) {
            double lp = log(pb_approx_density(pb_candidates[i].cx, pb_candidates[i].cz) + 1e-300);
            for (int e = 0; e < n; ++e) {
                double tx = pb_candidates[i].cx * 16.0 + target_coord;
                double tz = pb_candidates[i].cz * 16.0 + target_coord;
                double gamma = pb_bearing(tx - eyes[e].x, tz - eyes[e].z);
                double delta = pb_angle_error(gamma, eyes[e].angle);
                double var = eyes[e].sigma * eyes[e].sigma + pb_position_variance(&eyes[e], tx, tz);
                lp += -(delta * delta)/(2.0 * fmax(var,1e-12));
            }
            pb_candidates[i].weight = lp;
            if (lp > maxlog) maxlog = lp;
        }
        total = 0.0;
        for (int i = 0; i < pb_candidate_count; ++i) {
            pb_candidates[i].weight = exp(pb_candidates[i].weight - maxlog);
            total += pb_candidates[i].weight;
        }
    }
    if (!(total > 0.0)) return 0;

    for (int i = 0; i < pb_candidate_count; ++i) {
        pb_candidates[i].p = pb_candidates[i].weight / total;
        pb_candidates[i].weight = pb_candidates[i].p;
    }
    qsort(pb_candidates, (size_t)pb_candidate_count, sizeof(PbCandidate), pb_cmp_candidate);
    return 1;
}

static void pb_state_init(void) {
    memset(&pb_state, 0, sizeof(pb_state));
    pb_state.sigma = 0.03;
    pb_state.alt_sigma = 0.06;
    pb_state.boat_sigma = 0.004;
    pb_state.crosshair_correction = 0.0;
    pb_state.target_coord = 8;
    pb_state.advanced_stats = 1;
    snprintf(pb_state.status, sizeof(pb_state.status), "Waiting for F3+C eye measurement");
    if (!pb_candidates)
        pb_candidates = (PbCandidate *)calloc(PB_MAX_CANDIDATES, sizeof(PbCandidate));
}

static int pb_parse_double(const char *s, double *out) {
    if (!s || !*s) return 0;
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || errno || !isfinite(v)) return 0;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return 0;
    *out = v;
    return 1;
}

static int pb_parse_f3c(const char *input, PbEye *out, double sigma, double crosshair) {
    if (!input || strncmp(input, "/execute in ", 12) != 0) return 0;
    char buf[PB_CLIPBOARD_CAP];
    snprintf(buf, sizeof(buf), "%s", input);
    char *tokens[16];
    int n = 0;
    char *save = NULL;
    for (char *t = strtok_r(buf, " \r\n\t", &save); t && n < 16; t = strtok_r(NULL, " \r\n\t", &save))
        tokens[n++] = t;
    if (n != 11) return 0;
    if (!strstr(tokens[2], "overworld")) return 0;

    double x,y,z,yaw,pitch;
    if (!pb_parse_double(tokens[6], &x) || !pb_parse_double(tokens[7], &y) ||
        !pb_parse_double(tokens[8], &z) || !pb_parse_double(tokens[9], &yaw) ||
        !pb_parse_double(tokens[10], &pitch)) return 0;
    (void)y;
    memset(out, 0, sizeof(*out));
    out->x = x;
    out->z = z;
    out->raw_angle = pb_wrap(yaw);
    out->angle = pb_correct_angle(yaw, crosshair);
    out->vertical = pitch;
    out->sigma = sigma;
    return 1;
}

static int pb_parse_manual(const char *input, PbEye *out, double sigma, double crosshair) {
    if (!input) return 0;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", input);
    char *tokens[5];
    int n = 0;
    char *save = NULL;
    for (char *t = strtok_r(buf, " ,\r\n\t", &save); t && n < 5; t = strtok_r(NULL, " ,\r\n\t", &save))
        tokens[n++] = t;
    if (n < 3 || n > 4) return 0;
    double x,z,a;
    if (!pb_parse_double(tokens[0], &x) || !pb_parse_double(tokens[1], &z) || !pb_parse_double(tokens[2], &a))
        return 0;
    memset(out,0,sizeof(*out));
    out->x=x; out->z=z; out->raw_angle=pb_wrap(a); out->angle=pb_correct_angle(a,crosshair);
    out->sigma=sigma;
    if (n == 4) {
        out->correction_increments = atoi(tokens[3]);
        out->correction = 0.01 * out->correction_increments;
        out->angle = pb_wrap(out->angle + out->correction);
    }
    return 1;
}

static int pb_add_eye(PbEye eye) {
    if (pb_state.locked || pb_state.eye_count >= PB_MAX_EYES) return 0;
    pb_state.eyes[pb_state.eye_count++] = eye;
    snprintf(pb_state.status, sizeof(pb_state.status), "Eye %d recorded", pb_state.eye_count);
    return 1;
}

static void pb_undo(void) {
    if (!pb_state.locked && pb_state.eye_count > 0) {
        pb_state.eye_count--;
        snprintf(pb_state.status, sizeof(pb_state.status), "Undid last eye");
    }
}

static void pb_reset(void) {
    if (!pb_state.locked) {
        pb_state.eye_count = 0;
        snprintf(pb_state.status, sizeof(pb_state.status), "Reset");
    }
}

static void pb_adjust_last(double delta) {
    if (!pb_state.locked && pb_state.eye_count > 0) {
        PbEye *e = &pb_state.eyes[pb_state.eye_count - 1];
        e->correction += delta;
        e->correction_increments += (delta > 0 ? 1 : -1);
        e->angle = pb_wrap(pb_correct_angle(e->raw_angle, pb_state.crosshair_correction) + e->correction);
        snprintf(pb_state.status, sizeof(pb_state.status), "Last eye adjusted %+.2f deg", delta);
    }
}

static int pb_read_pipe(const char *cmd, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
#ifdef _WIN32
    FILE *f = _popen(cmd, "rb");
#else
    FILE *f = popen(cmd, "r");
#endif
    if (!f) return 0;
    size_t n = fread(out, 1, cap - 1, f);
    out[n] = '\0';
#ifdef _WIN32
    _pclose(f);
#else
    pclose(f);
#endif
    while (n && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = '\0';
    return n > 0;
}

static int pb_clipboard(char *out, size_t cap) {
#ifdef _WIN32
    return pb_read_pipe("powershell -NoProfile -Command \"Get-Clipboard -Raw\" 2>NUL", out, cap);
#elif defined(__APPLE__)
    return pb_read_pipe("pbpaste 2>/dev/null", out, cap);
#else
    if (getenv("WAYLAND_DISPLAY") && pb_read_pipe("wl-paste -n 2>/dev/null", out, cap)) return 1;
    if (pb_read_pipe("xclip -selection clipboard -o 2>/dev/null", out, cap)) return 1;
    if (pb_read_pipe("xsel --clipboard --output 2>/dev/null", out, cap)) return 1;
    return 0;
#endif
}

static int pb_poll_clipboard(void) {
    char clip[PB_CLIPBOARD_CAP];
    if (!pb_clipboard(clip, sizeof(clip))) return 0;
    if (strcmp(clip, pb_state.last_clipboard) == 0) return 0;
    snprintf(pb_state.last_clipboard, sizeof(pb_state.last_clipboard), "%s", clip);
    PbEye e;
    if (pb_parse_f3c(clip, &e, pb_state.sigma, pb_state.crosshair_correction)) {
        return pb_add_eye(e);
    }
    return 0;
}

static void pb_json_escape(FILE *f, const char *s) {
    for (; s && *s; ++s) {
        unsigned char c=(unsigned char)*s;
        if (c=='\"' || c=='\\') { fputc('\\',f); fputc(c,f); }
        else if (c=='\n') fputs("\\n",f);
        else if (c=='\r') fputs("\\r",f);
        else if (c=='\t') fputs("\\t",f);
        else if (c>=32) fputc(c,f);
    }
}

static void pb_write_state_json(FILE *f) {
    int solved = pb_state.eye_count > 0 && pb_solve(pb_state.eyes, pb_state.eye_count, pb_state.target_coord);
    fprintf(f, "{\"name\":\"PunBrain\",\"version\":\"%s\",\"eyeCount\":%d,\"locked\":%s,",
            PB_VERSION, pb_state.eye_count, pb_state.locked?"true":"false");
    fprintf(f, "\"sigma\":%.6f,\"crosshair\":%.6f,\"mcTarget\":%d,\"status\":\"",
            pb_state.sigma,pb_state.crosshair_correction,pb_state.target_coord);
    pb_json_escape(f,pb_state.status);
    fputs("\",\"eyes\":[",f);
    for(int i=0;i<pb_state.eye_count;i++){
        if(i)fputc(',',f);
        PbEye *e=&pb_state.eyes[i];
        fprintf(f,"{\"x\":%.5f,\"z\":%.5f,\"raw\":%.7f,\"angle\":%.7f,\"sigma\":%.6f,\"corr\":%.4f}",
                e->x,e->z,e->raw_angle,e->angle,e->sigma,e->correction);
    }
    fputs("],\"result\":",f);
    if(!solved){
        fputs("null}",f);
        return;
    }
    PbCandidate *best=&pb_candidates[0];
    PbEye *last=&pb_state.eyes[pb_state.eye_count-1];
    double tx=best->cx*16.0+pb_state.target_coord;
    double tz=best->cz*16.0+pb_state.target_coord;
    double dist=hypot(tx-last->x,tz-last->z);
    double dir=pb_bearing(tx-last->x,tz-last->z);
    fprintf(f,"{\"x\":%.3f,\"z\":%.3f,\"chunkX\":%d,\"chunkZ\":%d,\"ring\":%d,\"certainty\":%.10f,\"distance\":%.3f,\"direction\":%.7f,\"candidates\":[",
            tx,tz,best->cx,best->cz,best->ring,best->p,dist,dir);
    int top=pb_candidate_count<PB_TOP?pb_candidate_count:PB_TOP;
    for(int i=0;i<top;i++){
        if(i)fputc(',',f);
        PbCandidate *c=&pb_candidates[i];
        fprintf(f,"{\"x\":%.3f,\"z\":%.3f,\"chunkX\":%d,\"chunkZ\":%d,\"p\":%.10f}",
                c->cx*16.0+pb_state.target_coord,c->cz*16.0+pb_state.target_coord,c->cx,c->cz,c->p);
    }
    fputs("]}}",f);
}

static void pb_print_state(void) {
    pb_write_state_json(stdout);
    fputc('\n',stdout);
}

static void pb_print_blind(double nx,double nz) {
    double ox=nx*8.0, oz=nz*8.0, r=hypot(ox,oz)/16.0;
    int best_ring=0;
    double bestd=1e100, mid=0;
    for(int ring=0;ring<PB_NUM_RINGS;ring++){
        double m=.5*(pb_ring_inner(ring)+pb_ring_outer(ring));
        double d=fabs(r-m);
        if(d<bestd){bestd=d;best_ring=ring;mid=m;}
    }
    double br=hypot(ox,oz);
    double ux=0,uz=1;
    if(br>1e-9){ux=ox/br;uz=oz/br;}
    double tx=ux*mid*16.0,tz=uz*mid*16.0;
    double nd=hypot(tx-ox,tz-oz)/8.0;
    double dir=pb_bearing(tx-ox,tz-oz);
    double highroll=exp(-0.5*pow((bestd*16.0)/400.0,2.0));
    printf("{\"mode\":\"blind\",\"netherX\":%.3f,\"netherZ\":%.3f,\"ring\":%d,\"optimalNetherX\":%.3f,\"optimalNetherZ\":%.3f,\"distance\":%.3f,\"direction\":%.6f,\"highrollApprox\":%.8f}\n",
           nx,nz,best_ring,tx/8.0,tz/8.0,nd,dir,highroll);
}

static void pb_calibrate(int argc,int start) {
    double vals[512];
    int n=0;
    for(int i=start;i<argc && n<512;i++){
        double x;
        if(pb_parse_double(pp_arg(i),&x)) vals[n++]=x;
    }
    if(n<2){puts("{\"ok\":false,\"error\":\"calibrate needs at least two angular-error samples\"}");return;}
    double mean=0; for(int i=0;i<n;i++)mean+=vals[i]; mean/=n;
    double var=0; for(int i=0;i<n;i++){double d=vals[i]-mean;var+=d*d;} var/=n;
    double sigma=sqrt(var);
    printf("{\"mode\":\"calibrate\",\"samples\":%d,\"bias\":%.9f,\"sigma\":%.9f}\n",n,mean,pb_clamp(sigma,0.001,2.0));
}

static const char *PB_HTML =
"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>PunBrain</title><style>"
":root{color-scheme:dark;font-family:Inter,system-ui,sans-serif}*{box-sizing:border-box}body{margin:0;background:#0b0e14;color:#e7eaf0}"
".top{height:52px;display:flex;align-items:center;padding:0 18px;border-bottom:1px solid #252a36;background:#11151e;gap:12px}.brand{font-weight:800;font-size:20px}.tag{font-size:12px;color:#8f98aa}.dot{width:8px;height:8px;border-radius:50%;background:#66d17a}"
".wrap{max-width:1040px;margin:18px auto;padding:0 14px}.grid{display:grid;grid-template-columns:1.3fr .7fr;gap:14px}.card{background:#121722;border:1px solid #272d3b;border-radius:12px;padding:15px;box-shadow:0 10px 30px #0004}.hero{font-size:38px;font-weight:850;letter-spacing:-1px}.muted{color:#8f98aa}.prob{font-size:18px;font-weight:700;color:#80e59a}"
"button,input,select{background:#1a2130;color:#eef;border:1px solid #343d50;border-radius:8px;padding:9px 11px}button{cursor:pointer}button:hover{background:#242d3e}.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.space{justify-content:space-between}table{width:100%;border-collapse:collapse;margin-top:8px}td,th{padding:8px;border-bottom:1px solid #252b38;text-align:right;font-variant-numeric:tabular-nums}th:first-child,td:first-child{text-align:left}.small{font-size:12px}.footer{margin:14px 0;color:#687286;text-align:center}@media(max-width:760px){.grid{grid-template-columns:1fr}.hero{font-size:30px}}"
"</style></head><body><div class='top'><div class='dot'></div><div class='brand'>PunBrain</div><div class='tag'>PunPun native stronghold calculator</div></div>"
"<div class='wrap'><div class='grid'><div class='card'><div class='row space'><div><div class='muted small'>BEST PREDICTION</div><div id='pos' class='hero'>Waiting for eye...</div><div id='meta' class='muted'></div></div><div id='prob' class='prob'></div></div>"
"<div class='row' style='margin-top:14px'><button onclick=\"act('undo')\">Undo</button><button onclick=\"act('reset')\">Reset</button><button onclick=\"act('minus')\">-0.01°</button><button onclick=\"act('plus')\">+0.01°</button><button id='lock' onclick=\"act('lock')\">Lock</button></div>"
"<h3>Predictions</h3><table><thead><tr><th>#</th><th>Coordinates</th><th>Chunk</th><th>Certainty</th></tr></thead><tbody id='pred'></tbody></table></div>"
"<div class='card'><h3 style='margin-top:0'>Input</h3><div id='status' class='muted small'>Starting...</div><p class='small muted'>Throw an Eye of Ender, aim at it, then press <b>F3+C</b>. PunBrain watches the clipboard and records the copied position/yaw itself.</p>"
"<div class='row'><input id='manual' placeholder='x z yaw' style='flex:1'><button onclick='manualAdd()'>Add</button></div><h3>Settings</h3><label class='small muted'>Standard deviation</label><input id='sigma' type='number' step='0.001' min='0.001' max='2' style='width:100%'><label class='small muted'>Crosshair correction</label><input id='cross' type='number' step='0.001' style='width:100%'><label class='small muted'>Minecraft target</label><select id='mc' style='width:100%'><option value='8'>1.18 and older (8,8)</option><option value='0'>1.19+ (0,0)</option></select><button style='margin-top:9px;width:100%' onclick='cfg()'>Apply settings</button><h3>Eyes</h3><div id='eyes'></div></div></div>"
"<div class='card' style='margin-top:14px'><h3 style='margin-top:0'>Blind travel</h3><div class='row'><input id='bx' placeholder='Nether X'><input id='bz' placeholder='Nether Z'><button onclick='blindCalc()'>Calculate</button></div><pre id='blindout'></pre></div><div class='footer small'>PunBrain is GPLv3. Ninjabrain Bot algorithms/behavior are credited in NOTICE.md. New executable, not official MCSR approval.</div></div>"
"<script>const q=s=>document.querySelector(s);async function get(){let s=await(await fetch('/state')).json();q('#status').textContent=s.status+' • '+s.eyeCount+' eye(s)';q('#sigma').value=s.sigma;q('#cross').value=s.crosshair;q('#mc').value=s.mcTarget;q('#lock').textContent=s.locked?'Unlock':'Lock';let r=s.result;if(!r){q('#pos').textContent='Waiting for eye...';q('#meta').textContent='Press F3+C while aimed at the eye';q('#prob').textContent='';q('#pred').innerHTML='';}else{q('#pos').textContent=Math.round(r.x)+', '+Math.round(r.z);q('#meta').textContent='Chunk '+r.chunkX+', '+r.chunkZ+' • '+Math.round(r.distance)+' blocks • '+r.direction.toFixed(2)+'°';q('#prob').textContent=(r.certainty*100).toFixed(2)+'%';q('#pred').innerHTML=r.candidates.map((c,i)=>`<tr><td>${i+1}</td><td>${Math.round(c.x)}, ${Math.round(c.z)}</td><td>${c.chunkX}, ${c.chunkZ}</td><td>${(c.p*100).toFixed(2)}%</td></tr>`).join('');}q('#eyes').innerHTML=s.eyes.map((e,i)=>`<div class='small'>#${i+1} ${e.x.toFixed(1)}, ${e.z.toFixed(1)} · ${e.angle.toFixed(4)}° <span class='muted'>σ ${e.sigma}</span></div>`).join('');}async function act(c){await fetch('/action?cmd='+c);get()}async function cfg(){await fetch(`/config?sigma=${q('#sigma').value}&cross=${q('#cross').value}&mc=${q('#mc').value}`);get()}async function manualAdd(){await fetch('/manual?v='+encodeURIComponent(q('#manual').value));q('#manual').value='';get()}async function blindCalc(){q('#blindout').textContent=await(await fetch(`/blind?x=${q('#bx').value}&z=${q('#bz').value}`)).text()}setInterval(get,250);get();</script></body></html>";

static void pb_open_browser(void) {
    char cmd[512];
#ifdef _WIN32
    snprintf(cmd,sizeof(cmd),"start \"\" http://127.0.0.1:%d",PB_GUI_PORT);
#elif defined(__APPLE__)
    snprintf(cmd,sizeof(cmd),"open http://127.0.0.1:%d >/dev/null 2>&1 &",PB_GUI_PORT);
#else
    snprintf(cmd,sizeof(cmd),"xdg-open http://127.0.0.1:%d >/dev/null 2>&1 &",PB_GUI_PORT);
#endif
    system(cmd);
}

static const char *pb_query_value(const char *path,const char *key,char *out,size_t cap) {
    const char *q=strchr(path,'?'); if(!q)return NULL; q++;
    size_t kl=strlen(key);
    while(*q){
        if(strncmp(q,key,kl)==0 && q[kl]=='='){
            q+=kl+1; size_t n=0;
            while(*q && *q!='&' && n+1<cap){
                if(*q=='%' && isxdigit((unsigned char)q[1]) && isxdigit((unsigned char)q[2])){
                    char hex[3]={q[1],q[2],0}; out[n++]=(char)strtol(hex,NULL,16); q+=3;
                }else if(*q=='+'){out[n++]=' ';q++;} else out[n++]=*q++;
            }
            out[n]=0; return out;
        }
        q=strchr(q,'&'); if(!q)return NULL; q++;
    }
    return NULL;
}

static void pb_http_send(int fd,const char *type,const char *body) {
    char header[512];
    size_t len=strlen(body);
    int n=snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n",type,len);
#ifdef _WIN32
    send(fd,header,n,0); send(fd,body,(int)len,0);
#else
    write(fd,header,(size_t)n); write(fd,body,len);
#endif
}

static void pb_http_send_state(int fd) {
    FILE *tmp=tmpfile();
    if(!tmp){pb_http_send(fd,"application/json","{}");return;}
    pb_write_state_json(tmp); fflush(tmp); fseek(tmp,0,SEEK_END); long n=ftell(tmp); rewind(tmp);
    char *buf=(char*)malloc((size_t)n+1); if(!buf){fclose(tmp);pb_http_send(fd,"application/json","{}");return;}
    fread(buf,1,(size_t)n,tmp);buf[n]=0;fclose(tmp);pb_http_send(fd,"application/json",buf);free(buf);
}

static void pb_handle_http(int fd,const char *path) {
    if(strcmp(path,"/")==0){pb_http_send(fd,"text/html; charset=utf-8",PB_HTML);return;}
    if(strncmp(path,"/state",6)==0){pb_http_send_state(fd);return;}
    if(strncmp(path,"/action",7)==0){
        char cmd[64];
        if(pb_query_value(path,"cmd",cmd,sizeof(cmd))){
            if(strcmp(cmd,"reset")==0)pb_reset();
            else if(strcmp(cmd,"undo")==0)pb_undo();
            else if(strcmp(cmd,"plus")==0)pb_adjust_last(.01);
            else if(strcmp(cmd,"minus")==0)pb_adjust_last(-.01);
            else if(strcmp(cmd,"lock")==0)pb_state.locked=!pb_state.locked;
        }
        pb_http_send(fd,"application/json","{\"ok\":true}");return;
    }
    if(strncmp(path,"/config",7)==0){
        char v[128]; double d;
        if(pb_query_value(path,"sigma",v,sizeof(v))&&pb_parse_double(v,&d))pb_state.sigma=pb_clamp(fabs(d),.001,2);
        if(pb_query_value(path,"cross",v,sizeof(v))&&pb_parse_double(v,&d))pb_state.crosshair_correction=pb_clamp(d,-2,2);
        if(pb_query_value(path,"mc",v,sizeof(v)))pb_state.target_coord=atoi(v)==0?0:8;
        for(int i=0;i<pb_state.eye_count;i++){
            pb_state.eyes[i].angle=pb_wrap(pb_correct_angle(pb_state.eyes[i].raw_angle,pb_state.crosshair_correction)+pb_state.eyes[i].correction);
        }
        pb_http_send(fd,"application/json","{\"ok\":true}");return;
    }
    if(strncmp(path,"/manual",7)==0){
        char v[512]; PbEye e;
        if(pb_query_value(path,"v",v,sizeof(v))&&pb_parse_manual(v,&e,pb_state.sigma,pb_state.crosshair_correction))pb_add_eye(e);
        pb_http_send(fd,"application/json","{\"ok\":true}");return;
    }
    if(strncmp(path,"/blind",6)==0){
        char xs[64]={0},zs[64]={0}; double x=0,z=0;
        pb_query_value(path,"x",xs,sizeof(xs));pb_query_value(path,"z",zs,sizeof(zs));pb_parse_double(xs,&x);pb_parse_double(zs,&z);
        char b[512];
        double ox=x*8.0,oz=z*8.0,r=hypot(ox,oz)/16.0;int ring=pb_ring_for_radius(r);
        double mid=.5*(pb_ring_inner(ring)+pb_ring_outer(ring))*16.0,br=hypot(ox,oz),ux=0,uz=1;if(br>1e-9){ux=ox/br;uz=oz/br;}
        snprintf(b,sizeof(b),"{\"ring\":%d,\"optimalNetherX\":%.2f,\"optimalNetherZ\":%.2f}",ring,ux*mid/8.0,uz*mid/8.0);
        pb_http_send(fd,"application/json",b);return;
    }
    pb_http_send(fd,"application/json","{\"ok\":false,\"error\":\"not found\"}");
}

static int pb_gui(void) {
#ifdef _WIN32
    WSADATA wsa; if(WSAStartup(MAKEWORD(2,2),&wsa)!=0)return 2;
#endif
    int s=(int)socket(AF_INET,SOCK_STREAM,0); if(s<0){perror("socket");return 2;}
    int yes=1;
#ifndef _WIN32
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
#else
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof(yes));
#endif
    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(PB_GUI_PORT);
    if(bind(s,(struct sockaddr*)&addr,sizeof(addr))<0){perror("bind");return 2;}
    if(listen(s,8)<0){perror("listen");return 2;}
#ifndef _WIN32
    fcntl(s,F_SETFL,fcntl(s,F_GETFL,0)|O_NONBLOCK);
#endif
    printf("PunBrain %s GUI: http://127.0.0.1:%d\n",PB_VERSION,PB_GUI_PORT);
    printf("F3+C clipboard capture is active. Ctrl+C stops PunBrain.\n");
    fflush(stdout);pb_open_browser();

    for(;;){
        pb_poll_clipboard();
        fd_set r;FD_ZERO(&r);FD_SET(s,&r);struct timeval tv={0,120000};
        int ready=select(s+1,&r,NULL,NULL,&tv);
        if(ready>0&&FD_ISSET(s,&r)){
            int c=(int)accept(s,NULL,NULL);if(c>=0){
                char req[8192];int n=(int)recv(c,req,sizeof(req)-1,0);if(n>0){
                    req[n]=0;char method[16],path[4096];method[0]=path[0]=0;
                    sscanf(req,"%15s %4095s",method,path);
                    if(strcmp(method,"GET")==0)pb_handle_http(c,path);
                }
#ifdef _WIN32
                closesocket(c);
#else
                close(c);
#endif
            }
        }
    }
}

static void pb_help(void) {
    puts("PunBrain " PB_VERSION " - Ninjabrain-style stronghold calculator written for PunPun");
    puts("  gui                              launch standalone GUI + F3+C clipboard watcher");
    puts("  watch                            watch F3+C clipboard in terminal");
    puts("  solve x z yaw sigma [x z yaw sigma ...]");
    puts("  parse \"<F3+C command>\"           parse one copied Minecraft command");
    puts("  blind netherX netherZ            blind-coordinate evaluation");
    puts("  calibrate error [error ...]      estimate measurement sigma");
    puts("  selftest                         run deterministic core checks");
    puts("  version");
}

static int pb_selftest(void) {
    int counts[8];pb_ring_counts(counts);
    int expect[8]={3,6,10,15,21,28,36,9};
    for(int i=0;i<8;i++)if(counts[i]!=expect[i]){fprintf(stderr,"ring count fail %d\n",i);return 2;}
    if(fabs(pb_correct_angle(0,0)-(-0.000824*sin(45*PB_PI/180.0)))>1e-10)return 3;
    PbEye a={0};a.x=0;a.z=0;a.raw_angle=-45;a.angle=pb_correct_angle(-45,0);a.sigma=.03;
    PbEye b={0};b.x=100;b.z=0;b.raw_angle=-48;b.angle=pb_correct_angle(-48,0);b.sigma=.03;
    PbEye es[2]={a,b};
    if(!pb_solve(es,2,8)||pb_candidate_count<1)return 4;
    printf("PunBrain selftest OK: rings, angle correction, prior, posterior (%d candidates)\n",pb_candidate_count);
    return 0;
}

int64_t punbrain_main(void) {
    pb_state_init();
    const int argc=(int)pp_arg_count();
    if(argc==0){pb_help();return 0;}
    const char *mode=pp_arg(0);

    if(strcmp(mode,"version")==0||strcmp(mode,"--version")==0){
        puts("PunBrain " PB_VERSION " (PunPun standalone)");
        return 0;
    }
    if(strcmp(mode,"selftest")==0)return pb_selftest();
    if(strcmp(mode,"gui")==0)return pb_gui();
    if(strcmp(mode,"watch")==0){
        puts("PunBrain clipboard watch. Press F3+C while aiming at an Eye of Ender. Ctrl+C exits.");
        for(;;){
            if(pb_poll_clipboard())pb_print_state();
#ifdef _WIN32
            Sleep(150);
#else
            struct timespec ts={0,150000000L}; nanosleep(&ts,NULL);
#endif
        }
    }
    if(strcmp(mode,"parse")==0){
        if(argc<2){puts("{\"ok\":false,\"error\":\"parse needs quoted F3+C command\"}");return 2;}
        PbEye e;if(!pb_parse_f3c(pp_arg(1),&e,pb_state.sigma,pb_state.crosshair_correction)){puts("{\"ok\":false,\"error\":\"not a valid Overworld F3+C command\"}");return 2;}
        printf("{\"ok\":true,\"x\":%.6f,\"z\":%.6f,\"rawAngle\":%.9f,\"angle\":%.9f,\"vertical\":%.6f}\n",e.x,e.z,e.raw_angle,e.angle,e.vertical);return 0;
    }
    if(strcmp(mode,"solve")==0){
        if(argc<5||((argc-1)%4)!=0){puts("{\"ok\":false,\"error\":\"usage: solve x z yaw sigma [x z yaw sigma ...]\"}");return 2;}
        int n=(argc-1)/4;if(n>PB_MAX_EYES)return 2;
        for(int i=0;i<n;i++){
            double x,z,a,sig;int k=1+i*4;
            if(!pb_parse_double(pp_arg(k),&x)||!pb_parse_double(pp_arg(k+1),&z)||!pb_parse_double(pp_arg(k+2),&a)||!pb_parse_double(pp_arg(k+3),&sig))return 2;
            PbEye e={0};e.x=x;e.z=z;e.raw_angle=pb_wrap(a);e.angle=pb_correct_angle(a,0);e.sigma=pb_clamp(fabs(sig),.001,2);pb_state.eyes[pb_state.eye_count++]=e;
        }
        pb_print_state();return 0;
    }
    if(strcmp(mode,"blind")==0){
        double x,z;if(argc!=3||!pb_parse_double(pp_arg(1),&x)||!pb_parse_double(pp_arg(2),&z))return 2;pb_print_blind(x,z);return 0;
    }
    if(strcmp(mode,"calibrate")==0){pb_calibrate(argc,1);return 0;}
    pb_help();return 2;
}
""");

extern native fn punbrain_main() -> i64;

launch {
    unsafe {
        let status = punbrain_main();
    }
}
