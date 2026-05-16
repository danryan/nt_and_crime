/*
 * sim_gainCustomUI - JSON-on-stdin scenario driver for the gainCustomUI plug-in.
 *
 * usage: sim_gainCustomUI --scenario-json - [--output-dir DIR] [--quiet]
 *
 * flags:
 *   --scenario-json -   read scenario JSON from stdin (required; only "-" is supported)
 *   --output-dir DIR    where to write out_bus.bin, out_screen.bin, out_params.log
 *                       (default: ./out/)
 *   --quiet             suppress per-step progress output
 *
 * stdout: progress lines like "step 1/8: frames 0-31"
 *
 * exit codes:
 *   0  scenario completed; outputs written
 *   1  scenario failed to parse
 *   2  plugin construction failed
 *   3  scenario runtime error
 */

#include "nt_runtime.h"
#include "nt_jsonstream.h"
#include "plugin_loader.h"

#include <distingnt/api.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void print_usage(const char* argv0) {
    std::printf(
        "usage: %s --scenario-json - [--output-dir DIR] [--quiet]\n"
        "\n"
        "flags:\n"
        "  --scenario-json -   read scenario JSON from stdin (required)\n"
        "  --output-dir DIR    where to write out_bus.bin, out_screen.bin, out_params.log\n"
        "                      (default: ./out/)\n"
        "  --quiet             suppress per-step progress output\n"
        "\n"
        "exit codes:\n"
        "  0  scenario completed; outputs written\n"
        "  1  scenario failed to parse\n"
        "  2  plugin construction failed\n"
        "  3  scenario runtime error\n",
        argv0
    );
}

static std::string read_stdin() {
    std::string buf;
    char chunk[4096];
    while (std::fgets(chunk, (int)sizeof(chunk), stdin)) {
        buf += chunk;
    }
    return buf;
}

// Write a raw byte buffer to a file; return false on error.
static bool write_file(const std::string& path, const void* data, size_t size) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "error: cannot open '%s' for writing\n", path.c_str());
        return false;
    }
    size_t written = std::fwrite(data, 1, size, f);
    std::fclose(f);
    return written == size;
}

// Ensure a directory path ends with '/'.
static std::string ensure_slash(const std::string& dir) {
    if (dir.empty() || dir.back() != '/') return dir + '/';
    return dir;
}

// ---------------------------------------------------------------------------
// Scenario parsing helpers
// ---------------------------------------------------------------------------

struct Scenario {
    std::string plugin_name;
    // params: pairs of (name, int16 value)
    struct Param { std::string name; int16_t value; };
    std::vector<Param> params;
    // input_buses: 1-based bus index -> type ("silence" only for now)
    struct InputBus { int bus; std::string type; };
    std::vector<InputBus> input_buses;
    int duration_frames = 64;
    // events: ignored in v1
    // expect: ignored in v1 (golden compare is a later task)
};

// Parse a flat JSON object of the form {"Name": value, ...} where values are
// numbers. Returns false on parse error.
static bool parse_params(
    _NT_jsonParse& parse,
    const nt::LoadedPlugin* loaded,
    std::vector<Scenario::Param>& out
) {
    int num = 0;
    if (!parse.numberOfObjectMembers(num)) return false;

    const _NT_algorithm* alg = loaded->algorithm;
    const _NT_factory*   fac = loaded->factory;

    // Determine parameter count via calculateRequirements.
    _NT_algorithmRequirements areq{};
    fac->calculateRequirements(areq, nullptr);
    int param_count = (int)areq.numParameters;

    for (int i = 0; i < num; ++i) {
        // Find which parameter name matches the current member.
        int matched_idx = -1;
        for (int p = 0; p < param_count; ++p) {
            if (parse.matchName(alg->parameters[p].name)) {
                matched_idx = p;
                break;
            }
        }
        if (matched_idx >= 0) {
            int v = 0;
            if (!parse.number(v)) return false;
            out.push_back({alg->parameters[matched_idx].name, (int16_t)v});
        } else {
            if (!parse.skipMember()) return false;
        }
    }
    return true;
}

