// PunBrain native probability core.
// SPDX-License-Identifier: GPL-3.0-only
//
// This module ports the stronghold ring/prior/posterior model into PunPun.
// Host-specific clipboard/window plumbing belongs elsewhere.

public struct Eye {
    x: f64,
    z: f64,
    raw_angle: f64,
    angle: f64,
    sigma: f64,
    correction: f64,
    kind: i64
}

public struct Candidate {
    cx: i64,
    cz: i64,
    ring: i64,
    weight: f64
}

public struct Solution {
    ok: bool,
    x: f64,
    z: f64,
    chunk_x: i64,
    chunk_z: i64,
    ring: i64,
    certainty: f64,
    distance: f64,
    direction: f64,
    candidate_count: i64
}

public struct Calibration {
    samples: i64,
    bias: f64,
    sigma: f64
}

public struct BlindResult {
    ring: i64,
    optimal_x: f64,
    optimal_z: f64,
    direction: f64,
    distance: f64,
    highroll_approx: f64
}

fn pb_pi() -> f64 { return 3.1415926535897932384626433832795; }
fn pb_num_rings() -> i64 { return 8; }
fn pb_num_strongholds() -> i64 { return 128; }
fn pb_snap_radius() -> i64 { return 7; }

fn pb_minf(a: f64, b: f64) -> f64 {
    if a < b { return a; }
    return b;
}

fn pb_maxf(a: f64, b: f64) -> f64 {
    if a > b { return a; }
    return b;
}

fn pb_clampf(value: f64, lo: f64, hi: f64) -> f64 {
    if value < lo { return lo; }
    if value > hi { return hi; }
    return value;
}

public fn pb_wrap_angle(angle: f64) -> f64 {
    let mut a = fmod(angle, 360.0);
    if a < -180.0 { a += 360.0; }
    if a > 180.0 { a -= 360.0; }
    return a;
}

public fn pb_bearing(dx: f64, dz: f64) -> f64 {
    return -atan2(dx, dz) * 180.0 / pb_pi();
}

public fn pb_correct_angle(alpha: f64, crosshair: f64) -> f64 {
    let mut a = alpha + crosshair;
    a -= 0.000824 * sin((a + 45.0) * pb_pi() / 180.0);
    return pb_wrap_angle(a);
}

public fn pb_normal_eye(x: f64, z: f64, raw_angle: f64, sigma: f64, crosshair: f64) -> Eye {
    return Eye(x, z, pb_wrap_angle(raw_angle), pb_correct_angle(raw_angle, crosshair), pb_clampf(fabs(sigma), 0.001, 2.0), 0.0, 0);
}

// Ninjabrain-style boat quantization before the usual horizontal correction.
public fn pb_boat_eye(x: f64, z: f64, raw_angle: f64, sigma: f64, sensitivity: f64, crosshair: f64, boat_angle: f64) -> Eye {
    let mut pre = sensitivity * 0.6 + 0.2;
    pre = pre * pre * pre * 8.0;
    let minimum_increment = pre * 0.15;
    let snapped = boat_angle + round((raw_angle - boat_angle) / minimum_increment) * minimum_increment;
    return Eye(x, z, pb_wrap_angle(raw_angle), pb_correct_angle(snapped, crosshair), pb_clampf(fabs(sigma), 0.0001, 2.0), 0.0, 2);
}

public fn pb_adjust_eye(eye: Eye, correction: f64, crosshair: f64) -> Eye {
    let total = eye.correction + correction;
    return Eye(eye.x, eye.z, eye.raw_angle, pb_wrap_angle(pb_correct_angle(eye.raw_angle, crosshair) + total), eye.sigma, total, eye.kind);
}

public fn pb_ring_count(ring: i64) -> i64 {
    let mut in_ring: i64 = 1;
    let mut total: i64 = 0;
    let mut r: i64 = 0;
    while r <= ring and r < pb_num_rings() {
        in_ring += 2 * in_ring / (r + 1);
        let remaining = pb_num_strongholds() - total;
        if in_ring > remaining { in_ring = remaining; }
        total += in_ring;
        if r == ring { return in_ring; }
        r += 1;
    }
    return 0;
}

public fn pb_ring_inner(ring: i64) -> f64 {
    return 32.0 * ((4.0 + decimal(ring) * 6.0) - 1.25);
}

public fn pb_ring_outer(ring: i64) -> f64 {
    return 32.0 * ((4.0 + decimal(ring) * 6.0) + 1.25);
}

