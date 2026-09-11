package dev.punpun.punbrain;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.keybinding.v1.KeyBindingHelper;
import net.fabricmc.fabric.api.client.rendering.v1.HudRenderCallback;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.option.KeyBinding;
import net.minecraft.client.util.InputUtil;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EyeOfEnderEntity;
import net.minecraft.text.Text;
import net.minecraft.util.math.Vec3d;
import org.lwjgl.glfw.GLFW;

import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.URI;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executors;

/**
 * PunBrain's Minecraft-side collector and HUD.
 *
 * Precision is taken from the Eye of Ender's native double-precision trajectory,
 * not OCR, F3 rounding, or player crosshair yaw. The trajectory is robustly fit
 * over several game ticks and its residuals become the solver's uncertainty.
 */
public final class PunbrainClient implements ClientModInitializer {
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
    private static final Map<UUID, Track> TRACKS = new HashMap<>();
    private static final Set<UUID> FINISHED = new HashSet<>();
    private static final List<EyeMeasurement> EYES = new ArrayList<>();

    private static final Path CONFIG_DIR =
            FabricLoader.getInstance().getConfigDir().resolve("punbrain");
    private static final Path CONFIG_FILE = CONFIG_DIR.resolve("config.json");

    private static Settings settings;
    private static volatile Result latest;
    private static volatile String lastError = "";
    private static volatile String pendingClipboard;
    private static volatile boolean hudEnabled = true;
    private static volatile boolean locked = false;
    private static HttpServer server;

    private static KeyBinding resetKey;
    private static KeyBinding toggleKey;
    private static KeyBinding copyKey;
    private static KeyBinding lockKey;

    @Override
    public void onInitializeClient() {
        settings = Settings.load();
        hudEnabled = settings.hudEnabled;

        resetKey = KeyBindingHelper.registerKeyBinding(new KeyBinding(
                "key.punbrain.reset", InputUtil.Type.KEYSYM, GLFW.GLFW_KEY_P,
                "category.punbrain"));
        toggleKey = KeyBindingHelper.registerKeyBinding(new KeyBinding(
                "key.punbrain.toggle", InputUtil.Type.KEYSYM, GLFW.GLFW_KEY_O,
                "category.punbrain"));
        copyKey = KeyBindingHelper.registerKeyBinding(new KeyBinding(
                "key.punbrain.copy", InputUtil.Type.KEYSYM, GLFW.GLFW_KEY_K,
                "category.punbrain"));
        lockKey = KeyBindingHelper.registerKeyBinding(new KeyBinding(
                "key.punbrain.lock", InputUtil.Type.KEYSYM, GLFW.GLFW_KEY_L,
                "category.punbrain"));

        ClientTickEvents.END_CLIENT_TICK.register(PunbrainClient::tick);
        HudRenderCallback.EVENT.register(PunbrainClient::renderHud);
        startApi();
        verifyCore();
    }

    private static void tick(MinecraftClient client) {
        if (client.player == null || client.world == null) {
            TRACKS.clear();
            FINISHED.clear();
            return;
        }

        while (resetKey.wasPressed()) reset();
        while (toggleKey.wasPressed()) hudEnabled = !hudEnabled;
        while (lockKey.wasPressed()) locked = !locked;
        while (copyKey.wasPressed()) {
            Result r = latest;
            if (r != null) pendingClipboard = formatCoords(r.x, r.z);
        }

        if (pendingClipboard != null) {
            client.keyboard.setClipboard(pendingClipboard);
            pendingClipboard = null;
        }

        if (locked) return;

        for (Track track : TRACKS.values()) track.unseenTicks++;

        for (Entity entity : client.world.getEntities()) {
            if (!(entity instanceof EyeOfEnderEntity eye)) continue;
            if (FINISHED.contains(eye.getUuid())) continue;
            if (eye.squaredDistanceTo(client.player) > 256.0 * 256.0) continue;

            Track track = TRACKS.computeIfAbsent(
                    eye.getUuid(), id -> new Track(id, client.player.hasVehicle()));
            track.unseenTicks = 0;
            track.add(eye.getPos());

            if (track.samples.size() >= settings.maxTrackSamples) finish(track);
        }

        List<Track> stale = TRACKS.values().stream()
                .filter(t -> t.unseenTicks >= 3)
                .toList();
        for (Track track : stale) finish(track);
    }

