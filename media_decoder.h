// Interfaces to read and uncompress media (video/image) files.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "image_buffer.h"
#include "interval.h"
#include "unix_system.h"
#include "xy.h"

namespace pivid {

// Static metadata about a media (video/image) file. Unchanged during playback.
// Returned by MediaDecoder::info().
struct MediaFileInfo {
    std::string filename;
    std::string container_type;        // Like "matroska,webm"
    std::string codec_name;            // Like "h264_v4l2m2m"
    std::string pixel_format;          // Like "drm_prime" or "RGBA"
    std::optional<XY<int>> size;       // Frame image size, if known
    std::optional<double> frame_rate;  // Video frames/second, if known
    std::optional<int64_t> bit_rate;   // Compressed video bits/sec, if known
    std::optional<double> duration;    // Length in seconds, if known
};

// Uncompressed frame from a video. (Still images appear as one-frame videos.)
// Returned by MediaDecoder::next_frame().
struct MediaFrame {
    ImageBuffer image;
    Interval time;        // Display seconds since video start
    std::string_view frame_type;  // "B", "I", "P" etc for debugging
    bool is_key_frame = false;    // True if the frame can be seeked to
    bool is_corrupt = false;      // True if the codec had an error
};

// Interface to a media codec to read media (video/image) files.
// Returned by open_media_decoder() for a media file.
// *Externally synchronized*, use from one thread at a time.
class MediaDecoder {
  public:
    virtual ~MediaDecoder() = default;

    // Returns static metadata for the media file.
    virtual MediaFileInfo const& file_info() const = 0;

    // Reset to the key frame preceding the timestamp.
    virtual void seek_before(double) = 0;

    // Returns the next uncompressed frame from the media, or {} at EOF.
    virtual std::optional<MediaFrame> next_frame() = 0;
};

// Opens a media (video/image) file and returns a decoder to access it.
std::unique_ptr<MediaDecoder> open_media_decoder(std::string const& filename);

// Encodes media info as JSON.
void to_json(nlohmann::json&, MediaFileInfo const&);

// Encodes a TIFF blob (suitable for writing to a file) for debugging images.
std::vector<uint8_t> debug_tiff(ImageBuffer const&);

// Debugging descriptions of structures.
std::string debug(MediaFrame const&);
std::string debug(MediaFileInfo const&);

}  // namespace pivid
