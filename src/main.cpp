/*
 * This sample provides a very basic demonstration of 3D rendering on the Xbox,
 * using pbkit. Based on the pbkit demo sources.
 */

#include <SDL_image.h>
#include <hal/debug.h>
#include <hal/fileio.h>
#include <hal/video.h>
#include <nxdk/mount.h>
#include <pbkit/pbkit.h>
#include <windows.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "debug_output.h"
#include "logger.h"
#include "test_driver.h"
#include "test_host.h"
#include "tests/antialiasing_tests.h"
#include "tests/attribute_carryover_tests.h"
#include "tests/attribute_explicit_setter_tests.h"
#include "tests/attribute_float_tests.h"
#include "tests/blend_tests.h"
#include "tests/clear_tests.h"
#include "tests/color_mask_blend_tests.h"
#include "tests/color_zeta_disable_tests.h"
#include "tests/color_zeta_overlap_tests.h"
#include "tests/combiner_tests.h"
#include "tests/depth_format_fixed_function_tests.h"
#include "tests/depth_format_tests.h"
#include "tests/fog_tests.h"
#include "tests/front_face_tests.h"
#include "tests/image_blit_tests.h"
#include "tests/inline_array_size_mismatch.h"
#include "tests/lighting_normal_tests.h"
#include "tests/material_alpha_tests.h"
#include "tests/material_color_source_tests.h"
#include "tests/material_color_tests.h"
#include "tests/null_surface_tests.h"
#include "tests/overlapping_draw_modes_tests.h"
#include "tests/pvideo_tests.h"
#include "tests/set_vertex_data_tests.h"
#include "tests/shade_model_tests.h"
#include "tests/smoothing_tests.h"
#include "tests/stencil_tests.h"
#include "tests/surface_clip_tests.h"
#include "tests/surface_pitch_tests.h"
#include "tests/texgen_matrix_tests.h"
#include "tests/texgen_tests.h"
#include "tests/texture_border_tests.h"
#include "tests/texture_cpu_update_tests.h"
#include "tests/texture_cubemap_tests.h"
#include "tests/texture_format_dxt_tests.h"
#include "tests/texture_format_tests.h"
#include "tests/texture_framebuffer_blit_tests.h"
#include "tests/texture_matrix_tests.h"
#include "tests/texture_render_target_tests.h"
#include "tests/texture_render_update_in_place_tests.h"
#include "tests/texture_shadow_comparator_tests.h"
#include "tests/texture_signed_component_tests.h"
#include "tests/three_d_primitive_tests.h"
#include "tests/two_d_line_tests.h"
#include "tests/vertex_shader_independence_tests.h"
#include "tests/vertex_shader_rounding_tests.h"
#include "tests/vertex_shader_swizzle_tests.h"
#include "tests/viewport_tests.h"
#include "tests/volume_texture_tests.h"
#include "tests/w_param_tests.h"
#include "tests/window_clip_tests.h"
#include "tests/zero_stride_tests.h"

#ifndef FALLBACK_OUTPUT_ROOT_PATH
#define FALLBACK_OUTPUT_ROOT_PATH "e:\\";
#endif
static constexpr int kFramebufferWidth = 640;
static constexpr int kFramebufferHeight = 480;
static constexpr int kTextureWidth = 256;
static constexpr int kTextureHeight = 256;

static constexpr const char* kLogFileName = "pgraph_progress_log.txt";

static void register_suites(TestHost& host, std::vector<std::shared_ptr<TestSuite>>& test_suites,
                            const std::string& output_directory);
static bool get_writable_output_directory(std::string& xbe_root_directory);
static bool get_test_output_path(std::string& test_output_directory);
static void dump_config_file(const std::string& config_file_path,
                             const std::vector<std::shared_ptr<TestSuite>>& test_suites);
static bool discover_historical_crashes(const std::string& log_file_path,
                                        std::map<std::string, std::set<std::string>>& crashes);
static bool process_config(const char* config_file_path, std::vector<std::shared_ptr<TestSuite>>& test_suites);