fn pb_ring_inner_post(ring: i64) -> f64 {
    return pb_ring_inner(ring) - (decimal(pb_snap_radius()) + 1.0) * sqrt(2.0);
}

fn pb_ring_outer_post(ring: i64) -> f64 {
    return pb_ring_outer(ring) + (decimal(pb_snap_radius()) + 1.0) * sqrt(2.0);
}

fn pb_max_chunk() -> i64 { return whole(ceil(pb_ring_outer_post(7))); }

public fn pb_ring_for_radius(radius_chunks: f64) -> i64 {
    let mut r: i64 = 0;
    while r < pb_num_rings() {
        if radius_chunks >= pb_ring_inner_post(r) and radius_chunks <= pb_ring_outer_post(r) { return r; }
        r += 1;
    }
    let mut best = 1000000000.0;
    let mut best_ring: i64 = 0;
    r = 0;
    while r < pb_num_rings() {
        let middle = 0.5 * (pb_ring_inner(r) + pb_ring_outer(r));
        let d = fabs(radius_chunks - middle);
        if d < best { best = d; best_ring = r; }
        r += 1;
    }
    return best_ring;
}

fn pb_java_floor_div4(value: i64) -> i64 {
    if value >= 0 { return value / 4; }
    return -((-value + 3) / 4);
}

// ApproximatedDensity: vanilla ring density convolved with the stronghold
// biome-snapping offset distribution.
fn pb_build_density() -> List<f64> {
    let density = list<f64>();
    let pre = list<f64>();
    let max_len: i64 = 1540;
    let mut i: i64 = 0;
    while i < max_len {
        list_push(density, 0.0);
        list_push(pre, 0.0);
        i += 1;
    }

    let mut ring: i64 = 0;
    while ring < pb_num_rings() {
        let c0 = whole(floor(pb_ring_inner(ring)));
        let c1 = whole(floor(pb_ring_outer(ring)));
        i = c0;
        while i <= c1 and i < max_len {
            if i > 0 {
                let denominator = 2.0 * pb_pi() * (pb_ring_outer(ring) - pb_ring_inner(ring)) * decimal(i);
                let mut rho = decimal(pb_ring_count(ring)) / denominator;
                if i == c0 or i == c1 { rho *= 0.5; }
                list_put(pre, i, rho);
            }
            i += 1;
        }
        ring += 1;
    }

    let offsets = list<i64>();
    i = 0;
    while i < 15 { list_push(offsets, 0); i += 1; }
    i = -26;
    while i <= 30 {
        let chunk_offset = pb_java_floor_div4(i);
        let key = -chunk_offset;
        if key >= -7 and key <= 7 {
            let index = key + 7;
            list_put(offsets, index, list_at(offsets, index) + 1);
        }
        i += 1;
    }

    let filter = list<f64>();
    i = 0;
    while i < 12 { list_push(filter, 0.0); i += 1; }
    let mut filter_sum = 0.0;
    let mut k: i64 = -7;
    while k <= 7 {
        let xw = list_at(offsets, k + 7);
        let mut l: i64 = -7;
        while l <= 7 {
            let zw = list_at(offsets, l + 7);
            let w = xw * zw;
            let mut sample: i64 = 0;
            while sample < 200 {
                let phi = 2.0 * pb_pi() * decimal(sample) / 200.0;
                let radius = sqrt(decimal(k * k + l * l));
                let dr = whole(fabs(round(radius * sin(phi))));
                if dr < 12 {
                    list_put(filter, dr, list_at(filter, dr) + decimal(w));
                }
                if dr == 0 { filter_sum += decimal(w); }
                else { filter_sum += 2.0 * decimal(w); }
                sample += 1;
            }
            l += 1;
        }
        k += 1;
    }
    if filter_sum <= 0.0 { filter_sum = 1.0; }
    i = 0;
    while i < 12 {
        list_put(filter, i, list_at(filter, i) / filter_sum);
        i += 1;
    }

    let density_len = pb_max_chunk() + 5;
    i = 0;
    while i < density_len {
        let mut value = 0.0;
        let mut j: i64 = -11;
        while j <= 11 {
            let source = i + j;
            if source >= 0 and source < max_len {
                let fj = abs(j);
                if fj < 12 { value += list_at(pre, source) * list_at(filter, fj); }
            }
            j += 1;
        }
        list_put(density, i, value);
        i += 1;
    }
    return density;
}