    private static void finish(Track track) {
        TRACKS.remove(track.id);
        FINISHED.add(track.id);
        if (track.samples.size() < settings.minTrackSamples) return;

        Fit fit = RobustFit.fit(track.samples);
        if (fit == null || fit.length < 1.0) return;

        Vec3d anchor = track.samples.get(Math.min(1, track.samples.size() - 1));
        double modelSigma = track.inVehicle ? settings.boatModelSigma : settings.baseModelSigma;
        double sigma = Math.sqrt(fit.sigmaDeg * fit.sigmaDeg + modelSigma * modelSigma);
        sigma = clamp(sigma, settings.minSigma, settings.maxSigma);

        EYES.add(new EyeMeasurement(
                anchor.x, anchor.z, fit.bearingDeg, sigma,
                track.samples.size(), fit.rms, fit.length, track.inVehicle));
        if (EYES.size() > settings.maxStoredThrows) EYES.remove(0);
        solveAsync();
    }

    private static void solveAsync() {
        List<EyeMeasurement> snapshot = List.copyOf(EYES);
        CompletableFuture.runAsync(() -> {
            try {
                Result result = Core.solve(settings, snapshot);
                latest = result;
                lastError = "";
                if (settings.autoCopy && result != null
                        && result.confidence >= settings.autoCopyConfidence) {
                    pendingClipboard = formatCoords(result.x, result.z);
                }
            } catch (Exception e) {
                lastError = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
            }
        });
    }

    private static void verifyCore() {
        CompletableFuture.runAsync(() -> {
            try {
                String output = Core.run(settings, List.of("version"));
                JsonObject root = JsonParser.parseString(output).getAsJsonObject();
                if (!root.has("ok") || !root.get("ok").getAsBoolean()) {
                    lastError = "PunBrain core did not pass version probe";
                }
            } catch (Exception e) {
                lastError = "Core unavailable: " + e.getMessage();
            }
        });
    }

    private static void reset() {
        TRACKS.clear();
        FINISHED.clear();
        EYES.clear();
        latest = null;
        lastError = "";
    }

    private static void renderHud(DrawContext draw, Object tickCounter) {
        MinecraftClient client = MinecraftClient.getInstance();
        if (!hudEnabled || client.player == null || client.world == null) return;
        if (settings.hideWithScreens && client.currentScreen != null) return;

        Theme theme = Theme.from(settings.theme);
        int x = settings.hudX;
        int y = settings.hudY;
        int h = latest == null ? 58 : 94;

        draw.fill(x - 6, y - 6, x + 212, y + h, theme.background);
        draw.fill(x - 6, y - 6, x - 3, y + h, theme.accent);

        line(draw, client, "PunBrain", x, y, theme.accent);
        line(draw, client, "Eyes " + EYES.size() + (locked ? "  [LOCKED]" : ""),
                x, y + 13, theme.text);

        EyeMeasurement lastEye = EYES.isEmpty() ? null : EYES.get(EYES.size() - 1);
        if (lastEye != null) {
            line(draw, client,
                    String.format(Locale.ROOT, "Eye %.5f°  σ %.4f°", lastEye.bearing, lastEye.sigma),
                    x, y + 26, theme.muted);
        }

        Result result = latest;
        if (result != null) {
            double dx = result.x - client.player.getX();
            double dz = result.z - client.player.getZ();
            double liveDistance = Math.hypot(dx, dz);
            double liveBearing = -Math.toDegrees(Math.atan2(dx, dz));

            line(draw, client,
                    String.format(Locale.ROOT, "Stronghold %.0f, %.0f", result.x, result.z),
                    x, y + 42, theme.text);
            line(draw, client,
                    String.format(Locale.ROOT, "P %.1f%%  ±%.0fb",
                            result.confidence * 100.0, result.uncertainty),
                    x, y + 55, theme.good);
            line(draw, client,
                    String.format(Locale.ROOT, "%.0fb  face %.2f°", liveDistance, liveBearing),
                    x, y + 68, theme.text);
            line(draw, client,
                    String.format(Locale.ROOT, "Next throw %.0f, %.0f",
                            result.nextThrowX, result.nextThrowZ),
                    x, y + 81, theme.muted);
        } else if (!lastError.isBlank()) {
            line(draw, client, trim(lastError, 34), x, y + 42, theme.bad);
        } else {
            line(draw, client, "Throw an Eye of Ender", x, y + 42, theme.muted);
        }
    }