/* Main program function */
int main() {
  XVideoSetMode(kFramebufferWidth, kFramebufferHeight, 32, REFRESH_DEFAULT);

  int status = pb_init();
  if (status) {
    debugPrint("pb_init Error %d\n", status);
    pb_show_debug_screen();
    Sleep(2000);
    return 1;
  }

  if (SDL_Init(SDL_INIT_GAMECONTROLLER)) {
    debugPrint("Failed to initialize SDL_GAMECONTROLLER.");
    debugPrint("%s", SDL_GetError());
    pb_show_debug_screen();
    Sleep(2000);
    return 1;
  }

  if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
    debugPrint("Failed to initialize SDL_image PNG mode.");
    pb_show_debug_screen();
    Sleep(2000);
    return 1;
  }

  std::string test_output_directory;
  if (!get_test_output_path(test_output_directory)) {
    debugPrint("Failed to mount %s", test_output_directory.c_str());
    pb_show_debug_screen();
    Sleep(2000);
    return 1;
  };

  pb_show_front_screen();

  TestHost host(kFramebufferWidth, kFramebufferHeight, kTextureWidth, kTextureHeight);

  std::vector<std::shared_ptr<TestSuite>> test_suites;
  register_suites(host, test_suites, test_output_directory);

  TestHost::EnsureFolderExists(test_output_directory);

  std::map<std::string, std::set<std::string>> historical_crashes;
#ifdef ENABLE_PROGRESS_LOG
  {
    std::string log_file = test_output_directory + "\\" + kLogFileName;
#ifdef ENABLE_INTERACTIVE_CRASH_AVOIDANCE
    discover_historical_crashes(log_file, historical_crashes);
#endif
    DeleteFile(log_file.c_str());

    Logger::Initialize(log_file, true);

    if (!historical_crashes.empty()) {
      Logger::Log() << "<HistoricalCrashes>" << std::endl;
      for (auto& suite_tests : historical_crashes) {
        for (auto& test : suite_tests.second) {
          Logger::Log() << "Crash? " << suite_tests.first << "::" << test << std::endl;
        }
      }
      Logger::Log() << "</HistoricalCrashes>" << std::endl;
    }
  }
#endif

#ifdef DUMP_CONFIG_FILE
  dump_config_file(test_output_directory + "\\config.cnf", test_suites);
#endif

  bool config_parsed = false;
#ifdef RUNTIME_CONFIG_PATH
  config_parsed = process_config(RUNTIME_CONFIG_PATH, test_suites);
#endif
  if (!config_parsed) {
    // Optionally parse a bundled config.cnf
    process_config("d:\\pgraph_tests.cnf", test_suites);
  }

  if (!historical_crashes.empty()) {
    for (auto& suite : test_suites) {
      auto crash_info = historical_crashes.find(suite->Name());
      if (crash_info != historical_crashes.end()) {
        suite->SetSuspectedCrashes(crash_info->second);
      }
    }
  }

  TestDriver driver(host, test_suites, kFramebufferWidth, kFramebufferHeight, !historical_crashes.empty());
  driver.Run();

#ifdef ENABLE_SHUTDOWN
  HalInitiateShutdown();
#else
  debugPrint("Results written to %s\n\nRebooting in 4 seconds...\n", test_output_directory.c_str());
#endif
  pb_show_debug_screen();
  Sleep(4000);

  pb_kill();
  return 0;
}

static bool ensure_drive_mounted(char drive_letter) {
  if (nxIsDriveMounted(drive_letter)) {
    return true;
  }

  char dos_path[4] = "x:\\";
  dos_path[0] = drive_letter;
  char device_path[256] = {0};
  if (XConvertDOSFilenameToXBOX(dos_path, device_path) != STATUS_SUCCESS) {
    return false;
  }

  if (!strstr(device_path, R"(\Device\Harddisk0\Partition)")) {
    return false;
  }
  device_path[28] = 0;

  return nxMountDrive(drive_letter, device_path);
}

