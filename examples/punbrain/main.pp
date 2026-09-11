@inject->c("""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern int64_t pp_arg_count(void);
extern const char *pp_arg(int64_t index);

#define PB_PI 3.14159265358979323846
#define PB_MAX_THROWS 32
#define PB_MAX_CANDIDATES 24000
#define PB_TOP 5

typedef struct {
    double x;
    double z;
    double bearing;
    double sigma;
} PbThrow;

typedef struct {
    int chunk_x;
    int chunk_z;
    int ring;
    double x;
    double z;
    double logp;
    double probability;
} PbCandidate;

static PbCandidate pb_candidates[PB_MAX_CANDIDATES];
static int pb_candidate_count = 0;

static double pb_clamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static double pb_wrap_deg(double a) {
    while (a <= -180.0) a += 360.0;
    while (a > 180.0) a -= 360.0;
    return a;
}

static double pb_bearing(double dx, double dz) {
    return -atan2(dx, dz) * 180.0 / PB_PI;
}

static double pb_distance(double x0, double z0, double x1, double z1) {
    return hypot(x1 - x0, z1 - z0);
}

static int pb_parse_double(const char *s, double *out) {
    if (!s || !*s) return 0;
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || !isfinite(v)) return 0;
    *out = v;
    return 1;
}

static int pb_parse_int(const char *s, int *out) {
    if (!s || !*s) return 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return 0;
    *out = (int)v;
    return 1;
}

/*
 * Vanilla stronghold ring envelope. The phase of each ring is seed-dependent,
 * so PunBrain uses the ring as a prior and lets measured eye bearings provide
 * the angular evidence.
 */
static void pb_ring_bounds(int ring, double *inner_blocks, double *outer_blocks) {
    const double dist_param = 32.0;
    const double snap = 8.0 * sqrt(2.0);
    const double center = dist_param * (4.0 + 6.0 * ring);
    const double half_width = dist_param * 1.25;
    *inner_blocks = (center - half_width - snap) * 16.0;
    *outer_blocks = (center + half_width + snap) * 16.0;
}

static double pb_ring_log_prior(double x, double z, int *best_ring) {
    const double r = hypot(x, z);
    double best = -1e30;
    int ring_out = 0;
    for (int ring = 0; ring < 8; ++ring) {
        double inner, outer;
        pb_ring_bounds(ring, &inner, &outer);
        double lp;
        if (r >= inner && r <= outer) {
            const double center = 0.5 * (inner + outer);
            const double width = 0.5 * (outer - inner);
            const double t = (r - center) / width;
            lp = -log(outer * outer - inner * inner) - 0.08 * t * t;
        } else {
            const double d = r < inner ? inner - r : r - outer;
            lp = -18.0 - 0.5 * (d / 96.0) * (d / 96.0);
        }
        if (lp > best) {
            best = lp;
            ring_out = ring;
        }
    }
    if (best_ring) *best_ring = ring_out;
    return best;
}

static double pb_throw_log_likelihood(const PbThrow *t, double x, double z) {
    const double predicted = pb_bearing(x - t->x, z - t->z);
    const double err = pb_wrap_deg(predicted - t->bearing);
    const double sigma = pb_clamp(t->sigma, 0.004, 2.0);
    const double q = err / sigma;

    /* Gaussian core + broad outlier floor: one bad frame cannot erase a run. */
    const double gaussian = exp(-0.5 * q * q) / (sigma * 2.5066282746310002);
    const double broad = 1.0 / 360.0;
    const double density = 0.992 * gaussian + 0.008 * broad;
    return log(density + 1e-300);
}

static double pb_score(const PbThrow *throws, int n, double x, double z, int *ring) {
    double lp = pb_ring_log_prior(x, z, ring);
    for (int i = 0; i < n; ++i) lp += pb_throw_log_likelihood(&throws[i], x, z);
    return lp;
}

static int pb_candidate_exists(int cx, int cz) {
    for (int i = 0; i < pb_candidate_count; ++i) {
        if (pb_candidates[i].chunk_x == cx && pb_candidates[i].chunk_z == cz) return 1;
    }
    return 0;
}

static void pb_add_candidate(const PbThrow *throws, int n, int cx, int cz) {
    if (pb_candidate_count >= PB_MAX_CANDIDATES || pb_candidate_exists(cx, cz)) return;
    PbCandidate *c = &pb_candidates[pb_candidate_count++];
    c->chunk_x = cx;
    c->chunk_z = cz;
    c->x = cx * 16.0 + 8.0;
    c->z = cz * 16.0 + 8.0;
    c->logp = pb_score(throws, n, c->x, c->z, &c->ring);
    c->probability = 0.0;
}

static void pb_add_neighborhood(const PbThrow *throws, int n, double x, double z, int radius_chunks) {
    const int base_x = (int)floor(x / 16.0);
    const int base_z = (int)floor(z / 16.0);
    for (int dz = -radius_chunks; dz <= radius_chunks; ++dz) {
        for (int dx = -radius_chunks; dx <= radius_chunks; ++dx) {
            pb_add_candidate(throws, n, base_x + dx, base_z + dz);
        }
    }
}

static int pb_ray_circle(const PbThrow *t, double radius, double *out_x, double *out_z) {
    const double a = t->bearing * PB_PI / 180.0;
    const double dx = -sin(a);
    const double dz = cos(a);
    const double b = 2.0 * (t->x * dx + t->z * dz);
    const double c = t->x * t->x + t->z * t->z - radius * radius;
    const double disc = b * b - 4.0 * c;
    if (disc < 0.0) return 0;
    const double root = sqrt(disc);
    const double t0 = (-b - root) * 0.5;
    const double t1 = (-b + root) * 0.5;
    double d = 1e300;
    if (t0 > 0.0) d = t0;
    if (t1 > 0.0 && t1 < d) d = t1;
    if (!isfinite(d) || d == 1e300) return 0;
    *out_x = t->x + dx * d;
    *out_z = t->z + dz * d;
    return 1;
}

static int pb_line_intersection(const PbThrow *a, const PbThrow *b, double *x, double *z) {
    const double aa = a->bearing * PB_PI / 180.0;
    const double ba = b->bearing * PB_PI / 180.0;
    const double adx = -sin(aa), adz = cos(aa);
    const double bdx = -sin(ba), bdz = cos(ba);
    const double det = adx * (-bdz) - (-bdx) * adz;
    if (fabs(det) < 1e-8) return 0;
    const double rx = b->x - a->x;
    const double rz = b->z - a->z;
    const double u = (rx * (-bdz) - (-bdx) * rz) / det;
    if (u < -256.0) return 0;
    *x = a->x + u * adx;
    *z = a->z + u * adz;
    return isfinite(*x) && isfinite(*z);
}

static void pb_generate_candidates(const PbThrow *throws, int n) {
    pb_candidate_count = 0;

    for (int i = 0; i < n; ++i) {
        for (int ring = 0; ring < 8; ++ring) {
            double inner, outer;
            pb_ring_bounds(ring, &inner, &outer);
            const double radii[3] = {inner, 0.5 * (inner + outer), outer};
            for (int k = 0; k < 3; ++k) {
                double x, z;
                if (pb_ray_circle(&throws[i], radii[k], &x, &z)) {
                    pb_add_neighborhood(throws, n, x, z, n == 1 ? 7 : 4);
                }
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            double x, z;
            if (pb_line_intersection(&throws[i], &throws[j], &x, &z)) {
                pb_add_neighborhood(throws, n, x, z, 12);
            }
        }
    }

    if (pb_candidate_count == 0) {
        const PbThrow *t = &throws[0];
        const double a = t->bearing * PB_PI / 180.0;
        const double dx = -sin(a), dz = cos(a);
        for (double d = 512.0; d <= 32768.0; d *= 1.45) {
            pb_add_neighborhood(throws, n, t->x + dx * d, t->z + dz * d, 2);
        }
    }
}

static int pb_cmp_candidate(const void *lhs, const void *rhs) {
    const PbCandidate *a = (const PbCandidate *)lhs;
    const PbCandidate *b = (const PbCandidate *)rhs;
    if (a->logp < b->logp) return 1;
    if (a->logp > b->logp) return -1;
    if (a->chunk_x != b->chunk_x) return a->chunk_x < b->chunk_x ? -1 : 1;
    return a->chunk_z < b->chunk_z ? -1 : (a->chunk_z > b->chunk_z);
}

static void pb_normalize(void) {
    if (pb_candidate_count == 0) return;
    qsort(pb_candidates, (size_t)pb_candidate_count, sizeof(PbCandidate), pb_cmp_candidate);
    const double max_lp = pb_candidates[0].logp;
    double total = 0.0;
    for (int i = 0; i < pb_candidate_count; ++i) {
        double w = exp(pb_candidates[i].logp - max_lp);
        if (!isfinite(w)) w = 0.0;
        pb_candidates[i].probability = w;
        total += w;
    }
    if (total <= 0.0) total = 1.0;
    for (int i = 0; i < pb_candidate_count; ++i) pb_candidates[i].probability /= total;
}

static void pb_print_solve(const PbThrow *throws, int n) {
    pb_generate_candidates(throws, n);
    pb_normalize();

    if (pb_candidate_count == 0) {
        printf("{\"ok\":false,\"error\":\"no candidates\"}\n");
        return;
    }

    const PbCandidate *best = &pb_candidates[0];
    const PbThrow *last = &throws[n - 1];
    const double distance = pb_distance(last->x, last->z, best->x, best->z);
    const double direction = pb_bearing(best->x - last->x, best->z - last->z);

    double mean_x = 0.0, mean_z = 0.0, mass = 0.0;
    const int cap = pb_candidate_count < 512 ? pb_candidate_count : 512;
    for (int i = 0; i < cap; ++i) {
        const double w = pb_candidates[i].probability;
        mean_x += w * pb_candidates[i].x;
        mean_z += w * pb_candidates[i].z;
        mass += w;
    }
    if (mass > 0.0) {
        mean_x /= mass;
        mean_z /= mass;
    } else {
        mean_x = best->x;
        mean_z = best->z;
    }

    double var = 0.0;
    for (int i = 0; i < cap; ++i) {
        const double dx = pb_candidates[i].x - mean_x;
        const double dz = pb_candidates[i].z - mean_z;
        var += pb_candidates[i].probability * (dx * dx + dz * dz);
    }
    const double uncertainty = mass > 0.0 ? sqrt(var / mass) : 0.0;

    const double dir_rad = direction * PB_PI / 180.0;
    const double baseline = best->probability < 0.50 ? 48.0 : 24.0;
    const double next_x = last->x + cos(dir_rad) * baseline;
    const double next_z = last->z + sin(dir_rad) * baseline;

    printf("{\"ok\":true,\"mode\":\"solve\",\"throws\":%d,", n);
    printf("\"x\":%.3f,\"z\":%.3f,\"chunkX\":%d,\"chunkZ\":%d,\"ring\":%d,",
           best->x, best->z, best->chunk_x, best->chunk_z, best->ring);
    printf("\"confidence\":%.9f,\"distance\":%.3f,\"direction\":%.6f,",
           best->probability, distance, direction);
    printf("\"uncertainty\":%.3f,\"posteriorMeanX\":%.3f,\"posteriorMeanZ\":%.3f,",
           uncertainty, mean_x, mean_z);
    printf("\"nextThrowX\":%.3f,\"nextThrowZ\":%.3f,\"candidates\":[", next_x, next_z);
    const int top = pb_candidate_count < PB_TOP ? pb_candidate_count : PB_TOP;
    for (int i = 0; i < top; ++i) {
        if (i) putchar(',');
        printf("{\"x\":%.3f,\"z\":%.3f,\"p\":%.9f,\"ring\":%d}",
               pb_candidates[i].x, pb_candidates[i].z,
               pb_candidates[i].probability, pb_candidates[i].ring);
    }
    printf("]}\n");
}

static void pb_blind(double nether_x, double nether_z) {
    const double x = nether_x * 8.0;
    const double z = nether_z * 8.0;
    const double r = hypot(x, z);
    int ring = 0;
    double best_d = 1e300;
    double target_r = 0.0;

    for (int i = 0; i < 8; ++i) {
        double inner, outer;
        pb_ring_bounds(i, &inner, &outer);
        const double center = 0.5 * (inner + outer);
        const double d = fabs(center - r);
        if (d < best_d) {
            best_d = d;
            ring = i;
            target_r = center;
        }
    }

    double ux = 0.0, uz = 1.0;
    if (r > 1e-9) { ux = x / r; uz = z / r; }
    const double tx = ux * target_r;
    const double tz = uz * target_r;
    const double distance = pb_distance(x, z, tx, tz);
    const double direction = pb_bearing(tx - x, tz - z);
    const double highroll = exp(-0.5 * (best_d / 400.0) * (best_d / 400.0));

    printf("{\"ok\":true,\"mode\":\"blind\",\"netherX\":%.3f,\"netherZ\":%.3f,",
           nether_x, nether_z);
    printf("\"overworldX\":%.3f,\"overworldZ\":%.3f,\"ring\":%d,", x, z, ring);
    printf("\"optimalX\":%.3f,\"optimalZ\":%.3f,\"distance\":%.3f,",
           tx / 8.0, tz / 8.0, distance / 8.0);
    printf("\"direction\":%.6f,\"highrollApprox\":%.9f}\n", direction, highroll);
}

static int pb_cmp_double(const void *a, const void *b) {
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double pb_median(double *a, int n) {
    qsort(a, (size_t)n, sizeof(double), pb_cmp_double);
    if (n & 1) return a[n / 2];
    return 0.5 * (a[n / 2 - 1] + a[n / 2]);
}

static void pb_calibrate(int argc) {
    if (argc < 2) {
        printf("{\"ok\":false,\"error\":\"calibrate needs angular errors\"}\n");
        return;
    }
    double values[256];
    int n = 0;
    for (int i = 1; i < argc && n < 256; ++i) {
        double v;
        if (pb_parse_double(pp_arg(i), &v)) values[n++] = v;
    }
    if (n == 0) {
        printf("{\"ok\":false,\"error\":\"no valid angular errors\"}\n");
        return;
    }
    double copy[256];
    memcpy(copy, values, sizeof(double) * (size_t)n);
    const double med = pb_median(copy, n);
    for (int i = 0; i < n; ++i) copy[i] = fabs(values[i] - med);
    double sigma = 1.4826 * pb_median(copy, n);
    sigma = pb_clamp(sigma, 0.004, 2.0);
    printf("{\"ok\":true,\"mode\":\"calibrate\",\"samples\":%d,\"bias\":%.9f,\"sigma\":%.9f}\n",
           n, med, sigma);
}

static void pb_divine(int argc) {
    if (argc < 4) {
        printf("{\"ok\":false,\"error\":\"usage: divine x z bearing spread\"}\n");
        return;
    }
    double x, z, bearing, spread = 22.5;
    if (!pb_parse_double(pp_arg(1), &x) || !pb_parse_double(pp_arg(2), &z) ||
        !pb_parse_double(pp_arg(3), &bearing) ||
        (argc >= 5 && !pb_parse_double(pp_arg(4), &spread))) {
        printf("{\"ok\":false,\"error\":\"invalid divine values\"}\n");
        return;
    }
    spread = pb_clamp(fabs(spread), 0.1, 180.0);
    printf("{\"ok\":true,\"mode\":\"divine\",\"x\":%.3f,\"z\":%.3f,"
           "\"bearing\":%.6f,\"spread\":%.6f,"
           "\"note\":\"sector prior ready for combination with eye evidence\"}\n",
           x, z, pb_wrap_deg(bearing), spread);
}

static void pb_help(void) {
    puts("PunBrain core");
    puts("  solve x z bearing sigma [x z bearing sigma ...]");
    puts("  blind netherX netherZ");
    puts("  divine x z bearing [spread]");
    puts("  calibrate angularError [angularError ...]");
    puts("  version");
}

int64_t punbrain_cli(void) {
    const int argc = (int)pp_arg_count();
    if (argc == 0) {
        pb_help();
        return 0;
    }

    const char *mode = pp_arg(0);

    if (strcmp(mode, "version") == 0) {
        puts("{\"ok\":true,\"name\":\"PunBrain\",\"version\":\"0.1.0\",\"protocol\":1}");
        return 0;
    }

    if (strcmp(mode, "solve") == 0) {
        if (argc < 5 || ((argc - 1) % 4) != 0) {
            puts("{\"ok\":false,\"error\":\"usage: solve x z bearing sigma [x z bearing sigma ...]\"}");
            return 2;
        }

        const int n = (argc - 1) / 4;
        if (n > PB_MAX_THROWS) {
            puts("{\"ok\":false,\"error\":\"too many throws\"}");
            return 2;
        }

        PbThrow throws[PB_MAX_THROWS];
        for (int i = 0; i < n; ++i) {
            const int base = 1 + i * 4;
            if (!pb_parse_double(pp_arg(base), &throws[i].x) ||
                !pb_parse_double(pp_arg(base + 1), &throws[i].z) ||
                !pb_parse_double(pp_arg(base + 2), &throws[i].bearing) ||
                !pb_parse_double(pp_arg(base + 3), &throws[i].sigma)) {
                puts("{\"ok\":false,\"error\":\"invalid numeric throw\"}");
                return 2;
            }
            throws[i].bearing = pb_wrap_deg(throws[i].bearing);
            throws[i].sigma = pb_clamp(fabs(throws[i].sigma), 0.004, 2.0);
        }

        pb_print_solve(throws, n);
        return 0;
    }

    if (strcmp(mode, "blind") == 0) {
        double x, z;
        if (argc != 3 || !pb_parse_double(pp_arg(1), &x) || !pb_parse_double(pp_arg(2), &z)) {
            puts("{\"ok\":false,\"error\":\"usage: blind netherX netherZ\"}");
            return 2;
        }
        pb_blind(x, z);
        return 0;
    }

    if (strcmp(mode, "calibrate") == 0) {
        pb_calibrate(argc);
        return 0;
    }

    if (strcmp(mode, "divine") == 0) {
        pb_divine(argc);
        return 0;
    }

    pb_help();
    return 2;
}
""");

extern native fn punbrain_cli() -> i64;

launch {
    let status = punbrain_cli();
}
