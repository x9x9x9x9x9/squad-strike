/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Replay video export (src/pc/video_export.cpp): an MP4 (H.264 video, AAC
 * audio) from captured frames and the game's mix. Windows encodes with Media
 * Foundation, which every Windows install has; elsewhere the export pipes
 * the frames to an ffmpeg found on the PATH. */
#ifndef PC_VIDEO_EXPORT_H
#define PC_VIDEO_EXPORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether this build can export at all (Windows, or ffmpeg on the PATH). */
bool pc_video_available(void);
/* Starts a file: `width` x `height` at 60 frames a second, audio at
 * `audio_rate` Hz stereo. False (and a log line) when it cannot. */
bool pc_video_open(const char* path, int width, int height, int audio_rate);
/* One frame, in capture order (`index` counts from 0). Any thread. */
void pc_video_frame(
    const uint8_t* pixels, int width, int height, int stride, bool bgra, uint64_t index);
/* Interleaved stereo float samples at the open's audio rate. Game thread. */
void pc_video_audio(const float* samples, int frames);
/* Finishes the file. False when it could not be written. */
bool pc_video_close(void);

#ifdef __cplusplus
}
#endif

#endif
