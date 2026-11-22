/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Replay video export: see video_export.h.
 *
 * Frames arrive from the GPU readback (aurora_capture_*) on whatever thread
 * finishes them; they are copied into a short queue and written by one
 * encoder thread, in index order. Audio arrives on the game thread at the
 * game's own rate and is converted to 48 kHz 16-bit stereo, the rate AAC
 * encoders take. */
#include "video_export.h"

#include "pc.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#endif

namespace {

constexpr int OutRate = 48000;
constexpr int Fps = 60;
constexpr size_t QueueMax = 12; /* frames waiting for the encoder */

struct Frame {
    uint64_t index;
    std::vector<uint8_t> bgra; /* tightly packed, top row first */
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual bool video(const uint8_t* bgra, uint64_t index) = 0;
    virtual bool audio(const int16_t* samples, size_t frames) = 0;
    virtual bool finish() = 0;
};

#if defined(_WIN32)
/* Media Foundation is loaded at run time, not linked: Windows N editions
 * ship without it until the Media Feature Pack is installed, and a game that
 * will not start for want of a video exporter would be the wrong trade. */
struct MfApi {
    bool tried = false, ok = false;
    HRESULT(WINAPI* Startup)(ULONG, DWORD) = nullptr;
    HRESULT(WINAPI* Shutdown)() = nullptr;
    HRESULT(WINAPI* CreateAttributes)(IMFAttributes**, UINT32) = nullptr;
    HRESULT(WINAPI* CreateMediaType)(IMFMediaType**) = nullptr;
    HRESULT(WINAPI* CreateMemoryBuffer)(DWORD, IMFMediaBuffer**) = nullptr;
    HRESULT(WINAPI* CreateSample)(IMFSample**) = nullptr;
    HRESULT(WINAPI* CreateSinkWriterFromURL)
    (LPCWSTR, IMFByteStream*, IMFAttributes*, IMFSinkWriter**) = nullptr;
} mf;

template <class F>
bool sym(HMODULE m, const char* name, F& out) {
    out = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name)));
    return out != nullptr;
}

bool mf_load() {
    if (mf.tried)
        return mf.ok;
    mf.tried = true;
    HMODULE plat = LoadLibraryW(L"mfplat.dll");
    HMODULE rw = LoadLibraryW(L"mfreadwrite.dll");
    mf.ok = plat != nullptr && rw != nullptr && sym(plat, "MFStartup", mf.Startup) &&
            sym(plat, "MFShutdown", mf.Shutdown) &&
            sym(plat, "MFCreateAttributes", mf.CreateAttributes) &&
            sym(plat, "MFCreateMediaType", mf.CreateMediaType) &&
            sym(plat, "MFCreateMemoryBuffer", mf.CreateMemoryBuffer) &&
            sym(plat, "MFCreateSample", mf.CreateSample) &&
            sym(rw, "MFCreateSinkWriterFromURL", mf.CreateSinkWriterFromURL);
    if (!mf.ok)
        pc_log_line("video: Media Foundation is not available on this Windows");
    return mf.ok;
}

