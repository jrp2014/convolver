// SPDX-License-Identifier: GPL-2.0-or-later
// Portable boundary scaffolding. The production helper bodies are inserted.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <strings.h>
#define __int64 long long
#include "libsndfile/sndfile.h"
#undef __int64
#define LIBSNDFILE
#define TEXT(s) s
#define CT2CA(s) (s)
#define _tcsncicmp strncasecmp
typedef char TCHAR;
typedef uint32_t DWORD;
const int MAX_PATH = 260;
static std::ostringstream cdebug;
struct SNDFILE_tag { int fd; };
static SNDFILE handle;
static int opened = 0, closed = 0, synced = 0, last_mode = 0, last_ownership = -1;
static bool valid_format = true, fail_open = false;
static int stream_error = 0, close_status = 0, sync_status = 0;
static SF_INFO last_info;
static std::string last_path;
extern "C" {
int sf_format_check(const SF_INFO*) { return valid_format; }
SNDFILE* sf_open_fd(int fd, int mode, SF_INFO* info, int close_desc) {
    ++opened; handle.fd = fd; last_mode = mode; last_ownership = close_desc; last_info = *info;
    return fail_open ? NULL : &handle;
}
SNDFILE* sf_open(const char* path, int mode, SF_INFO* info) {
    last_path = path; ++opened; last_mode = mode; last_info = *info;
    return fail_open ? NULL : &handle;
}
void sf_write_sync(SNDFILE* file) {
    if (file != &handle) throw std::runtime_error("invalid sync handle");
    ++synced;
    if (sync_status) stream_error = sync_status;
}
int sf_close(SNDFILE* file) {
    if (file != &handle) throw std::runtime_error("invalid close handle");
    ++closed; return close_status;
}
int sf_error(SNDFILE* file) {
    if (file != &handle) throw std::runtime_error("invalid error handle");
    return stream_error;
}
const char* sf_strerror(SNDFILE*) { return "test open error"; }
const char* sf_error_number(int) { return "test close error"; }
int sf_command(SNDFILE*, int, void* data, int size) {
    if (size > 0) static_cast<char*>(data)[0] = '\0';
    return 0;
}
}