static bool get_writable_output_directory(std::string& xbe_root_directory) {
  std::string xbe_directory = XeImageFileName->Buffer;
  if (xbe_directory.find("\\Device\\CdRom") == 0) {
    debugPrint("Running from readonly media, using default path for test output.\n");
    xbe_root_directory = FALLBACK_OUTPUT_ROOT_PATH;

    std::replace(xbe_root_directory.begin(), xbe_root_directory.end(), '/', '\\');

    return ensure_drive_mounted(xbe_root_directory.front());
  }

  xbe_root_directory = "D:";
  return true;
}

static bool get_test_output_path(std::string& test_output_directory) {
  if (!get_writable_output_directory(test_output_directory)) {
    return false;
  }
  char last_char = test_output_directory.back();
  if (last_char == '\\' || last_char == '/') {
    test_output_directory.pop_back();
  }
  test_output_directory += "\\nxdk_pgraph_tests";
  return true;
}

static void dump_config_file(const std::string& config_file_path,
                             const std::vector<std::shared_ptr<TestSuite>>& test_suites) {
  if (!ensure_drive_mounted(config_file_path[0])) {
    ASSERT(!"Failed to mount config path")
  }

  const char* out_path = config_file_path.c_str();
  PrintMsg("Writing config file to %s\n", out_path);

  std::ofstream config_file(config_file_path);
  ASSERT(config_file && "Failed to open config file for output");

  config_file << "# pgraph test suite configuration" << std::endl;
  config_file << "# Lines starting with '#' are ignored." << std::endl;
  config_file << "# To enable a test suite, add its name on a single line with no leading #. E.g.," << std::endl;
  config_file << "# Lighting normals" << std::endl;
  config_file << "# To disable a single test within a suite, add the name of the test prefixed with" << std::endl;
  config_file << "#  a '-' after the uncommented suite. E.g.," << std::endl;
  config_file << "# -NoNormal" << std::endl;
  config_file << std::endl;

  for (auto& suite : test_suites) {
    config_file << suite->Name() << std::endl;
    for (auto& test_name : suite->TestNames()) {
      config_file << "# " << test_name << std::endl;
    }
    config_file << std::endl << "#-------------------" << std::endl;
  }
}

static bool discover_historical_crashes(const std::string& log_file_path,
                                        std::map<std::string, std::set<std::string>>& crashes) {
  crashes.clear();
  if (!ensure_drive_mounted(log_file_path[0])) {
    return false;
  }

  std::string dos_style_path = log_file_path;
  std::replace(dos_style_path.begin(), dos_style_path.end(), '/', '\\');

  std::ifstream log_file(dos_style_path.c_str());
  if (!log_file) {
    return false;
  }

  std::string last_test_suite;
  std::string last_test_name;
  std::string line;

  auto add_crash = [&crashes](const std::string& suite, const std::string& test) {
    auto toskip = crashes.find(suite);
    if (toskip == crashes.end()) {
      crashes[suite] = {test};
    } else {
      toskip->second.emplace(test);
    }
  };

  while (std::getline(log_file, line)) {
    PrintMsg("'%s'\n", line.c_str());
    if (!line.compare(0, 7, "Crash? ")) {
      line = line.substr(7);
      auto delimiter = line.find("::");
      add_crash(line.substr(0, delimiter), line.substr(delimiter + 2));
      continue;
    }

    if (!line.compare(0, 9, "Starting ")) {
      if (!last_test_suite.empty()) {
        PrintMsg("Potential crash: '%s' '%s'\n", last_test_suite.c_str(), last_test_name.c_str());
        add_crash(last_test_suite, last_test_name);
      }

      line = line.substr(9);
      auto delimiter = line.find("::");
      last_test_suite = line.substr(0, delimiter);
      last_test_name = line.substr(delimiter + 2);
      continue;
    }

    if (!line.compare(0, 13, "  Completed '")) {
      std::string test_name = line.substr(13);
      auto delimiter = test_name.rfind("' in ");
      test_name = test_name.substr(0, delimiter);
      if (test_name == last_test_name) {
        auto crash_suite = crashes.find(last_test_suite);
        if (crash_suite != crashes.end()) {
          crash_suite->second.erase(last_test_name);
        }
        last_test_suite.clear();
        last_test_name.clear();
        continue;
      }
      PrintMsg("Completed line mismatch: '%s' '%s' but completed: '%s'\n", last_test_suite.c_str(),
               last_test_name.c_str(), test_name.c_str());
    }

    PrintMsg("Unprocessed log line '%s'\n", line.c_str());
  }

  if (!last_test_suite.empty()) {
    PrintMsg("Potential crash: '%s' '%s'\n", last_test_suite.c_str(), last_test_name.c_str());
    add_crash(last_test_suite, last_test_name);
  }

  return true;
}

