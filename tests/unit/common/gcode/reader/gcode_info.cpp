#include "test_files.hpp"

#include <gcode_reader_any.hpp>
#include <gcode_info.hpp>
#include <catch2/catch.hpp>

namespace {

struct TestFile {
    const char *path;
    bool encrypted;
    // Any info or metadata available.
    bool has_any_info;
};

// Test both on plaintext and binary readers. They behave slightly differently
// on the outside. No need to do different compressions or so, though, these shoud be the same.
const std::vector<TestFile> test_files = {
    { NEW_PLAIN, false, true },
    { NEW_BINARY, false, true },
    { NEW_ENCRYPTED, true, true },
    { NEW_ENCRYPTED_MULTI, true, true },
    { NEW_ENCRYPTED_POLY, true, true },
    // With a fully encrypted gcode, we don't "see" the metadata, as it is encrypted.
    { NEW_ENCRYPTED_FULLY, true, false },
    { NEW_SIGNED, /* technically, yes, encrypted */ true, true },
};

} // namespace

TEST_CASE("GCodeInfo") {
    for (const auto &def : test_files) {
        const char *filename = def.path;
        SECTION(std::string("Test-file: ") + filename) {
            AnyGcodeFormatReader reader(filename);
            REQUIRE(reader.is_open());

            GCodeInfo info;
            info.load(*reader.get());
            CHECK(info.is_loaded());
            CHECK(!info.has_error());

            CHECK(info.has_preview_thumbnail() == def.has_any_info);
            CHECK(info.has_progress_thumbnail() == def.has_any_info);
            CHECK(info.has_filament_described() == def.has_any_info);
            if (!def.encrypted) {
                // BUG: These are in the encrypted section. The gcode info
                // doesn't open this (on purpose). But these are not accessible
                // for us in this case.
                CHECK(info.get_bed_preheat_temp() == 60);
                CHECK(info.get_hotend_preheat_temp() == 215);
            }

            if (def.has_any_info) {
                CHECK(info.is_singletool_gcode());
                CHECK(info.UsedExtrudersCount() == 1);
                auto extruder_info = info.get_extruder_info(0);
                CHECK(extruder_info.used());
                CHECK(strcmp(extruder_info.filament_name->data(), "PLA") == 0);
                CHECK(!extruder_info.requires_hardened_nozzle.has_value());
                CHECK(!extruder_info.requires_high_flow_nozzle.has_value());
            }
        }
    }
}
