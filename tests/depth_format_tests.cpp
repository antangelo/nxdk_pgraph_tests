#include "depth_format_tests.h"

#include <hal/xbox.h>
#include <pbkit/pbkit.h>
#include <windows.h>

#include "../shaders/precalculated_vertex_shader.h"
#include "../test_host.h"
#include "../texture_format.h"
#include "nxdk_ext.h"
#include "pbkit_ext.h"
#include "vertex_buffer.h"

constexpr uint32_t kDepthFormats[] = {
    NV097_SET_SURFACE_FORMAT_ZETA_Z24S8,
    NV097_SET_SURFACE_FORMAT_ZETA_Z16,
};

constexpr uint32_t kNumDepthFormats = sizeof(kDepthFormats) / sizeof(kDepthFormats[0]);

DepthFormatTests::DepthFormatTests(TestHost &host, std::string output_dir) : TestBase(host, std::move(output_dir)) {}

void DepthFormatTests::Run() {
  // NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8
  const TextureFormatInfo &texture_format = kTextureFormats[3];
  host_.SetTextureFormat(texture_format);

  auto shader = std::make_shared<PrecalculatedVertexShader>(false);
  host_.SetShaderProgram(shader);

  // Prepare a large quad at the maximum depth with a number of smaller quads on top of it with progressively higher
  // z-buffer values.

  auto fb_width = static_cast<float>(host_.GetFramebufferWidth());
  auto fb_height = static_cast<float>(host_.GetFramebufferHeight());

  float left = 120.0f;
  float right = left + (fb_width - left * 2.0f);
  float top = 40;
  float bottom = top + (fb_height - top * 2.0f);
  uint32_t big_quad_width = static_cast<int>(right - left);
  uint32_t big_quad_height = static_cast<int>(bottom - top);

  constexpr uint32_t kSmallSize = 30;
  constexpr uint32_t kSmallSpacing = 15;
  constexpr uint32_t kStep = kSmallSize + kSmallSpacing;

  uint32_t quads_per_row = (big_quad_width - kSmallSize) / kStep;
  uint32_t quads_per_col = (big_quad_height - kSmallSize) / kStep;

  uint32_t row_size = kSmallSize + quads_per_row * kStep;
  uint32_t col_size = kSmallSize + quads_per_row * kStep;

  uint32_t x_offset = left + kSmallSpacing + (big_quad_width - row_size) / 2;
  uint32_t y_offset = top + kSmallSpacing + (big_quad_height - col_size) / 2;

  uint32_t num_quads = 1 + quads_per_row * quads_per_col;
  std::shared_ptr<VertexBuffer> buffer = host_.AllocateVertexBuffer(6 * num_quads);

  constexpr uint32_t kMaxDepth = 0x00FFFFFF;
  constexpr float kMaxDepthFloat = static_cast<float>(kMaxDepth);
  float z_inc = kMaxDepthFloat / (num_quads + 1.0f);

  // Quads are intentionally rendered from front to back to verify the behavior of the depth buffer.
  uint32_t idx = 0;
  float z_left = 0.0f;
  float right_offset = 10.0f;
  float y = y_offset;
  Color ul = {1.0, 1.0, 1.0, 1.0};
  Color ll = {1.0, 1.0, 1.0, 1.0};
  Color lr = {1.0, 1.0, 1.0, 1.0};
  Color ur = {1.0, 1.0, 1.0, 1.0};

  for (int y_idx = 0; y_idx < quads_per_col; ++y_idx, y += kStep) {
    float x = x_offset;
    for (int x_idx = 0; x_idx < quads_per_row; ++x_idx, z_left += z_inc, right_offset *= -1.0f, x += kStep) {
      float z_right = z_left + right_offset;
      float left_color = 0.25f + (z_left / kMaxDepthFloat * 0.75f);
      float right_color = 0.25f + (z_right / kMaxDepthFloat * 0.75f);
      ul.SetGrey(left_color);
      ll.SetGrey(left_color);
      lr.SetGrey(right_color);
      ur.SetGrey(right_color);
      buffer->DefineQuad(idx++, x, y, x + kSmallSize, y + kSmallSize, z_left, z_left, z_right, z_right, ul, ll, lr, ur);
    }
  }

  ul.SetRGB(0.0, 1.0, 0.0);
  ll.SetRGB(1.0, 0.0, 0.0);
  lr.SetRGB(0.0, 0.0, 1.0);
  ur.SetRGB(0.3, 0.3, 0.3);
  constexpr float kBackDepth = kMaxDepthFloat - 2.0f;
  constexpr float kFrontDepth = kBackDepth - (kBackDepth / 3.0f);
  buffer->DefineQuad(
      idx++,
      left,
      top,
      right,
      bottom,
      kFrontDepth,
      kBackDepth,
      kBackDepth,
      kFrontDepth,
      ul,
      ll,
      lr,
      ur);

  constexpr uint32_t kNumDepthTests = 16;
  constexpr uint32_t kDepthCutoffStep = kMaxDepth / kNumDepthTests;

  constexpr bool kCompressionSettings[] = {false, true};
  for (auto compression_enabled: kCompressionSettings) {
    for (auto depth_format : kDepthFormats) {
      uint32_t depth_cutoff = kMaxDepthFloat;
      for (int i = 0; i <= kNumDepthTests; ++i, depth_cutoff -= kDepthCutoffStep) {
        Test(depth_format, false, depth_cutoff);
      }

      // This should always render black if the depth test mode is LESS.
      Test(depth_format, compression_enabled, 0);
    }
  }
}

void DepthFormatTests::Test(uint32_t depth_format, bool compress_z, uint32_t depth_cutoff) {
  host_.SetDepthBufferFormat(depth_format);
  host_.PrepareDraw(0xFF000000, depth_cutoff, 0x00);

  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_CONTROL0, MASK(NV097_SET_CONTROL0_Z_FORMAT, NV097_SET_CONTROL0_Z_FORMAT_FIXED) | 1);

  p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, true);
  p = pb_push1(p, NV097_SET_DEPTH_MASK, true);
  p = pb_push1(p, NV097_SET_DEPTH_FUNC, NV097_SET_DEPTH_FUNC_V_LESS);

  p = pb_push1(p, NV097_SET_COMPRESS_ZBUFFER_EN, compress_z);

  //#define CULL_NEAR_FAR_SHIFT 0
  //#define ZCLAMP_SHIFT 4
  //#define CULL_IGNORE_W_SHIFT 8
  //  uint32_t min_max_control = (1 << CULL_NEAR_FAR_SHIFT) | (1 << ZCLAMP_SHIFT) | (1 << CULL_IGNORE_W_SHIFT);
  //  p = pb_push1(p, NV097_SET_ZMIN_MAX_CONTROL, min_max_control);

  p = pb_push1(p, NV097_SET_STENCIL_TEST_ENABLE, false);
  p = pb_push1(p, NV097_SET_STENCIL_MASK, false);
  pb_end(p);

  host_.DrawVertices();

  pb_print("DF: %d\n", depth_format);
  pb_print("CompZ: %d\n", compress_z);
  pb_print("Max: %x\n", depth_cutoff);
  pb_draw_text_screen();

  char buf[64] = {0};
  snprintf(buf, 63, "DepthFmt_DF_%d", depth_format);

  host_.FinishDrawAndSave(output_dir_.c_str(), buf);
}