fn pb_density_at(density: List<f64>, cx: f64, cz: f64) -> f64 {
    let radius = hypot(cx, cz);
    let i0 = whole(floor(radius));
    let i1 = i0 + 1;
    let n = list_size(density);
    if i0 < 0 or i0 >= n { return 0.0; }
    if i1 >= n { return list_at(density, i0); }
    let t = radius - decimal(i0);
    return (1.0 - t) * list_at(density, i0) + t * list_at(density, i1);
}

fn pb_orth_component(ax: f64, az: f64, ux: f64, uz: f64) -> f64 {
    let parallel = ux * ax + uz * az;
    let ox = ux * parallel - ax;
    let oz = uz * parallel - az;
    return uz * ox - ux * oz;
}

fn pb_project_major(ax: f64, az: f64, ux: f64, uz: f64, major_x: bool) -> f64 {
    let projection = ax * ux + az * uz;
    if major_x { return ux * projection; }
    return uz * projection;
}

fn pb_circle_intersection_major(ox: f64, oz: f64, ux: f64, uz: f64, radius: f64, major_x: bool) -> f64 {
    let dot = ox * ux + oz * uz;
    let discriminant = dot * dot + radius * radius - ox * ox - oz * oz;
    if discriminant < 0.0 { return 0.0; }
    let b = -dot - sqrt(discriminant);
    if major_x { return ox + b * ux; }
    return oz + b * uz;
}

fn pb_choose_xor(flag: bool, comparison: bool, first: f64, second: f64) -> f64 {
    if flag != comparison { return first; }
    return second;
}

fn pb_iter_start_major(om: f64, on: f64, ux: f64, uz: f64, vx: f64, vz: f64, major_x: bool, major_positive: bool) -> f64 {
    let maxc = decimal(pb_max_chunk());
    if om * om + on * on <= maxc * maxc { return om; }

    let mut ox = om;
    let mut oz = on;
    if !major_x { ox = on; oz = om; }
    let uorth = pb_orth_component(-ox, -oz, ux, uz);
    let vorth = pb_orth_component(-ox, -oz, vx, vz);

    if uorth > 0.0 and vorth < 0.0 {
        let magnitude = hypot(ox, oz);
        if magnitude < 0.000000000001 { return om; }
        let ix = ox / magnitude * maxc;
        let iz = oz / magnitude * maxc;
        let m1 = om + pb_project_major(ix - ox, iz - oz, ux, uz, major_x);
        let m2 = om + pb_project_major(ix - ox, iz - oz, vx, vz, major_x);
        return pb_choose_xor(major_positive, m1 > m2, m1, m2);
    }

    let iu = pb_circle_intersection_major(ox, oz, ux, uz, maxc, major_x);
    let iv = pb_circle_intersection_major(ox, oz, vx, vz, maxc, major_x);
    if iu != 0.0 or iv != 0.0 {
        if iu != 0.0 and iv != 0.0 { return pb_choose_xor(major_positive, iu > iv, iu, iv); }
        if iu != 0.0 { return iu; }
        return iv;
    }
    return om;
}

fn pb_candidate_weight(density: List<f64>, cx: i64, cz: i64) -> f64 {
    let mut weight = 0.0;
    let mut k: i64 = 0;
    while k < 2 {
        let sx = decimal(cx) - 0.5 + decimal(k);
        let mut l: i64 = 0;
        while l < 2 {
            let sz = decimal(cz) - 0.5 + decimal(l);
            weight += pb_density_at(density, sx, sz);
            l += 1;
        }
        k += 1;
    }
    return weight * 0.25;
}