// INSERT_SOURCE

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static SF_INFO info(int format) {
    SF_INFO result = {};
    result.format = format; result.channels = 2; result.samplerate = 48000;
    return result;
}
static void descriptor_case(const std::string& name) {
    if (name == "fd_raw") {
        SF_INFO value = info(SF_FORMAT_RAW | SF_FORMAT_DOUBLE | SF_ENDIAN_BIG);
        { CWaveFileHandle file(7, SFM_READ, &value); }
        check(last_info.format == (SF_FORMAT_RAW | SF_FORMAT_DOUBLE | SF_ENDIAN_BIG), "raw format must retain subtype/endian");
        check(last_info.channels == 2 && last_info.samplerate == 48000, "raw channel/rate preserved");
        check(last_mode == SFM_READ && handle.fd == 7, "descriptor and read mode forwarded");
    } else if (name == "fd_autodetect") {
        // MAT4 shares the RAW bit but is not RAW. The historical bit-presence
        // test incorrectly preserves it instead of asking libsndfile to detect.
        const int formats[] = {SF_FORMAT_WAV | SF_FORMAT_FLOAT, SF_FORMAT_MAT4 | SF_FORMAT_FLOAT, SF_FORMAT_PAF | SF_FORMAT_PCM_16, 0};
        for (unsigned i = 0; i != 4; ++i) {
            SF_INFO value = info(formats[i]);
            { CWaveFileHandle file(8, SFM_READ, &value); }
            check(last_info.format == 0, "non-RAW input must request autodetection");
        }
    } else if (name == "fd_write") {
        SF_INFO value = info(SF_FORMAT_WAV | SF_FORMAT_PCM_24);
        { CWaveFileHandle file(1, SFM_WRITE, &value); }
        check(last_info.format == value.format && last_mode == SFM_WRITE, "write format/mode preserved");
        check(handle.fd == 1, "stdout descriptor forwarded");
    }
    check(last_ownership == SF_FALSE, "caller retains descriptor ownership");
    check(opened == closed && closed == synced && opened > 0, "successful handles synced and closed once");
}
static void error_case(bool invalid) {
    SF_INFO value = info(SF_FORMAT_WAV | SF_FORMAT_FLOAT);
    valid_format = !invalid; fail_open = !invalid;
    try { CWaveFileHandle file(2, SFM_WRITE, &value); }
    catch (const wavfileException& error) {
        const std::string message = error.what();
        check(message.find("2:stderr") != std::string::npos, "error identifies standard descriptor");
        check(message.find(invalid ? "Invalid output format specified" : "test open error") != std::string::npos, "error reports specific cause");
        check(opened == (invalid ? 0 : 1), "invalid output rejected before open");
        check(closed == 0 && synced == 0, "failed construction must not close caller descriptor");
        if (!invalid) check(last_ownership == SF_FALSE, "failed open retains descriptor ownership");
        return;
    }
    throw std::runtime_error("expected wavfileException");
}
static void diagnostics_case() {
    const int descriptors[] = {0, 1, 2, 17, -1};
    const char* labels[] = {"0:stdin", "1:stdout", "2:stderr", "17", "-1"};
    for (unsigned i = 0; i != 5; ++i) {
        wavfileException error("reason", descriptors[i], "detail");
        check(error.what() == std::string("Problem with file descriptor (") + labels[i] + "): reason detail", "descriptor diagnostic");
    }
    wavfileException named("reason", "sample.wav", "detail");
    check(std::string(named.what()) == "Problem with sound file (sample.wav): reason detail", "named-file diagnostic preserved");
}
struct Entry { const char* name; unsigned mask; };
static void format_case() {
    // Expectations use the bundled libsndfile API constants, independently of
    // the production table's literal numbers; cover every historical identifier.
    const Entry majors[] = {{"WAV", SF_FORMAT_WAV}, {"AIFF", SF_FORMAT_AIFF}, {"AU", SF_FORMAT_AU}, {"RAW", SF_FORMAT_RAW}, {"PAF", SF_FORMAT_PAF}, {"SVX", SF_FORMAT_SVX}, {"NIST", SF_FORMAT_NIST}, {"VOC", SF_FORMAT_VOC}, {"IRCAM", SF_FORMAT_IRCAM}, {"W64", SF_FORMAT_W64}, {"MAT4", SF_FORMAT_MAT4}, {"MAT5", SF_FORMAT_MAT5}, {"PVF", SF_FORMAT_PVF}, {"XI", SF_FORMAT_XI}, {"HTK", SF_FORMAT_HTK}, {"SDS", SF_FORMAT_SDS}, {"AVR", SF_FORMAT_AVR}, {"WAVEX", SF_FORMAT_WAVEX}, {"SD2", SF_FORMAT_SD2}, {"FLAC", SF_FORMAT_FLAC}, {"CAF", SF_FORMAT_CAF}};
    const Entry minors[] = {{"PCM_S8", SF_FORMAT_PCM_S8}, {"PCM_16", SF_FORMAT_PCM_16}, {"PCM_24", SF_FORMAT_PCM_24}, {"PCM_32", SF_FORMAT_PCM_32}, {"PCM_U8", SF_FORMAT_PCM_U8}, {"FLOAT", SF_FORMAT_FLOAT}, {"DOUBLE", SF_FORMAT_DOUBLE}, {"ULAW", SF_FORMAT_ULAW}, {"ALAW", SF_FORMAT_ALAW}, {"IMA_ADPCM", SF_FORMAT_IMA_ADPCM}, {"MS_ADPCM", SF_FORMAT_MS_ADPCM}, {"GSM610", SF_FORMAT_GSM610}, {"VOX_ADPCM", SF_FORMAT_VOX_ADPCM}, {"G721_32", SF_FORMAT_G721_32}, {"G723_24", SF_FORMAT_G723_24}, {"G723_40", SF_FORMAT_G723_40}, {"DWVW_12", SF_FORMAT_DWVW_12}, {"DWVW_16", SF_FORMAT_DWVW_16}, {"DWVW_24", SF_FORMAT_DWVW_24}, {"DWVW_N", SF_FORMAT_DWVW_N}, {"DPCM_8", SF_FORMAT_DPCM_8}, {"DPCM_16", SF_FORMAT_DPCM_16}};
    for (const Entry& major : majors) for (const Entry& minor : minors) {
        check(SndFileFormat::Lookup(major.name, minor.name) == (major.mask | minor.mask), "format mask differs from libsndfile constant");
        std::string lower_major = major.name, lower_minor = minor.name;
        std::transform(lower_major.begin(), lower_major.end(), lower_major.begin(), [](char c) { return std::tolower(c); });
        std::transform(lower_minor.begin(), lower_minor.end(), lower_minor.begin(), [](char c) { return std::tolower(c); });
        check(SndFileFormat::Lookup(lower_major.c_str(), lower_minor.c_str()) == (major.mask | minor.mask), "lower-case formats rejected");
    }
    check(SndFileFormat::Lookup("WaVeX", "PcM_24") == (SF_FORMAT_WAVEX | SF_FORMAT_PCM_24), "mixed-case formats rejected");
}
static void rejected_formats_case() {
    const char* pairs[][2] = {{"", "FLOAT"}, {"WAV", ""}, {"UNKNOWN", "FLOAT"}, {"WAV", "UNKNOWN"}, {"WAVEXjunk", "FLOAT"}, {"WAV", "VOX_ADPCMmore"}, {" WAV", "FLOAT"}, {"WAV", "FLOAT "}};
    for (const auto& pair : pairs) {
        bool rejected = false;
        try { SndFileFormat::Lookup(pair[0], pair[1]); }
        catch (const std::range_error&) { rejected = true; }
        check(rejected, "unknown/prefix/whitespace format must be rejected");
    }
}
static void path_case() {
    const char* paths[] = {"test.wav", "test.RAW", "test.dbl"};
    const int expected[] = {0, SF_FORMAT_RAW | SF_FORMAT_FLOAT, SF_FORMAT_RAW | SF_FORMAT_DOUBLE};
    for (unsigned i = 0; i != 3; ++i) {
        SF_INFO value = info(SF_FORMAT_WAV | SF_FORMAT_PCM_16);
        { CWaveFileHandle file(paths[i], SFM_READ, &value, 44100); }
        check(last_path == paths[i] && last_info.format == expected[i], "named path read behavior changed");
        if (i) check(last_info.channels == 1 && last_info.samplerate == 44100, "named raw defaults changed");
    }
}
#ifdef HAS_STATUS_API
static void status_case(const std::string& name) {
    SF_INFO value = info(SF_FORMAT_RAW | SF_FORMAT_FLOAT);
    {
        CWaveFileHandle file(1, SFM_WRITE, &value);
        if (name == "stream_error") {
            check(file.error() == 0, "clean stream must report no error");
            stream_error = 19;
            check(file.error() == 19, "stream error must be returned unchanged");
        } else {
            close_status = (name == "explicit_close" || name == "sync_failure") ? 0 : 23;
            sync_status = (name == "sync_failure" || name == "sync_and_close_failure") ? 29 : 0;
            if (name != "destructor_failure") {
                const int expected = close_status ? close_status : sync_status;
                check(file.close() == expected, "close must report finalization failure, preferring close status");
                check(closed == 1 && synced == 1, "explicit close must sync and close exactly once");
                check(file.close() == 0, "closing an already closed handle is harmless");
                check(closed == 1 && synced == 1, "repeated close must not touch released handle");
            }
        }
    }
    check(closed == 1 && synced == 1, "destructor must not double close even on close failure");
    check(last_ownership == SF_FALSE, "close retains caller descriptor ownership");
}
#endif
static void default_handle_case() {
    { CWaveFileHandle file; }
    check(closed == 0 && synced == 0 && opened == 0, "default handle must not call libsndfile cleanup");
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        const std::string name = argv[1];
        if (name == "fd_raw" || name == "fd_autodetect" || name == "fd_write") descriptor_case(name);
        else if (name == "invalid_write") error_case(true);
        else if (name == "open_failure") error_case(false);
        else if (name == "diagnostics") diagnostics_case();
        else if (name == "formats") format_case();
        else if (name == "reject_formats") rejected_formats_case();
        else if (name == "path_compatibility") path_case();
        else if (name == "default_handle") default_handle_case();
#ifdef HAS_STATUS_API
        else if (name == "stream_error" || name == "explicit_close" || name == "close_failure" || name == "destructor_failure" || name == "sync_failure" || name == "sync_and_close_failure") status_case(name);
#endif
        else return 2;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