    private static void line(DrawContext draw, MinecraftClient client,
                             String text, int x, int y, int color) {
        draw.drawTextWithShadow(client.textRenderer, Text.literal(text), x, y, color);
    }

    private static String trim(String s, int max) {
        if (s.length() <= max) return s;
        return s.substring(0, Math.max(0, max - 1)) + "…";
    }

    private static String formatCoords(double x, double z) {
        return String.format(Locale.ROOT, "%.0f %.0f", x, z);
    }

    private static void startApi() {
        try {
            server = HttpServer.create(new InetSocketAddress("127.0.0.1", settings.apiPort), 0);
            server.setExecutor(Executors.newSingleThreadExecutor(r -> {
                Thread t = new Thread(r, "punbrain-api");
                t.setDaemon(true);
                return t;
            }));
            server.createContext("/v1/status", PunbrainClient::apiStatus);
            server.createContext("/v1/reset", PunbrainClient::apiReset);
            server.createContext("/v1/eye", PunbrainClient::apiEye);
            server.createContext("/v1/blind", PunbrainClient::apiBlind);
            server.start();
        } catch (IOException e) {
            lastError = "API: " + e.getMessage();
        }
    }

    private static void apiStatus(HttpExchange ex) throws IOException {
        JsonObject out = new JsonObject();
        out.addProperty("ok", true);
        out.addProperty("throws", EYES.size());
        out.addProperty("locked", locked);
        out.addProperty("hud", hudEnabled);
        out.addProperty("error", lastError);
        Result r = latest;
        if (r != null) out.add("result", GSON.toJsonTree(r));
        respond(ex, 200, GSON.toJson(out));
    }

    private static void apiReset(HttpExchange ex) throws IOException {
        MinecraftClient.getInstance().execute(PunbrainClient::reset);
        respond(ex, 200, "{\"ok\":true}");
    }

    private static void apiEye(HttpExchange ex) throws IOException {
        try {
            Map<String, String> q = query(ex.getRequestURI());
            double x = Double.parseDouble(required(q, "x"));
            double z = Double.parseDouble(required(q, "z"));
            double bearing = Double.parseDouble(required(q, "bearing"));
            double sigma = q.containsKey("sigma")
                    ? Double.parseDouble(q.get("sigma")) : settings.baseModelSigma;

            MinecraftClient.getInstance().execute(() -> {
                EYES.add(new EyeMeasurement(
                        x, z, bearing, clamp(sigma, settings.minSigma, settings.maxSigma),
                        0, 0.0, 0.0, false));
                solveAsync();
            });
            respond(ex, 200, "{\"ok\":true}");
        } catch (Exception e) {
            respond(ex, 400, jsonError(e.getMessage()));
        }
    }

    private static void apiBlind(HttpExchange ex) throws IOException {
        try {
            Map<String, String> q = query(ex.getRequestURI());
            String x = required(q, "x");
            String z = required(q, "z");
            respond(ex, 200, Core.run(settings, List.of("blind", x, z)));
        } catch (Exception e) {
            respond(ex, 400, jsonError(e.getMessage()));
        }
    }

    private static Map<String, String> query(URI uri) {
        Map<String, String> out = new HashMap<>();
        String raw = uri.getRawQuery();
        if (raw == null || raw.isBlank()) return out;
        for (String pair : raw.split("&")) {
            int eq = pair.indexOf('=');
            String k = eq < 0 ? pair : pair.substring(0, eq);
            String v = eq < 0 ? "" : pair.substring(eq + 1);
            out.put(URLDecoder.decode(k, StandardCharsets.UTF_8),
                    URLDecoder.decode(v, StandardCharsets.UTF_8));
        }
        return out;
    }

    private static String required(Map<String, String> q, String key) {
        String value = q.get(key);
        if (value == null || value.isBlank()) throw new IllegalArgumentException("missing " + key);
        return value;
    }

    private static String jsonError(String message) {
        JsonObject out = new JsonObject();
        out.addProperty("ok", false);
        out.addProperty("error", message == null ? "unknown error" : message);
        return GSON.toJson(out);
    }

    private static void respond(HttpExchange ex, int status, String body) throws IOException {
        byte[] data = body.getBytes(StandardCharsets.UTF_8);
        ex.getResponseHeaders().set("Content-Type", "application/json; charset=utf-8");
        ex.getResponseHeaders().set("Cache-Control", "no-store");
        ex.sendResponseHeaders(status, data.length);
        try (OutputStream stream = ex.getResponseBody()) {
            stream.write(data);
        }
    }