fn pb_build_prior(first: Eye, target_coord: i64, density: List<f64>) -> List<Candidate> {
    let candidates = list<Candidate>();
    let tolerance = pb_minf(1.0, 30.0 * first.sigma) * pb_pi() / 180.0;
    let phi = first.angle * pb_pi() / 180.0;
    let dx = -sin(phi);
    let dz = cos(phi);
    let ux = -sin(phi - tolerance);
    let uz = cos(phi - tolerance);
    let vx = -sin(phi + tolerance);
    let vz = cos(phi + tolerance);
    let major_x = cos(phi) * cos(phi) < 0.5;
    let mut major_positive = cos(phi) > 0.0;
    if major_x { major_positive = -sin(phi) > 0.0; }

    let mut om = (first.z - decimal(target_coord)) / 16.0;
    let mut on = (first.x - decimal(target_coord)) / 16.0;
    if major_x {
        om = (first.x - decimal(target_coord)) / 16.0;
        on = (first.z - decimal(target_coord)) / 16.0;
    }
    let start = pb_iter_start_major(om, on, ux, uz, vx, vz, major_x, major_positive);
    let mut uk = ux / uz;
    let mut vk = vx / vz;
    if major_x { uk = uz / ux; vk = vz / vx; }
    let mut right_positive = uk - vk > 0.0;
    if major_positive { right_positive = vk - uk > 0.0; }
    let mut major: i64 = whole(floor(start));
    if major_positive { major = whole(ceil(start)); }
    let maxc = pb_max_chunk();
    let range = 5000.0 / 16.0;
    let mut outer_guard: i64 = 0;

    while outer_guard < 10000 and list_size(candidates) < 180000 {
        let major_delta = decimal(major) - start;
        let mut along = major_delta / dz;
        if major_x { along = major_delta / dx; }
        if along >= range { return candidates; }

        if major >= -maxc - 2 and major <= maxc + 2 {
            let minor_u = on + uk * (decimal(major) - om);
            let minor_v = on + vk * (decimal(major) - om);
            let mut minor: i64 = whole(floor(minor_u));
            if right_positive { minor = whole(ceil(minor_u)); }
            if minor < -maxc { minor = -maxc; }
            if minor > maxc { minor = maxc; }
            let mut inner_guard: i64 = 0;
            while inner_guard < 2 * maxc + 4 and minor >= -maxc and minor <= maxc {
                let inside = (right_positive and decimal(minor) < minor_v) or (!right_positive and decimal(minor) > minor_v);
                if !inside { inner_guard = 2 * maxc + 4; }
                else {
                    let mut cx = minor;
                    let mut cz = major;
                    if major_x { cx = major; cz = minor; }
                    let prior = pb_candidate_weight(density, cx, cz);
                    if prior > 0.0 {
                        list_push(candidates, Candidate(cx, cz, pb_ring_for_radius(hypot(decimal(cx), decimal(cz))), prior));
                    }
                    if right_positive { minor += 1; }
                    else { minor -= 1; }
                    inner_guard += 1;
                }
            }
        }
        if major_positive { major += 1; }
        else { major -= 1; }
        outer_guard += 1;
    }
    return candidates;
}

fn pb_position_variance(eye: Eye, tx: f64, tz: f64) -> f64 {
    let distance = pb_maxf(1.0, hypot(tx - eye.x, tz - eye.z));
    let fx = eye.x - floor(eye.x);
    let fz = eye.z - floor(eye.z);
    let precise_x = fabs(fx - 0.3) < 0.000001 or fabs(fx - 0.7) < 0.000001;
    let precise_z = fabs(fz - 0.3) < 0.000001 or fabs(fz - 0.7) < 0.000001;
    if precise_x and precise_z { return 0.0; }
    let max_lateral = 0.005 * sqrt(2.0) * 180.0 / pb_pi();
    let angular = max_lateral / distance;
    return angular * angular / 6.0;
}

fn pb_likelihood(eye: Eye, cx: i64, cz: i64, target_coord: i64) -> f64 {
    let tx = decimal(cx * 16 + target_coord);
    let tz = decimal(cz * 16 + target_coord);
    let gamma = pb_bearing(tx - eye.x, tz - eye.z);
    let mut delta = fabs(pb_wrap_angle(gamma - eye.angle));
    if delta > 180.0 { delta = 360.0 - delta; }
    let variance = eye.sigma * eye.sigma + pb_position_variance(eye, tx, tz);
    return exp(-(delta * delta) / (2.0 * pb_maxf(variance, 0.000000000001)));
}

fn pb_condition(candidates: List<Candidate>, eye: Eye, target_coord: i64) {
    let mut total = 0.0;
    let mut i: i64 = 0;
    let n = list_size(candidates);
    while i < n {
        let c = list_at(candidates, i);
        let weight = c.weight * pb_likelihood(eye, c.cx, c.cz, target_coord);
        list_put(candidates, i, Candidate(c.cx, c.cz, c.ring, weight));
        total += weight;
        i += 1;
    }
    if total <= 0.0 { return; }
    i = 0;
    while i < n {
        let c = list_at(candidates, i);
        list_put(candidates, i, Candidate(c.cx, c.cz, c.ring, c.weight / total));
        i += 1;
    }
}