// Parse "input_buses": {"1": "silence", ...}.
static bool parse_input_buses(
    _NT_jsonParse& parse,
    std::vector<Scenario::InputBus>& out
) {
    int num = 0;
    if (!parse.numberOfObjectMembers(num)) return false;
    for (int i = 0; i < num; ++i) {
        // We don't have matchName-by-index; we have to check each expected key.
        // Since we only support bus indices 1..64, iterate them.
        bool found = false;
        for (int bus = 1; bus <= 64 && !found; ++bus) {
            char key[8];
            std::snprintf(key, sizeof(key), "%d", bus);
            if (parse.matchName(key)) {
                found = true;
                const char* type_str = nullptr;
                if (!parse.string(type_str)) return false;
                out.push_back({bus, type_str ? type_str : ""});
            }
        }
        if (!found) {
            if (!parse.skipMember()) return false;
        }
    }
    return true;
}

// Top-level scenario JSON parse. Returns true on success.
static bool parse_scenario(
    _NT_jsonParse& parse,
    Scenario& sc,
    const nt::LoadedPlugin* loaded
) {
    int num = 0;
    if (!parse.numberOfObjectMembers(num)) return false;

    for (int i = 0; i < num; ++i) {
        if (parse.matchName("plugin")) {
            const char* s = nullptr;
            if (!parse.string(s)) return false;
            sc.plugin_name = s ? s : "";
        } else if (parse.matchName("params")) {
            if (!parse_params(parse, loaded, sc.params)) return false;
        } else if (parse.matchName("input_buses")) {
            if (!parse_input_buses(parse, sc.input_buses)) return false;
        } else if (parse.matchName("duration_frames")) {
            int v = 0;
            if (!parse.number(v)) return false;
            sc.duration_frames = v;
        } else {
            // events, expect, and any future keys: skip.
            if (!parse.skipMember()) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    const char* scenario_src = nullptr;  // "-" = stdin
    std::string output_dir   = "./out/";
    bool quiet = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "--scenario-json") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --scenario-json requires an argument\n");
                return 1;
            }
            scenario_src = argv[++i];
        } else if (std::strcmp(argv[i], "--output-dir") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --output-dir requires an argument\n");
                return 1;
            }
            output_dir = ensure_slash(argv[++i]);
        } else if (std::strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else {
            std::fprintf(stderr, "error: unknown flag '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!scenario_src) {
        std::fprintf(stderr, "error: --scenario-json is required\n");
        print_usage(argv[0]);
        return 1;
    }
    if (std::strcmp(scenario_src, "-") != 0) {
        std::fprintf(stderr, "error: only '-' (stdin) is supported for --scenario-json in v1\n");
        return 1;
    }

    // --- Read JSON from stdin ---
    std::string json = read_stdin();
    if (json.empty()) {
        std::fprintf(stderr, "error: no JSON received on stdin\n");
        return 1;
    }

    // --- Initialise runtime ---
    nt::reset_runtime();

    // --- Load the plug-in ---
    nt::LoadedPlugin* loaded = nt::load_plugin();
    if (!loaded || !loaded->factory || !loaded->algorithm) {
        std::fprintf(stderr, "error: plugin construction failed\n");
        return 2;
    }

    // --- Parse scenario ---
    auto parse_host = nt::make_json_parse(json);
    Scenario sc;
    if (!parse_scenario(*parse_host, sc, loaded)) {
        std::fprintf(stderr, "error: scenario JSON parse failed\n");
        return 1;
    }

    // --- Open param log ---
    std::string param_log_path = output_dir + "out_params.log";
    // Create output_dir if needed (best-effort; write_file will report error).
    {
        // portable mkdir-p substitute: system call
        std::string cmd = "mkdir -p \"" + output_dir + "\"";
        int rc = std::system(cmd.c_str());
        (void)rc;
    }
    FILE* param_log = std::fopen(param_log_path.c_str(), "w");
    if (!param_log) {
        std::fprintf(stderr, "error: cannot open '%s' for writing\n", param_log_path.c_str());
        return 3;
    }
    nt::set_param_log(param_log);

    // --- Apply params ---
    _NT_algorithmRequirements areq{};
    loaded->factory->calculateRequirements(areq, nullptr);
    int param_count = (int)areq.numParameters;

    for (const Scenario::Param& sp : sc.params) {
        // Find the index of this parameter by name.
        for (int p = 0; p < param_count; ++p) {
            if (std::strcmp(loaded->algorithm->parameters[p].name, sp.name.c_str()) == 0) {
                NT_setParameterFromUi((uint32_t)NT_algorithmIndex(loaded->algorithm),
                                      (uint32_t)p + NT_parameterOffset(),
                                      sp.value);
                break;
            }
        }
    }

    // --- Set up bus frame count ---
    // Choose block size: 8 quads (32 frames per step) as the fixed default.
    const int numFramesBy4 = 8;       // 8 * 4 = 32 frames per step
    const int framesPerStep = numFramesBy4 * 4;

    // Total steps needed (round up).
    int total_steps = (sc.duration_frames + framesPerStep - 1) / framesPerStep;
    int total_frames = total_steps * framesPerStep;

    // Accumulate all bus output across the full scenario duration.
    // Layout: num_buses * total_frames floats, bus-major.
    const int NUM_BUSES = nt::num_buses();  // 64
    std::vector<float> all_bus_output((size_t)NUM_BUSES * (size_t)total_frames, 0.0f);

    // --- Run steps ---
    for (int step_idx = 0; step_idx < total_steps; ++step_idx) {
        int frame_start = step_idx * framesPerStep;

        // Set bus frame count and zero bus storage for this step.
        nt::set_bus_frame_count(framesPerStep);
        float* bus_base = nt::bus_frames_base();
        std::memset(bus_base, 0, (size_t)NUM_BUSES * (size_t)framesPerStep * sizeof(float));

        // Apply input_buses (silence = zero, which is already the case).
        // Non-silence bus types are reserved for future tasks.

        // Call step().
        loaded->factory->step(loaded->algorithm, bus_base, numFramesBy4);

        // Copy this step's output into the accumulator.
        for (int bus = 0; bus < NUM_BUSES; ++bus) {
            const float* src = bus_base + (size_t)bus * (size_t)framesPerStep;
            float* dst = all_bus_output.data() + (size_t)bus * (size_t)total_frames + (size_t)frame_start;
            std::memcpy(dst, src, (size_t)framesPerStep * sizeof(float));
        }

        if (!quiet) {
            std::printf("step %d/%d: frames %d-%d\n",
                        step_idx + 1, total_steps,
                        frame_start, frame_start + framesPerStep - 1);
        }
    }

    // --- Final draw ---
    std::memset(NT_screen, 0, sizeof(NT_screen));
    if (loaded->factory->draw) {
        loaded->factory->draw(loaded->algorithm);
    }

    // --- Close param log ---
    nt::set_param_log(nullptr);
    std::fclose(param_log);

    // --- Write outputs ---
    std::string bus_path    = output_dir + "out_bus.bin";
    std::string screen_path = output_dir + "out_screen.bin";

    size_t bus_bytes = (size_t)NUM_BUSES * (size_t)total_frames * sizeof(float);
    if (!write_file(bus_path, all_bus_output.data(), bus_bytes)) return 3;

    size_t screen_bytes = sizeof(NT_screen);  // 128 * 64 = 8192
    if (!write_file(screen_path, NT_screen, screen_bytes)) return 3;

    if (!quiet) {
        std::printf("outputs written to %s\n", output_dir.c_str());
        std::printf("  out_bus.bin    %zu bytes (%d buses x %d frames x 4)\n",
                    bus_bytes, NUM_BUSES, total_frames);
        std::printf("  out_screen.bin %zu bytes\n", screen_bytes);
        std::printf("  out_params.log\n");
    }

    return 0;
}
