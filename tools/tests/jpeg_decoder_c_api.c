/* Host-only compatibility checks for the actual installed TJpg_Decoder C core.
 * Compile this together with TJpg_Decoder/src/tjpgd.c and its original config.
 * No decoder source or firmware configuration is modified by this harness.
 */
#include <stdlib.h>
#include <string.h>
#include "tjpgd.h"

#if JD_FORMAT != 1
#error "The clock uses the decoder's RGB565 output format"
#endif

#define TEST_WIDTH 320U
#define TEST_HEIGHT 240U
#define TEST_PIXELS (TEST_WIDTH * TEST_HEIGHT)
#define GUARD_BYTES 64U
#define GUARD_VALUE 0xA5U

typedef struct {
  uint32_t prepare_result;
  uint32_t decode_result;
  uint32_t width;
  uint32_t height;
  uint32_t coverage_pixels;
  uint32_t rectangles;
  uint32_t output_checksum;
  uint32_t canaries_ok;
  uint32_t callbacks_ok;
  uint32_t workspace_remaining;
} DecoderReport;

typedef struct {
  const uint8_t *input;
  size_t input_size;
  size_t position;
  uint8_t *coverage;
  uint16_t *pixels;
  DecoderReport *report;
} DecodeContext;

static size_t read_jpeg(JDEC *decoder, uint8_t *destination, size_t requested) {
  DecodeContext *context = (DecodeContext *)decoder->device;
  size_t count = context->input_size - context->position;
  if (count > requested) count = requested;
  if (destination) memcpy(destination, context->input + context->position, count);
  context->position += count;
  return count;
}

static int capture_pixels(JDEC *decoder, void *bitmap, JRECT *rectangle) {
  DecodeContext *context = (DecodeContext *)decoder->device;
  DecoderReport *report = context->report;
  if (!bitmap || rectangle->left > rectangle->right ||
      rectangle->top > rectangle->bottom || rectangle->right >= TEST_WIDTH ||
      rectangle->bottom >= TEST_HEIGHT) {
    report->callbacks_ok = 0;
    return 0;
  }
  const uint16_t *source = (const uint16_t *)bitmap;
  uint32_t width = rectangle->right - rectangle->left + 1U;
  uint32_t height = rectangle->bottom - rectangle->top + 1U;
  for (uint32_t row = 0; row < height; ++row) {
    for (uint32_t column = 0; column < width; ++column) {
      uint32_t index = (rectangle->top + row) * TEST_WIDTH + rectangle->left + column;
      if (context->coverage[index]) {
        report->callbacks_ok = 0;
        return 0;
      }
      context->coverage[index] = 1;
      context->pixels[index] = source[row * width + column];
      ++report->coverage_pixels;
    }
  }
  ++report->rectangles;
  return 1;
}

static int check_guards(const uint8_t *allocation, size_t payload_size) {
  for (size_t i = 0; i < GUARD_BYTES; ++i) {
    if (allocation[i] != GUARD_VALUE ||
        allocation[GUARD_BYTES + payload_size + i] != GUARD_VALUE) return 0;
  }
  return 1;
}

#if defined(_WIN32)
__declspec(dllexport)
#endif
int test_decode_jpeg(const uint8_t *data, size_t size, DecoderReport *report) {
  if (!data || !size || !report) return 0;
  memset(report, 0, sizeof(*report));
  report->callbacks_ok = 1;
  report->decode_result = JDR_PAR;
  const size_t output_bytes = TEST_PIXELS * sizeof(uint16_t);
  uint8_t *workspace = (uint8_t *)malloc(TJPGD_WORKSPACE_SIZE + 2 * GUARD_BYTES);
  uint8_t *output = (uint8_t *)malloc(output_bytes + 2 * GUARD_BYTES);
  uint8_t *coverage = (uint8_t *)calloc(TEST_PIXELS, 1);
  if (!workspace || !output || !coverage) {
    free(workspace); free(output); free(coverage);
    return 0;
  }
  memset(workspace, GUARD_VALUE, TJPGD_WORKSPACE_SIZE + 2 * GUARD_BYTES);
  memset(output, GUARD_VALUE, output_bytes + 2 * GUARD_BYTES);
  DecodeContext context = {data, size, 0, coverage,
                          (uint16_t *)(output + GUARD_BYTES), report};
  JDEC decoder;
  memset(&decoder, 0, sizeof(decoder));
  report->prepare_result = jd_prepare(&decoder, read_jpeg, workspace + GUARD_BYTES,
                                     TJPGD_WORKSPACE_SIZE, &context);
  report->width = decoder.width;
  report->height = decoder.height;
  report->workspace_remaining = (uint32_t)decoder.sz_pool;
  if (report->prepare_result == JDR_OK && decoder.width == TEST_WIDTH &&
      decoder.height == TEST_HEIGHT) {
    decoder.swap = 0;
    report->decode_result = jd_decomp(&decoder, capture_pixels, 0);
  }
  report->canaries_ok = check_guards(workspace, TJPGD_WORKSPACE_SIZE) &&
                        check_guards(output, output_bytes);
  uint32_t checksum = 2166136261U;
  for (size_t i = 0; i < output_bytes; ++i) {
    checksum = (checksum ^ output[GUARD_BYTES + i]) * 16777619U;
  }
  report->output_checksum = checksum;
  int ok = report->prepare_result == JDR_OK && report->decode_result == JDR_OK &&
           report->width == TEST_WIDTH && report->height == TEST_HEIGHT &&
           report->coverage_pixels == TEST_PIXELS && report->callbacks_ok &&
           report->canaries_ok;
  free(workspace); free(output); free(coverage);
  return ok;
}
