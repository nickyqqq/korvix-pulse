#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include "clk_tjpgd.h"
#include <esp_partition.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#ifndef CUSTOM_ANIMATION_PARTITION_LABEL
#define CUSTOM_ANIMATION_PARTITION_LABEL "spiffs"
#endif
#ifndef CUSTOM_ANIMATION_MOUNT_POINT
#define CUSTOM_ANIMATION_MOUNT_POINT "/littlefs"
#endif

// One custom animation (the customer's GIF, converted to JPEG frames by the
// phone page), stored in the 896 KiB LittleFS data partition of the Huge APP
// scheme. Call from the Arduino loop / synchronous WebServer callbacks;
// concurrent calls from different FreeRTOS tasks are not supported.
//
// Upload format (all integers little-endian):
//   0..7  "CLKJPG01"; 8..9 width; 10..11 height: 240x180 (current phone page,
//   drawn smoothly through a RAM frame buffer) or 320x240 (first release);
//   12..13 frame count 1..150; 14..15 frames per second 5..20.
// Then, for each frame: uint32 JPEG byte count followed by that baseline JPEG.
// Exact EOF must follow the last frame. Maximum file 720 KiB / frame 16 KiB.
//
// To give the GIF as much room as possible, a new upload first deletes the
// previous GIF (the built-in day/sunset/night scenes play meanwhile). Upload
// data is fsync'ed and every frame fully decoded before it is renamed into
// place, so a failed or interrupted upload never leaves a half-written GIF.
class CustomAnimation {
 public:
  static constexpr uint32_t MAX_FILE_BYTES = 512U * 1024U;  // per GIF; several fit
  static constexpr int MAX_GIFS = 32;
  static constexpr uint32_t MAX_JPEG_BYTES = 16384U;
  static constexpr uint16_t MAX_FRAMES = 300;
  static constexpr uint16_t MIN_FPS = 5, MAX_FPS = 30;
  static constexpr uint16_t WIDTH = 320, HEIGHT = 240;          // full screen
  static constexpr uint16_t SMALL_WIDTH = 240, SMALL_HEIGHT = 180; // scaled up 4:3 on screen
  static constexpr uint32_t PARTITION_BYTES = 896U * 1024U;

  CustomAnimation() = default;
  CustomAnimation(const CustomAnimation &) = delete;
  CustomAnimation &operator=(const CustomAnimation &) = delete;
  ~CustomAnimation() {
    if (uploadFd_ >= 0) ::close(uploadFd_);
    if (playFd_ >= 0) ::close(playFd_);
    free(frameBuffer_);
  }

  struct GifInfo { uint32_t id; uint32_t bytes; uint16_t frames, fps; };