template <class T>
void release(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

/* Media Foundation's sink writer: H.264 + AAC into an MP4. */
class MfBackend final : public Backend {
public:
    MfBackend(int w, int h) : width(w), height(h) {}
    ~MfBackend() override {
        release(writer);
        if (started)
            mf.Shutdown();
        if (com)
            CoUninitialize();
    }
    bool open(const char* path) {
        com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        if (FAILED(mf.Startup(MF_VERSION, MFSTARTUP_FULL)))
            return fail("MFStartup");
        started = true;
        int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
        std::wstring wpath(n > 0 ? n : 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath.data(), n);
        IMFAttributes* attr = nullptr;
        mf.CreateAttributes(&attr, 2);
        if (attr != nullptr) {
            attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
            attr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
        }
        HRESULT hr = mf.CreateSinkWriterFromURL(wpath.c_str(), nullptr, attr, &writer);
        release(attr);
        if (FAILED(hr))
            return fail("MFCreateSinkWriterFromURL", hr);

        IMFMediaType* type = nullptr;
        mf.CreateMediaType(&type);
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        type->SetUINT32(MF_MT_AVG_BITRATE, width >= 1920 ? 16000000 : 10000000);
        type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
        MFSetAttributeSize(type, MF_MT_FRAME_SIZE, width, height);
        MFSetAttributeRatio(type, MF_MT_FRAME_RATE, Fps, 1);
        MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->AddStream(type, &video_stream);
        release(type);
        if (FAILED(hr))
            return fail("AddStream(video)", hr);

        mf.CreateMediaType(&type);
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        type->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(width * 4)); /* top-down */
        MFSetAttributeSize(type, MF_MT_FRAME_SIZE, width, height);
        MFSetAttributeRatio(type, MF_MT_FRAME_RATE, Fps, 1);
        MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->SetInputMediaType(video_stream, type, nullptr);
        release(type);
        if (FAILED(hr))
            return fail("SetInputMediaType(video)", hr);

        mf.CreateMediaType(&type);
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, OutRate);
        type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000); /* 192 kbit/s */
        hr = writer->AddStream(type, &audio_stream);
        release(type);
        if (FAILED(hr))
            return fail("AddStream(audio)", hr);

        mf.CreateMediaType(&type);
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, OutRate);
        type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, OutRate * 4);
        hr = writer->SetInputMediaType(audio_stream, type, nullptr);
        release(type);
        if (FAILED(hr))
            return fail("SetInputMediaType(audio)", hr);

        hr = writer->BeginWriting();
        if (FAILED(hr))
            return fail("BeginWriting", hr);
        return true;
    }
    bool video(const uint8_t* bgra, uint64_t index) override {
        const DWORD size = DWORD(width) * height * 4;
        IMFMediaBuffer* buffer = nullptr;
        if (FAILED(mf.CreateMemoryBuffer(size, &buffer)))
            return false;
        BYTE* dst = nullptr;
        buffer->Lock(&dst, nullptr, nullptr);
        std::memcpy(dst, bgra, size);
        buffer->Unlock();
        buffer->SetCurrentLength(size);
        const LONGLONG t = LONGLONG(index) * 10000000LL / Fps;
        const LONGLONG d = LONGLONG(index + 1) * 10000000LL / Fps - t;
        bool ok = write(video_stream, buffer, t, d);
        release(buffer);
        return ok;
    }
    bool audio(const int16_t* samples, size_t frames) override {
        const DWORD size = DWORD(frames * 4);
        IMFMediaBuffer* buffer = nullptr;
        if (frames == 0 || FAILED(mf.CreateMemoryBuffer(size, &buffer)))
            return frames == 0;
        BYTE* dst = nullptr;
        buffer->Lock(&dst, nullptr, nullptr);
        std::memcpy(dst, samples, size);
        buffer->Unlock();
        buffer->SetCurrentLength(size);
        const LONGLONG t = LONGLONG(audio_frames) * 10000000LL / OutRate;
        audio_frames += frames;
        const LONGLONG d = LONGLONG(audio_frames) * 10000000LL / OutRate - t;
        bool ok = write(audio_stream, buffer, t, d);
        release(buffer);
        return ok;
    }
    bool finish() override {
        HRESULT hr = writer->Finalize();
        release(writer);
        return SUCCEEDED(hr) || fail("Finalize", hr);
    }

private:
    bool write(DWORD stream, IMFMediaBuffer* buffer, LONGLONG t, LONGLONG d) {
        IMFSample* sample = nullptr;
        if (FAILED(mf.CreateSample(&sample)))
            return false;
        sample->AddBuffer(buffer);
        sample->SetSampleTime(t);
        sample->SetSampleDuration(d);
        HRESULT hr = writer->WriteSample(stream, sample);
        release(sample);
        return SUCCEEDED(hr) || fail("WriteSample", hr);
    }
    bool fail(const char* what, HRESULT hr = E_FAIL) {
        pc_log_line("video: %s failed (0x%08x)", what, (uint32_t)hr);
        return false;
    }
    int width, height;
    bool com = false, started = false;
    IMFSinkWriter* writer = nullptr;
    DWORD video_stream = 0, audio_stream = 0;
    uint64_t audio_frames = 0;
};
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) &&                       \
    !(defined(__APPLE__) && TARGET_OS_IPHONE)