fn pb_best(candidates: List<Candidate>) -> Candidate {
    let n = list_size(candidates);
    if n == 0 { return Candidate(0, 0, 0, 0.0); }
    let mut best = list_at(candidates, 0);
    let mut i: i64 = 1;
    while i < n {
        let c = list_at(candidates, i);
        if c.weight > best.weight { best = c; }
        i += 1;
    }
    return best;
}

public fn pb_solve(eyes: List<Eye>, target_coord: i64) -> Solution {
    if list_size(eyes) == 0 { return Solution(false, 0.0, 0.0, 0, 0, 0, 0.0, 0.0, 0.0, 0); }
    let density = pb_build_density();
    let first = list_at(eyes, 0);
    let candidates = pb_build_prior(first, target_coord, density);
    if list_size(candidates) == 0 { return Solution(false, 0.0, 0.0, 0, 0, 0, 0.0, 0.0, 0.0, 0); }

    let mut e: i64 = 0;
    while e < list_size(eyes) {
        pb_condition(candidates, list_at(eyes, e), target_coord);
        e += 1;
    }

    let best = pb_best(candidates);
    let tx = decimal(best.cx * 16 + target_coord);
    let tz = decimal(best.cz * 16 + target_coord);
    let last = list_at(eyes, list_size(eyes) - 1);
    let distance = hypot(tx - last.x, tz - last.z);
    let direction = pb_bearing(tx - last.x, tz - last.z);
    return Solution(true, tx, tz, best.cx, best.cz, best.ring, best.weight, distance, direction, list_size(candidates));
}

public fn pb_calibrate(errors: List<f64>) -> Calibration {
    let n = list_size(errors);
    if n == 0 { return Calibration(0, 0.0, 0.03); }
    let mut sum = 0.0;
    let mut i: i64 = 0;
    while i < n { sum += list_at(errors, i); i += 1; }
    let mean = sum / decimal(n);
    let mut variance = 0.0;
    i = 0;
    while i < n {
        let d = list_at(errors, i) - mean;
        variance += d * d;
        i += 1;
    }
    variance /= decimal(n);
    return Calibration(n, mean, pb_clampf(sqrt(variance), 0.001, 2.0));
}

public fn pb_blind(nether_x: f64, nether_z: f64) -> BlindResult {
    let overworld_x = nether_x * 8.0;
    let overworld_z = nether_z * 8.0;
    let radius_chunks = hypot(overworld_x, overworld_z) / 16.0;
    let ring = pb_ring_for_radius(radius_chunks);
    let middle = 0.5 * (pb_ring_inner(ring) + pb_ring_outer(ring)) * 16.0;
    let radius = hypot(overworld_x, overworld_z);
    let mut ux = 0.0;
    let mut uz = 1.0;
    if radius > 0.000000001 { ux = overworld_x / radius; uz = overworld_z / radius; }
    let target_x = ux * middle;
    let target_z = uz * middle;
    let distance = hypot(target_x - overworld_x, target_z - overworld_z) / 8.0;
    let direction = pb_bearing(target_x - overworld_x, target_z - overworld_z);
    let radial_error = fabs(radius_chunks - 0.5 * (pb_ring_inner(ring) + pb_ring_outer(ring))) * 16.0;
    let highroll = exp(-0.5 * pow(radial_error / 400.0, 2.0));
    return BlindResult(ring, target_x / 8.0, target_z / 8.0, direction, distance, highroll);
}

public fn pb_core_selftest() -> bool {
    if pb_ring_count(0) != 3 { return false; }
    if pb_ring_count(1) != 6 { return false; }
    if pb_ring_count(2) != 10 { return false; }
    if pb_ring_count(3) != 15 { return false; }
    if pb_ring_count(4) != 21 { return false; }
    if pb_ring_count(5) != 28 { return false; }
    if pb_ring_count(6) != 36 { return false; }
    if pb_ring_count(7) != 9 { return false; }
    let expected = -0.000824 * sin(45.0 * pb_pi() / 180.0);
    if fabs(pb_correct_angle(0.0, 0.0) - expected) > 0.0000000001 { return false; }

    let eyes = list<Eye>();
    list_push(eyes, pb_normal_eye(0.0, 0.0, -45.0, 0.03, 0.0));
    list_push(eyes, pb_normal_eye(100.0, 0.0, -48.0, 0.03, 0.0));
    let solution = pb_solve(eyes, 8);
    return solution.ok and solution.candidate_count > 0;
}