    private static double clamp(double value, double min, double max) {
        return Math.max(min, Math.min(max, value));
    }

    private record EyeMeasurement(double x, double z, double bearing, double sigma,
                                  int samples, double fitRms, double trackLength, boolean boat) {}

    private record Fit(double bearingDeg, double sigmaDeg, double rms, double length) {}

    private record Result(boolean ok, String mode, int throws, double x, double z,
                          int chunkX, int chunkZ, int ring, double confidence,
                          double distance, double direction, double uncertainty,
                          double posteriorMeanX, double posteriorMeanZ,
                          double nextThrowX, double nextThrowZ) {}

    private static final class Track {
        final UUID id;
        final boolean inVehicle;
        final List<Vec3d> samples = new ArrayList<>();
        int unseenTicks;

        Track(UUID id, boolean inVehicle) {
            this.id = id;
            this.inVehicle = inVehicle;
        }

        void add(Vec3d point) {
            if (samples.isEmpty()
                    || samples.get(samples.size() - 1).squaredDistanceTo(point) > 0.0004) {
                samples.add(point);
            }
        }
    }

    /** Iteratively reweighted orthogonal regression over the full eye trajectory. */
    private static final class RobustFit {
        static Fit fit(List<Vec3d> source) {
            if (source.size() < 4) return null;

            int start = source.size() >= 7 ? 1 : 0;
            int end = source.size() >= 8 ? source.size() - 1 : source.size();
            List<Vec3d> points = source.subList(start, end);
            if (points.size() < 4) return null;

            double[] weights = new double[points.size()];
            java.util.Arrays.fill(weights, 1.0);
            double dirX = 0.0, dirZ = 1.0, meanX = 0.0, meanZ = 0.0;

            for (int iter = 0; iter < 4; iter++) {
                double sw = 0.0;
                meanX = 0.0;
                meanZ = 0.0;
                for (int i = 0; i < points.size(); i++) {
                    double w = weights[i];
                    sw += w;
                    meanX += w * points.get(i).x;
                    meanZ += w * points.get(i).z;
                }
                if (sw <= 0.0) return null;
                meanX /= sw;
                meanZ /= sw;

                double xx = 0.0, zz = 0.0, xz = 0.0;
                for (int i = 0; i < points.size(); i++) {
                    Vec3d p = points.get(i);
                    double dx = p.x - meanX;
                    double dz = p.z - meanZ;
                    double w = weights[i];
                    xx += w * dx * dx;
                    zz += w * dz * dz;
                    xz += w * dx * dz;
                }

                double theta = 0.5 * Math.atan2(2.0 * xz, xx - zz);
                dirX = Math.cos(theta);
                dirZ = Math.sin(theta);

                Vec3d first = points.get(0);
                Vec3d last = points.get(points.size() - 1);
                if ((last.x - first.x) * dirX + (last.z - first.z) * dirZ < 0.0) {
                    dirX = -dirX;
                    dirZ = -dirZ;
                }

                double[] residuals = new double[points.size()];
                for (int i = 0; i < points.size(); i++) {
                    Vec3d p = points.get(i);
                    double dx = p.x - meanX;
                    double dz = p.z - meanZ;
                    residuals[i] = Math.abs(dx * (-dirZ) + dz * dirX);
                }

                double scale = Math.max(median(residuals) * 1.4826, 0.0005);
                double huber = 1.5 * scale;
                for (int i = 0; i < residuals.length; i++) {
                    double r = residuals[i];
                    weights[i] = r <= huber ? 1.0 : huber / r;
                }
            }

            Vec3d first = points.get(0);
            Vec3d last = points.get(points.size() - 1);
            double length = Math.hypot(last.x - first.x, last.z - first.z);
            if (length < 1e-6) return null;

            double weightedResidual2 = 0.0;
            double sw = 0.0;
            for (int i = 0; i < points.size(); i++) {
                Vec3d p = points.get(i);
                double dx = p.x - meanX;
                double dz = p.z - meanZ;
                double residual = dx * (-dirZ) + dz * dirX;
                weightedResidual2 += weights[i] * residual * residual;
                sw += weights[i];
            }

            double rms = Math.sqrt(weightedResidual2 / Math.max(sw, 1e-9));
            double sigmaDeg = Math.toDegrees(Math.atan2(Math.max(rms, 0.0002), length));
            sigmaDeg /= Math.sqrt(Math.max(1.0, points.size() - 2.0));
            double bearing = -Math.toDegrees(Math.atan2(dirX, dirZ));
            return new Fit(wrap(bearing), sigmaDeg, rms, length);
        }