#define HAVE_FFMPEG_PIPE 1
/* Elsewhere: frames down a pipe to ffmpeg into a video-only file, the audio
 * into a raw file beside it, and one more ffmpeg run joins the two. */
class FfmpegBackend final : public Backend {
public:
    FfmpegBackend(int w, int h) : width(w), height(h) {}
    ~FfmpegBackend() override {
        if (pipe != nullptr)
            pclose(pipe);
        if (raw != nullptr)
            std::fclose(raw);
    }
    bool open(const char* path) {
        out = path;
        video_tmp = out + ".video.mp4";
        audio_tmp = out + ".audio.raw";
        char cmd[4096];
        std::snprintf(cmd, sizeof cmd,
            "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgra -s %dx%d -r %d -i - "
            "-c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \"%s\"",
            width, height, Fps, video_tmp.c_str());
        pipe = popen(cmd, "w");
        raw = std::fopen(audio_tmp.c_str(), "wb");
        if (pipe == nullptr || raw == nullptr) {
            pc_log_line("video: cannot start ffmpeg");
            return false;
        }
        return true;
    }
    bool video(const uint8_t* bgra, uint64_t) override {
        const size_t size = size_t(width) * height * 4;
        return std::fwrite(bgra, size, 1, pipe) == 1;
    }
    bool audio(const int16_t* samples, size_t frames) override {
        return frames == 0 || std::fwrite(samples, frames * 4, 1, raw) == 1;
    }
    bool finish() override {
        const bool piped = pclose(pipe) == 0;
        pipe = nullptr;
        std::fclose(raw);
        raw = nullptr;
        char cmd[4096];
        std::snprintf(cmd, sizeof cmd,
            "ffmpeg -y -loglevel error -i \"%s\" -f s16le -ar %d -ac 2 -i \"%s\" -c:v copy "
            "-c:a aac -b:a 192k -shortest \"%s\"",
            video_tmp.c_str(), OutRate, audio_tmp.c_str(), out.c_str());
        const bool joined = piped && std::system(cmd) == 0;
        std::remove(video_tmp.c_str());
        std::remove(audio_tmp.c_str());
        return joined;
    }

private:
    int width, height;
    std::string out, video_tmp, audio_tmp;
    FILE* pipe = nullptr;
    FILE* raw = nullptr;
};
#endif

struct Export {
    Backend* backend = nullptr;
    int width = 0, height = 0;
    std::mutex mutex;
    std::condition_variable cv;
    std::map<uint64_t, Frame> pending; /* by index: readbacks may finish out of order */
    std::deque<std::vector<int16_t>> audio;
    uint64_t next = 0;
    bool closing = false, failed = false;
    std::thread worker;
    SDL_AudioStream* convert = nullptr;
    uint64_t frames_written = 0;
} g;

void run() {
    std::unique_lock lock{g.mutex};
    for (;;) {
        g.cv.wait(
            lock, [] { return g.closing || !g.audio.empty() || g.pending.count(g.next) != 0; });
        while (!g.audio.empty()) {
            auto chunk = std::move(g.audio.front());
            g.audio.pop_front();
            lock.unlock();
            if (!g.backend->audio(chunk.data(), chunk.size() / 2))
                g.failed = true;
            lock.lock();
        }
        auto it = g.pending.find(g.next);
        if (it != g.pending.end()) {
            Frame f = std::move(it->second);
            g.pending.erase(it);
            ++g.next;
            g.cv.notify_all();
            lock.unlock();
            if (!g.backend->video(f.bgra.data(), f.index))
                g.failed = true;
            ++g.frames_written;
            lock.lock();
            continue;
        }
        if (g.closing && g.audio.empty())
            return;
    }
}

bool ffmpeg_on_path() {
#if defined(HAVE_FFMPEG_PIPE)
    return std::system("ffmpeg -version >/dev/null 2>&1") == 0;
#else
    return false;
#endif
}

}  // namespace

extern "C" bool pc_video_available(void) {
#if defined(_WIN32)
    return mf_load();
#elif !defined(HAVE_FFMPEG_PIPE)
    return false;
#else
    static int have = -1;
    if (have < 0)
        have = ffmpeg_on_path();
    return have == 1;
#endif
}

