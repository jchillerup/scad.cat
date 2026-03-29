#include "openscad_api.h"

// openscad.h is reachable via the PUBLIC include dirs of OpenSCADLibInternal
// (upstream/openscad/src is listed as PUBLIC in their CMakeLists.txt).
#include "openscad.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

// Globals declared in upstream/openscad/src/openscad.cc that we reset between
// calls to prevent `-D var=val` definitions from bleeding across evaluations.
extern std::string commandline_commands;

extern "C" {

int openscad_evaluate(const char *scad_source,
                      uint8_t **stl_out, size_t *stl_len,
                      char **error_out)
{
    // Reset accumulated command-line variable definitions from any prior call.
    commandline_commands.clear();

    // Write the SCAD source to a temp file.
    const fs::path tmp_dir  = fs::temp_directory_path();
    const fs::path scad_path = tmp_dir / "openscad_api_in.scad";
    const fs::path stl_path  = tmp_dir / "openscad_api_out.stl";

    {
        std::ofstream f(scad_path);
        if (!f) {
            if (error_out) *error_out = strdup("openscad_api: failed to write temp SCAD file");
            return -1;
        }
        f << scad_source;
    }

    // Build argv that openscad_main() will parse.
    // Use --backend Manifold explicitly; it is the default but being explicit
    // avoids any user config files or env vars overriding it.
    const std::string scad_str = scad_path.string();
    const std::string stl_str  = stl_path.string();
    const char *argv[] = {
        "openscad",
        scad_str.c_str(),
        "--export-format", "binstl",
        "-o", stl_str.c_str(),
        "--backend", "Manifold",
        nullptr
    };
    constexpr int argc = 8;

    const int rc = openscad_main(argc, const_cast<char **>(argv));

    fs::remove(scad_path);

    if (rc != 0) {
        fs::remove(stl_path);
        if (error_out) *error_out = strdup("openscad_api: evaluation failed (see stderr for details)");
        return -1;
    }

    // Read the produced STL bytes.
    std::ifstream stl_file(stl_path, std::ios::binary);
    if (!stl_file) {
        if (error_out) *error_out = strdup("openscad_api: failed to read output STL");
        return -1;
    }
    std::vector<uint8_t> buf(std::istreambuf_iterator<char>(stl_file),
                              std::istreambuf_iterator<char>());
    stl_file.close();
    fs::remove(stl_path);

    if (buf.empty()) {
        if (error_out) *error_out = strdup("openscad_api: OpenSCAD produced empty STL");
        return -1;
    }

    *stl_out = static_cast<uint8_t *>(malloc(buf.size()));
    if (!*stl_out) {
        if (error_out) *error_out = strdup("openscad_api: out of memory");
        return -1;
    }
    memcpy(*stl_out, buf.data(), buf.size());
    *stl_len = buf.size();
    return 0;
}

void openscad_free(void *ptr) { free(ptr); }

} // extern "C"