        private static double median(double[] values) {
            double[] copy = values.clone();
            java.util.Arrays.sort(copy);
            int n = copy.length;
            return (n & 1) == 1 ? copy[n / 2] : 0.5 * (copy[n / 2 - 1] + copy[n / 2]);
        }

        private static double wrap(double angle) {
            while (angle <= -180.0) angle += 360.0;
            while (angle > 180.0) angle -= 360.0;
            return angle;
        }
    }

    private static final class Core {
        static Result solve(Settings settings, List<EyeMeasurement> eyes) throws Exception {
            List<String> args = new ArrayList<>();
            args.add("solve");
            for (EyeMeasurement eye : eyes) {
                args.add(Double.toString(eye.x));
                args.add(Double.toString(eye.z));
                args.add(Double.toString(eye.bearing));
                args.add(Double.toString(eye.sigma));
            }

            String json = run(settings, args);
            JsonObject root = JsonParser.parseString(json).getAsJsonObject();
            if (!root.has("ok") || !root.get("ok").getAsBoolean()) {
                throw new IOException(root.has("error")
                        ? root.get("error").getAsString() : "solver rejected input");
            }
            return GSON.fromJson(root, Result.class);
        }

        static String run(Settings settings, List<String> args) throws Exception {
            List<String> command = new ArrayList<>();
            command.add(settings.coreCommand());
            command.addAll(args);

            Process process = new ProcessBuilder(command).redirectErrorStream(true).start();
            String output = new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8).trim();
            int exit = process.waitFor();
            if (exit != 0 && output.isBlank()) throw new IOException("core exited " + exit);
            if (output.isBlank()) throw new IOException("empty core response");
            int jsonStart = output.lastIndexOf('\n');
            return jsonStart >= 0 ? output.substring(jsonStart + 1).trim() : output;
        }
    }

    private static final class Settings {
        boolean hudEnabled = true;
        boolean hideWithScreens = true;
        boolean autoCopy = true;
        double autoCopyConfidence = 0.20;
        int hudX = 10;
        int hudY = 10;
        int apiPort = 52533;
        String theme = "dark";
        String corePath = "";
        double baseModelSigma = 0.018;
        double boatModelSigma = 0.024;
        double minSigma = 0.006;
        double maxSigma = 0.35;
        int minTrackSamples = 5;
        int maxTrackSamples = 18;
        int maxStoredThrows = 32;

        String coreCommand() {
            if (corePath != null && !corePath.isBlank()) return corePath;
            Path local = CONFIG_DIR.resolve(isWindows() ? "punbrain-core.exe" : "punbrain-core");
            if (Files.isRegularFile(local)) return local.toAbsolutePath().toString();
            return isWindows() ? "punbrain-core.exe" : "punbrain-core";
        }

        static Settings load() {
            try {
                Files.createDirectories(CONFIG_DIR);
                if (Files.isRegularFile(CONFIG_FILE)) {
                    Settings loaded = GSON.fromJson(Files.readString(CONFIG_FILE), Settings.class);
                    if (loaded != null) return loaded;
                }
                Settings fresh = new Settings();
                Files.writeString(CONFIG_FILE, GSON.toJson(fresh));
                return fresh;
            } catch (Exception e) {
                return new Settings();
            }
        }

        private static boolean isWindows() {
            return System.getProperty("os.name", "").toLowerCase(Locale.ROOT).contains("win");
        }
    }

    private record Theme(int background, int accent, int text, int muted, int good, int bad) {
        static Theme from(String name) {
            if ("light".equalsIgnoreCase(name)) {
                return new Theme(0xDDF7F7F7, 0xFF5D2BE0, 0xFF151515,
                        0xFF555555, 0xFF147D45, 0xFFB42318);
            }
            if ("speedrun".equalsIgnoreCase(name)) {
                return new Theme(0xD9111519, 0xFF66FF99, 0xFFF4FFF8,
                        0xFFA8B3AD, 0xFF66FF99, 0xFFFF6B6B);
            }
            return new Theme(0xDD0B0E14, 0xFFB9FF4A, 0xFFF4F7FA,
                    0xFF9AA5B1, 0xFF65E6A6, 0xFFFF6B81);
        }
    }
}
