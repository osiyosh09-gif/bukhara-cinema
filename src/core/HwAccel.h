#pragma once

#include <QString>
#include <QStringList>

extern "C" {
struct AVCodecContext;
struct AVCodec;
struct AVFrame;
}

namespace bc::HwAccel {

// Hardware video decoding (the video card decodes H.264/HEVC/... instead of
// the processor). Used by playback, render preview and export (MLT's
// avformat producer, through MLT_AVFORMAT_HWACCEL) and by proxy making.
// Whenever the card cannot decode a file, FFmpeg falls back to the processor.
// Effects, colour and titles are still computed on the processor.

// The setting (QSettings "gpu/decode"): "auto" (default), "off", or a device
// type name ("d3d11va", "dxva2", "cuda", "vaapi") to force one.
QString setting();
void setSetting(const QString &value);

// Device types worth trying on this system, best first
// (Windows: d3d11va, dxva2, cuda; Linux: vaapi, cuda).
QStringList candidates();

// The candidates that FFmpeg was built with and whose device can be opened
// now (checked once per run, the result is kept).
QStringList available();

// The device type to decode with under the current setting: empty when off
// or when no device works.
QString decoder();

// Tells MLT which device to use for files opened from now on (sets or clears
// MLT_AVFORMAT_HWACCEL). Engine::init calls it; the settings dialog too.
void applyToEngine();

// The names of the hardware H.264 encoders the export can use.
QStringList encoders();

// For own FFmpeg decoders: opens the device `type` on `ctx` before
// avcodec_open2. Returns false (and leaves ctx unchanged) when the codec or
// the device does not support it.
bool attach(AVCodecContext *ctx, const AVCodec *codec, const QString &type);

// If `frame` is in video card memory, copies it into `sw` (normal memory,
// time stamps kept) and returns sw; otherwise returns frame. nullptr on error.
AVFrame *download(AVFrame *frame, AVFrame *sw);

} // namespace bc::HwAccel