  // Mounts storage, moves a GIF saved by an older build into the library, and
  // opens `preferredId` (or the newest GIF when that one is gone).
  bool begin(uint32_t preferredId = 0) {
    if (mounted_) return true;
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        CUSTOM_ANIMATION_PARTITION_LABEL);
    if (!partition || partition->size != PARTITION_BYTES)
      return fail("Custom storage needs the 896 KiB data partition.");
    // This partition only ever holds the customer's GIFs. A new board, or one
    // that previously ran other firmware, is formatted once on first start.
    if (!LittleFS.begin(true, CUSTOM_ANIMATION_MOUNT_POINT, 4,
                        CUSTOM_ANIMATION_PARTITION_LABEL))
      return fail("GIF storage could not start.");
    mounted_ = true;
    error_ = "";
    ::mkdir(CUSTOM_ANIMATION_MOUNT_POINT "/gifs", 0700);
    unlinkIfPresent(tempPath());
    struct stat info;
    char path[48];
    if (::stat(legacyPath(), &info) == 0) {  // single GIF from the previous version
      gifPath(newestId() + 1, path);
      ::rename(legacyPath(), path);
    }
    if (!(preferredId && select(preferredId))) {
      const uint32_t newest = newestId();
      if (newest) select(newest);
    }
    return true;
  }

  // GIFs in the library, oldest first. Returns how many were written to `out`.
  int list(GifInfo *out, int max) {
    uint32_t ids[MAX_GIFS];
    const int count = listIds(ids, MAX_GIFS);
    int written = 0;
    for (int i = 0; i < count && written < max; ++i) {
      char path[48]; gifPath(ids[i], path);
      const int fd = ::open(path, O_RDONLY);
      if (fd < 0) continue;
      struct stat info; uint8_t header[16];
      if (::fstat(fd, &info) == 0 && readExact(fd, header, sizeof(header)) && !memcmp(header, "CLKJPG01", 8))
        out[written++] = {ids[i], uint32_t(info.st_size), le16(header + 12), le16(header + 14)};
      ::close(fd);
    }
    return written;
  }

  // Makes `id` the GIF that plays. Files were fully checked when saved, so this
  // only builds the frame index (fast).
  bool select(uint32_t id) {
    if (!mounted_) return fail("GIF storage is not ready.");
    char path[48]; gifPath(id, path);
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) return fail("That GIF is no longer on the clock.");
    FrameLocation *candidate = scratchIndex_;
    uint16_t count = 0, w = 0, h = 0, fps = 0;
    const bool ok = indexFile(fd, candidate, count, w, h, fps);
    ::close(fd);
    if (!ok) return fail("That GIF could not be opened.");
    closePlayback();
    memcpy(frames_, candidate, sizeof(FrameLocation) * count);
    frameCount_ = count; width_ = w; height_ = h; fps_ = fps;
    activeId_ = id; strcpy(activePath_, path); valid_ = true; error_ = "";
    return true;
  }

  // Starts saving a new GIF of `size` bytes. Removes the oldest GIFs until it fits.
  bool beginUpload(uint32_t size) {
    if (!mounted_ && !begin()) return false;
    if (uploadFd_ >= 0) return fail("A GIF upload is already active.");
    if (size == 0 || size > MAX_FILE_BYTES) return fail("That GIF is too big for the clock.");
    if (!unlinkIfPresent(tempPath())) return fail("Could not clear the upload file.");
    if (!ensureBuffer()) return false;
    const size_t needed = ((size + 4095U) & ~4095U) + 24576U;  // whole blocks + room to work
    evicted_ = 0;
    for (;;) {
      uint32_t ids[MAX_GIFS];
      const int count = listIds(ids, MAX_GIFS);
      if (freeBytes() >= needed && count < MAX_GIFS) break;
      if (!count) return fail("Not enough space for the GIF.");
      if (!removeGif(ids[0])) return false;  // oldest first
      ++evicted_;
    }
    uploadFd_ = ::open(tempPath(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (uploadFd_ < 0) return fail("Could not start the GIF upload.");
    uploadedBytes_ = 0;
    error_ = "";
    return true;
  }

  bool writeUpload(const uint8_t *data, size_t size) {
    if (uploadFd_ < 0) return fail("No GIF upload is active.");
    if ((!data && size) || size > MAX_FILE_BYTES - uploadedBytes_) {
      abortUpload();
      return fail("That GIF is too big for the clock.");
    }
    size_t done = 0;
    while (done < size) {
      const ssize_t count = ::write(uploadFd_, data + done, size - done);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) {
        abortUpload();
        return fail("Saving the GIF failed. Please try again.");
      }
      done += static_cast<size_t>(count);
    }
    uploadedBytes_ += static_cast<uint32_t>(size);
    return true;
  }

  // Checks every frame, adds the GIF to the library and makes it the one playing.
  bool finishUpload() {
    if (uploadFd_ < 0) return fail("No GIF upload is active.");
    const int fd = uploadFd_;
    uploadFd_ = -1;
    const bool synced = ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    if (!synced || !closed) {
      unlinkIfPresent(tempPath());
      return fail("Saving the GIF failed. Please try again.");
    }
    FrameLocation *candidate = scratchIndex_;
    uint16_t count = 0;
    uint16_t w = 0, h = 0;
    if (!validateFile(tempPath(), candidate, count, w, h)) {
      unlinkIfPresent(tempPath());
      return false;
    }
    char path[48];
    const uint32_t id = newestId() + 1;
    gifPath(id, path);
    if (::rename(tempPath(), path) != 0) {
      unlinkIfPresent(tempPath());
      return fail("Saving the GIF failed. Please try again.");
    }
    uploadedBytes_ = 0;
    return select(id);
  }

  void abortUpload() {
    if (uploadFd_ >= 0) { ::close(uploadFd_); uploadFd_ = -1; }
    if (mounted_) unlinkIfPresent(tempPath());
    uploadedBytes_ = 0;
  }

  bool removeGif(uint32_t id) {
    char path[48]; gifPath(id, path);
    if (id == activeId_) { closePlayback(); valid_ = false; frameCount_ = 0; activeId_ = 0; }
    return unlinkIfPresent(path) || fail("Could not delete that GIF.");
  }

  // Factory reset: removes every GIF (not the whole filesystem).
  bool removeAll() {
    if (!mounted_ && !begin()) return false;
    abortUpload();
    uint32_t ids[MAX_GIFS];
    bool ok = true;
    for (int i = 0, n = listIds(ids, MAX_GIFS); i < n; ++i) ok &= removeGif(ids[i]);
    unlinkIfPresent(legacyPath());
    return ok || fail("Could not delete all GIFs.");
  }

  // Copies the first frame of a GIF (a normal JPEG) for the phone's thumbnails.
  bool readThumbnail(uint32_t id, uint8_t *&jpeg, size_t &length) {
    char path[48]; gifPath(id, path);
    if (!ensureBuffer()) return false;
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) return fail("That GIF is no longer on the clock.");
    uint8_t header[20];
    bool ok = readExact(fd, header, sizeof(header)) && !memcmp(header, "CLKJPG01", 8);
    const uint32_t size = ok ? le32(header + 16) : 0;
    ok = ok && size > 0 && size <= MAX_JPEG_BYTES && readExact(fd, frameBuffer_, size);
    ::close(fd);
    if (!ok) return fail("That GIF could not be opened.");
    jpeg = frameBuffer_; length = size;
    return true;
  }

  size_t freeBytes() const {
    const size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
    return used < total ? total - used : 0;
  }
  size_t totalBytes() const { return LittleFS.totalBytes(); }
  uint32_t activeId() const { return valid_ ? activeId_ : 0; }
  int evictedOnLastUpload() const { return evicted_; }
  bool exists() const { return valid_; }
  bool ready() const { return mounted_; }
  bool uploading() const { return uploadFd_ >= 0; }
  uint16_t frameCount() const { return valid_ ? frameCount_ : 0; }
  uint16_t fps() const { return fps_; }
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  const char *lastError() const { return error_; }

  // TJpgDec must already have the application's output callback, scale=1, and
  // desired byte-swap setting. The shared decoder configuration is untouched.
  // Fast path for 240x180 frames: decodes straight into `out` (width x height
  // RGB565 pixels) with the speed-tuned private decoder. `onBlock` runs after
  // each 16x16 block; returning false stops early (e.g. the user tapped).
  bool decodeFrame(uint16_t frame, uint16_t *out, bool (*onBlock)()) {
    if (!valid_ || frame >= frameCount_) return fail("Custom animation frame is unavailable.");
    if (!ensureBuffer()) return false;
    if (playFd_ < 0) playFd_ = ::open(activePath_, O_RDONLY);
    const FrameLocation &location = frames_[frame];
    if (playFd_ < 0 || ::lseek(playFd_, location.offset, SEEK_SET) < 0 ||
        !readExact(playFd_, frameBuffer_, location.size)) {
      closePlayback();
      return fail("Could not read the custom animation frame.");
    }
    const int result = decodeJpeg(frameBuffer_, location.size, out, width_, height_, onBlock);
    if (result < 0) return false;  // interrupted
    return result > 0 || fail("The custom animation frame could not be displayed.");
  }

  // Decodes one JPEG of exactly width x height into `out` with the fast decoder.
  // Returns 1 = done, 0 = bad JPEG, -1 = stopped by onBlock.
  static int decodeJpeg(const uint8_t *jpeg, size_t size, uint16_t *out, uint16_t width, uint16_t height,
                        bool (*onBlock)()) {
    static uint8_t workspace[11 * 1024] __attribute__((aligned(4)));
    FastInput input{jpeg, size, 0, out, width, height, onBlock};
    CLK_JDEC decoder{};  // zeroed: this decoder keeps its byte-swap flag from the struct
    decoder.swap = 0;
    if (clk_jd_prepare(&decoder, fastRead, workspace, sizeof(workspace), &input) != CLK_JDR_OK ||
        decoder.width != width || decoder.height != height) return 0;
    const CLK_JRESULT result = clk_jd_decomp(&decoder, fastOutput, 0);
    return result == CLK_JDR_OK ? 1 : result == CLK_JDR_INTR ? -1 : 0;
  }

  bool renderFrame(uint16_t frame, int16_t x = 0, int16_t y = 0) {
    if (!valid_ || frame >= frameCount_) return fail("Custom animation frame is unavailable.");
    if (!ensureBuffer()) return false;
    if (playFd_ < 0) playFd_ = ::open(activePath_, O_RDONLY);
    const FrameLocation &location = frames_[frame];
    if (playFd_ < 0 || ::lseek(playFd_, location.offset, SEEK_SET) < 0 ||
        !readExact(playFd_, frameBuffer_, location.size)) {
      closePlayback();
      return fail("Could not read the custom animation frame.");
    }
    if (TJpgDec.drawJpg(x, y, frameBuffer_, location.size) != JDR_OK)
      return fail("The custom animation frame could not be displayed.");
    return true;
  }

 private:
  struct FrameLocation { uint32_t offset; uint32_t size; };
  struct FastInput {
    const uint8_t *bytes; size_t size, position;
    uint16_t *out; uint16_t width, height; bool (*onBlock)();
  };
  static size_t fastRead(CLK_JDEC *decoder, uint8_t *destination, size_t requested) {
    FastInput &input = *static_cast<FastInput *>(decoder->device);
    size_t count = input.size - input.position;
    if (count > requested) count = requested;
    if (destination) memcpy(destination, input.bytes + input.position, count);
    input.position += count;
    return count;
  }
  static int fastOutput(CLK_JDEC *decoder, void *bitmap, CLK_JRECT *rect) {
    FastInput &input = *static_cast<FastInput *>(decoder->device);
    if (rect->right >= input.width || rect->bottom >= input.height) return 0;
    const int w = rect->right - rect->left + 1;
    const uint16_t *source = static_cast<const uint16_t *>(bitmap);
    for (int y = rect->top; y <= rect->bottom; ++y, source += w)
      memcpy(input.out + y * input.width + rect->left, source, w * 2);
    return !input.onBlock || input.onBlock();
  }
  struct DecodeInput {
    const uint8_t *bytes;
    size_t size, position;
    uint32_t pixels;
    uint16_t width, height;
  };
  FrameLocation frames_[MAX_FRAMES]{};
  uint8_t *frameBuffer_ = nullptr;
 public:
  // Called when memory runs out; the clock frees what it can spare (its smooth-drawing buffer).
  void (*lowMemory)() = nullptr;
  // Takes the memory GIFs need (one frame + the checker's workspace) once at start-up, before
  // Wi-Fi breaks the free memory into small pieces; kept for good, so uploads always have it.
  bool reserveMemory() {
    if (!verifyWork_) verifyWork_ = malloc(TJPGD_WORKSPACE_SIZE);
    return ensureBuffer() && verifyWork_ != nullptr;
  }
 private:
  void *verifyWork_ = nullptr;
 private:
  int uploadFd_ = -1, playFd_ = -1;
  uint32_t uploadedBytes_ = 0;
  uint16_t frameCount_ = 0, width_ = WIDTH, height_ = HEIGHT, fps_ = 10;
  uint16_t candidateFps_ = 10;
  bool mounted_ = false, valid_ = false;
  const char *error_ = "";

  static inline FrameLocation scratchIndex_[MAX_FRAMES]{};  // shared by select() and finishUpload()
  uint32_t activeId_ = 0;
  char activePath_[48] = "";
  int evicted_ = 0;
  static const char *legacyPath() { return CUSTOM_ANIMATION_MOUNT_POINT "/custom.clkj"; }
  static void gifPath(uint32_t id, char *out) {
    snprintf(out, 48, CUSTOM_ANIMATION_MOUNT_POINT "/gifs/g%08lu.clkj", static_cast<unsigned long>(id));
  }
  // Library ids, ascending (= oldest first).
  static int listIds(uint32_t *ids, int max) {
    DIR *dir = ::opendir(CUSTOM_ANIMATION_MOUNT_POINT "/gifs");
    if (!dir) return 0;
    int count = 0;
    while (struct dirent *entry = ::readdir(dir)) {
      unsigned long id = 0;
      if (sscanf(entry->d_name, "g%08lu.clkj", &id) != 1 || !id) continue;
      if (count >= max) continue;  // never happens: uploads keep the library below MAX_GIFS
      ids[count++] = id;
      for (int i = count - 1; i > 0 && ids[i] < ids[i-1]; --i) { const uint32_t t = ids[i]; ids[i] = ids[i-1]; ids[i-1] = t; }
    }
    ::closedir(dir);
    return count;
  }
  static uint32_t newestId() { uint32_t ids[MAX_GIFS]; const int n = listIds(ids, MAX_GIFS); return n ? ids[n-1] : 0; }
  // Frame index for an already-checked file: follows the length fields only.
  bool indexFile(int fd, FrameLocation *index, uint16_t &count, uint16_t &w, uint16_t &h, uint16_t &fps) {
    struct stat info; uint8_t header[16];
    if (::fstat(fd, &info) != 0 || !readExact(fd, header, sizeof(header)) || memcmp(header, "CLKJPG01", 8)) return false;
    w = le16(header + 8); h = le16(header + 10); count = le16(header + 12); fps = le16(header + 14);
    if (!((w == WIDTH && h == HEIGHT) || (w == SMALL_WIDTH && h == SMALL_HEIGHT)) || !count ||
        count > MAX_FRAMES || fps < MIN_FPS || fps > MAX_FPS) return false;
    uint32_t offset = sizeof(header);
    const uint32_t total = info.st_size;
    for (uint16_t i = 0; i < count; ++i) {
      uint8_t lengthBytes[4];
      if (total - offset < 4 || ::lseek(fd, offset, SEEK_SET) < 0 || !readExact(fd, lengthBytes, 4)) return false;
      const uint32_t length = le32(lengthBytes);
      offset += 4;
      if (!length || length > MAX_JPEG_BYTES || length > total - offset) return false;
      index[i] = {offset, length};
      offset += length;
    }
    return offset == total;
  }
  static const char *tempPath() { return CUSTOM_ANIMATION_MOUNT_POINT "/custom-upload.tmp"; }
  bool fail(const char *message) { error_ = message; return false; }
  void closePlayback() {
    if (playFd_ >= 0) { ::close(playFd_); playFd_ = -1; }
  }
  bool ensureBuffer() {
    if (!frameBuffer_) frameBuffer_ = static_cast<uint8_t *>(malloc(MAX_JPEG_BYTES));
    if (!frameBuffer_ && lowMemory) { lowMemory(); frameBuffer_ = static_cast<uint8_t *>(malloc(MAX_JPEG_BYTES)); }
    return frameBuffer_ != nullptr || fail("Not enough memory for a custom animation frame.");
  }
  static bool unlinkIfPresent(const char *path) {
    return ::unlink(path) == 0 || errno == ENOENT;
  }
  static uint16_t le16(const uint8_t *data) { return uint16_t(data[0]) | (uint16_t(data[1]) << 8); }
  static uint32_t le32(const uint8_t *data) {
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
           (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
  }
  static bool readExact(int fd, uint8_t *destination, size_t size) {
    size_t done = 0;
    while (done < size) {
      const ssize_t count = ::read(fd, destination + done, size - done);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) return false;
      done += static_cast<size_t>(count);
    }
    return true;
  }
  // Reject progressive/extended/arithmetic JPEGs before giving the exact-size
  // input to TJpgDec. Full entropy decoding below additionally checks support.
  static bool baselineHeader(const uint8_t *data, size_t size, uint16_t width, uint16_t height) {
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8 ||
        data[size - 2] != 0xFF || data[size - 1] != 0xD9) return false;
    size_t position = 2;
    bool foundFrame = false;
    while (position < size - 2) {
      if (data[position++] != 0xFF) return false;
      while (position < size && data[position] == 0xFF) ++position;
      if (position >= size) return false;
      const uint8_t marker = data[position++];
      if (marker == 0 || marker == 0xD8 || marker == 0xD9 || marker == 0x01 ||
          (marker >= 0xD0 && marker <= 0xD7) || position + 2 > size) return false;
      const size_t length = (size_t(data[position]) << 8) | data[position + 1];
      if (length < 2 || length > size - position) return false;
      const uint8_t *segment = data + position + 2;
      if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
          marker != 0xC8 && marker != 0xCC) {
        if (marker != 0xC0 || foundFrame || length < 11 || segment[0] != 8 ||
            ((uint16_t(segment[1]) << 8) | segment[2]) != height ||
            ((uint16_t(segment[3]) << 8) | segment[4]) != width ||
            (segment[5] != 1 && segment[5] != 3) ||
            length != 8U + 3U * segment[5]) return false;
        foundFrame = true;
      }
      if (marker == 0xCC) return false; // Arithmetic conditioning is unsupported.
      if (marker == 0xDA) return foundFrame;
      position += length;
    }
    return false;
  }
  static size_t decoderRead(JDEC *decoder, uint8_t *destination, size_t requested) {
    DecodeInput &input = *static_cast<DecodeInput *>(decoder->device);
    size_t count = input.size - input.position;
    if (count > requested) count = requested;
    if (destination) memcpy(destination, input.bytes + input.position, count);
    input.position += count;
    return count;
  }
  static int decoderDiscard(JDEC *decoder, void *bitmap, JRECT *rect) {
    DecodeInput &input = *static_cast<DecodeInput *>(decoder->device);
    if (!bitmap || rect->left > rect->right || rect->top > rect->bottom ||
        rect->right >= input.width || rect->bottom >= input.height) return 0;
    input.pixels += uint32_t(rect->right - rect->left + 1) * (rect->bottom - rect->top + 1);
    return input.pixels <= uint32_t(input.width) * input.height;
  }
  bool validateFile(const char *path, FrameLocation *index, uint16_t &count,
                    uint16_t &width, uint16_t &height) {
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) return fail("Could not read the uploaded animation.");
    struct stat info;
    uint8_t header[16];
    bool ok = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_size >= 20 && uint64_t(info.st_size) <= MAX_FILE_BYTES &&
              readExact(fd, header, sizeof(header));
    const uint16_t w = ok ? le16(header + 8) : 0, h = ok ? le16(header + 10) : 0;
    const bool sizeOk = (w == WIDTH && h == HEIGHT) || (w == SMALL_WIDTH && h == SMALL_HEIGHT);
    if (!ok || memcmp(header, "CLKJPG01", 8) != 0 || !sizeOk || le16(header + 12) == 0 ||
        le16(header + 12) > MAX_FRAMES || le16(header + 14) < MIN_FPS || le16(header + 14) > MAX_FPS) {
      ::close(fd);
      return fail("Invalid animation header, dimensions, frame count, or frame rate.");
    }
    const uint32_t total = static_cast<uint32_t>(info.st_size);
    const uint16_t candidateCount = le16(header + 12);
    void *workspace = verifyWork_ ? verifyWork_ : malloc(TJPGD_WORKSPACE_SIZE);
    if (!workspace && lowMemory) { lowMemory(); workspace = malloc(TJPGD_WORKSPACE_SIZE); }
    if (!workspace) { ::close(fd); return fail("Not enough memory to verify the animation."); }
    uint32_t offset = sizeof(header);
    const char *failure = "Invalid or truncated animation frame data.";
    for (uint16_t frame = 0; frame < candidateCount && ok; ++frame) {
      uint8_t lengthBytes[4];
      if (offset > total || total - offset < 4 || !readExact(fd, lengthBytes, 4)) { ok = false; break; }
      offset += 4;
      const uint32_t length = le32(lengthBytes);
      if (length == 0 || length > MAX_JPEG_BYTES || length > total - offset ||
          !readExact(fd, frameBuffer_, length) || !baselineHeader(frameBuffer_, length, w, h)) {
        ok = false; break;
      }
      DecodeInput input{frameBuffer_, length, 0, 0, w, h};
      JDEC decoder{};
      const JRESULT prepared = jd_prepare(&decoder, decoderRead, workspace, TJPGD_WORKSPACE_SIZE, &input);
      if (prepared != JDR_OK || decoder.width != w || decoder.height != h ||
          jd_decomp(&decoder, decoderDiscard, 0) != JDR_OK ||
          input.pixels != uint32_t(w) * h) {
        failure = "A JPEG frame cannot be decoded by this clock.";
        ok = false; break;
      }
      index[frame] = {offset, length};
      offset += length;
      yield();
    }
    if (ok && offset != total) { ok = false; failure = "Unexpected data after the final animation frame."; }
    if (workspace != verifyWork_) free(workspace);
    ::close(fd);
    if (!ok) return fail(failure);
    count = candidateCount; width = w; height = h; candidateFps_ = le16(header + 14);
    return true;
  }
};