extern "C" bool pc_video_open(const char* path, int width, int height, int audio_rate) {
    if (g.backend != nullptr)
        return false;
    Backend* backend = nullptr;
#if defined(_WIN32)
    if (!mf_load())
        return false;
    auto* writer = new MfBackend(width, height);
    if (writer->open(path))
        backend = writer;
    else
        delete writer;
#elif defined(HAVE_FFMPEG_PIPE)
    auto* ff = new FfmpegBackend(width, height);
    if (ff->open(path))
        backend = ff;
    else
        delete ff;
#else
    (void)path;
#endif
    if (backend == nullptr)
        return false;
    const SDL_AudioSpec in{SDL_AUDIO_F32, 2, audio_rate};
    const SDL_AudioSpec out{SDL_AUDIO_S16, 2, OutRate};
    g.convert = SDL_CreateAudioStream(&in, &out);
    g.backend = backend;
    g.width = width;
    g.height = height;
    g.next = 0;
    g.closing = false;
    g.failed = false;
    g.frames_written = 0;
    g.worker = std::thread(run);
    pc_log_line("video: writing %s (%dx%d)", path, width, height);
    return true;
}

extern "C" void pc_video_frame(
    const uint8_t* pixels, int width, int height, int stride, bool bgra, uint64_t index) {
    if (g.backend == nullptr || width != g.width || height != g.height)
        return;
    Frame f;
    f.index = index;
    f.bgra.resize(size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = pixels + size_t(y) * stride;
        uint8_t* dst = f.bgra.data() + size_t(y) * width * 4;
        if (bgra) {
            std::memcpy(dst, src, size_t(width) * 4);
        } else {
            for (int x = 0; x < width; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = 255;
            }
        }
    }
    std::unique_lock lock{g.mutex};
    /* Bounded: the readback waits for the encoder rather than piling up
     * frames of 8 MB each. */
    g.cv.wait(lock, [&] { return g.pending.size() < QueueMax || index <= g.next; });
    g.pending.emplace(index, std::move(f));
    g.cv.notify_all();
}

extern "C" void pc_video_audio(const float* samples, int frames) {
    if (g.backend == nullptr || g.convert == nullptr || frames <= 0)
        return;
    SDL_PutAudioStreamData(g.convert, samples, frames * 2 * int(sizeof(float)));
    const int avail = SDL_GetAudioStreamAvailable(g.convert);
    if (avail < 4)
        return;
    std::vector<int16_t> chunk(size_t(avail / 4) * 2);
    const int got = SDL_GetAudioStreamData(g.convert, chunk.data(), int(chunk.size() * 2));
    if (got <= 0)
        return;
    chunk.resize(size_t(got) / 2);
    std::lock_guard lock{g.mutex};
    g.audio.push_back(std::move(chunk));
    g.cv.notify_all();
}

extern "C" bool pc_video_close(void) {
    if (g.backend == nullptr)
        return false;
    if (g.convert != nullptr) {
        SDL_FlushAudioStream(g.convert);
        pc_video_audio(nullptr, 0);
        const int avail = SDL_GetAudioStreamAvailable(g.convert);
        if (avail >= 4) {
            std::vector<int16_t> chunk(size_t(avail / 4) * 2);
            const int got = SDL_GetAudioStreamData(g.convert, chunk.data(), int(chunk.size() * 2));
            if (got > 0) {
                chunk.resize(size_t(got) / 2);
                std::lock_guard lock{g.mutex};
                g.audio.push_back(std::move(chunk));
            }
        }
    }
    {
        std::lock_guard lock{g.mutex};
        g.closing = true;
        g.cv.notify_all();
    }
    g.worker.join();
    g.pending.clear();
    const bool ok = g.backend->finish() && !g.failed;
    delete g.backend;
    g.backend = nullptr;
    if (g.convert != nullptr) {
        SDL_DestroyAudioStream(g.convert);
        g.convert = nullptr;
    }
    pc_log_line(
        "video: %s, %llu frames", ok ? "finished" : "FAILED", (unsigned long long)g.frames_written);
    return ok;
}