static bool process_config(const char* config_file_path, std::vector<std::shared_ptr<TestSuite>>& test_suites) {
  if (!ensure_drive_mounted(config_file_path[0])) {
    PrintMsg("Ignoring missing config at %s\n", config_file_path);
    return false;
  }

  std::string dos_style_path = config_file_path;
  std::replace(dos_style_path.begin(), dos_style_path.end(), '/', '\\');
  std::map<std::string, std::vector<std::string>> test_config;
  std::ifstream config_file(dos_style_path.c_str());
  if (!config_file) {
    PrintMsg("Ignoring missing config at %s\n", config_file_path);
    return false;
  }

  // The config file is a list of test suite names (one per line), each optionally followed by lines containing a test
  // name prefixed with '-' (indicating that test should be disabled).
  std::string last_test_suite;
  std::string line;
  while (std::getline(config_file, line)) {
    if (line.empty()) {
      continue;
    }
    if (line.front() == '-') {
      line.erase(0, 1);
      test_config[last_test_suite].push_back(line);
      continue;
    }
    if (line.front() == '#') {
      continue;
    }

    test_config[line] = {};
    last_test_suite = line;
  }

  std::vector<std::shared_ptr<TestSuite>> filtered_tests;
  for (auto& suite : test_suites) {
    auto config = test_config.find(suite->Name());
    if (config == test_config.end()) {
      continue;
    }

    if (!config->second.empty()) {
      suite->DisableTests(config->second);
    }
    filtered_tests.push_back(suite);
  }

  if (filtered_tests.empty()) {
    return true;
  }
  test_suites = filtered_tests;

  return true;
}

static void register_suites(TestHost& host, std::vector<std::shared_ptr<TestSuite>>& test_suites,
                            const std::string& output_directory) {
  // Must be the first suite run for valid results. The first test depends on having a cleared initial state.
  {
    auto suite = std::make_shared<LightingNormalTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<AntialiasingTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<AttributeCarryoverTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<AttributeExplicitSetterTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<AttributeFloatTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<BlendTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<ClearTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<ColorMaskBlendTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<ColorZetaDisableTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<ColorZetaOverlapTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<CombinerTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<FogTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<FogCustomShaderTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<FogInfiniteFogCoordinateTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<FogVec4CoordTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<FrontFaceTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<DepthFormatTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<DepthFormatFixedFunctionTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<ImageBlitTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<InlineArraySizeMismatchTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<MaterialAlphaTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<MaterialColorTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<MaterialColorSourceTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<NullSurfaceTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<OverlappingDrawModesTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<PvideoTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<SetVertexDataTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<ShadeModelTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<SmoothingTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<SurfaceClipTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<SurfacePitchTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<StencilTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureBorderTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureCPUUpdateTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureCubemapTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TexgenMatrixTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TexgenTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureFormatDXTTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureFormatTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureFramebufferBlitTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureMatrixTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureRenderTargetTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureRenderUpdateInPlaceTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureShadowComparatorTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TextureSignedComponentTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<ThreeDPrimitiveTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<TwoDLineTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<VertexShaderIndependenceTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<VertexShaderRoundingTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<VertexShaderSwizzleTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<ViewportTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<VolumeTextureTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<WParamTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<WindowClipTests>(host, output_directory);
    test_suites.push_back(suite);
  }
  {
    auto suite = std::make_shared<ZeroStrideTests>(host, output_directory);
    test_suites.push_back(suite);
  }
}
