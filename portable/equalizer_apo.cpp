// SPDX-License-Identifier: GPL-2.0-or-later
#include "equalizer_apo.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <stdexcept>
#include <string_view>

// Equalizer APO semantics this relies on, checked against its 1.4.2 sources:
// - "Copy:" evaluates every assignment from the channels as they were before
//   the line, replaces only its targets and creates a channel for a target
//   name the device does not have (the "virtual channels" of the installer's
//   selective_delay.txt example). Created channels start silent each period.
// - It splits a sum on '+' and a term on '*', and reads a lone term as a
//   channel unless it is "0" or contains '.'. Numbers that need an exponent
//   would contain '+', so factors are written in fixed notation.
// - "Channel:" upper-cases its words; Copy does not. Created names are
//   therefore upper case, and do not start with a digit (a channel number).
// - "Convolution:" with a mono file filters each selected channel separately
//   in place, without added latency; "Delay: N samples" rounds N to an integer.
// - Include restores the including file's channel selection afterwards.
// - "If:" and "Eval:" run while the file loads, so only the chosen filter's
//   commands exist. "Channel: all" selects created channels too, and
//   "Preamp: -1000 dB" multiplies by 1e-50, which is 0 as a 32-bit float.
namespace convolver::equalizer_apo {
namespace {
std::string number(float value) {
    char buffer[128];
    const auto result =
        std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed);
    if (result.ec != std::errc{})
        throw std::runtime_error("cannot format a gain for Equalizer APO");
    return {buffer, result.ptr};
}
std::string term(float gain, const std::string& channel) {
    return gain == 1.0F ? channel : number(gain) + '*' + channel;
}
std::string device_channel(std::size_t index) {
    return std::to_string(index + 1);
}
std::string path_channel(std::size_t index) {
    return "CONVOLVER" + std::to_string(index + 1);
}
std::string describe(const Source& s) {
    const auto& c = s.configuration;
    return s.label + ", " + std::to_string(c.sample_rate) + " Hz, " +
           std::to_string(c.input_channels) + (c.input_channels == 1 ? " input, " : " inputs, ") +
           std::to_string(c.output_channels) + (c.output_channels == 1 ? " output" : " outputs");
}
// Lines selecting channels with equal delays, then delaying them.
void delays(std::string& out, const std::vector<std::size_t>& frames, std::size_t count,
            std::string_view indent) {
    std::map<std::size_t, std::vector<std::size_t>> groups;
    for (std::size_t c = 0; c < count && c < frames.size(); ++c)
        if (frames[c] != 0)
            groups[frames[c]].push_back(c);
    for (const auto& [delay, channels] : groups) {
        out += indent;
        out += "Channel:";
        for (const auto c : channels)
            out += ' ' + device_channel(c);
        out += '\n';
        out += indent;
        out += "Delay: " + std::to_string(delay) + " samples\n";
    }
}
class Impulses {
  public:
    explicit Impulses(std::vector<ImpulseFile>& files) : files_(files) {}
    const std::string& name(int rate, const std::vector<float>& samples) {
        for (const auto& f : files_)
            if (f.sample_rate == rate && f.samples == samples)
                return f.name;
        files_.push_back(
            {"ir-" + std::to_string(files_.size() + 1) + '-' + std::to_string(rate) + "Hz.wav",
             rate, samples});
        return files_.back().name;
    }

  private:
    std::vector<ImpulseFile>& files_;
};
void body(std::string& out, const Source& s, std::size_t number, Impulses& impulses) {
    const auto& c = s.configuration;
    const std::string_view indent = "  ";
    const auto channels = device_channels(c);
    out += "If: convolverFilter == " + std::to_string(number) + '\n';
    out += "  # Filter " + std::to_string(number) + ": " + describe(s) + '\n';
    if (std::any_of(c.input_delays.begin(), c.input_delays.end(), [](auto d) { return d != 0; })) {
        out += "  # Input delays\n";
        delays(out, c.input_delays, c.input_channels, indent);
    }
    // Paths with no input or no output contribute nothing.
    std::vector<std::size_t> used;
    for (std::size_t p = 0; p < c.paths.size(); ++p)
        if (!c.paths[p].inputs.empty() && !c.paths[p].outputs.empty())
            used.push_back(p);
    if (!used.empty())
        out += "  # Mix each path's input into a channel of its own\n";
    for (std::size_t k = 0; k < used.size(); ++k) {
        out += "  Copy: " + path_channel(k) + '=';
        const auto& inputs = c.paths[used[k]].inputs;
        for (std::size_t r = 0; r < inputs.size(); ++r)
            out += (r ? "+" : "") + term(inputs[r].gain, device_channel(inputs[r].channel));
        out += '\n';
    }
    // One Convolution per distinct impulse, on every path channel that uses it.
    std::vector<std::pair<std::string, std::vector<std::size_t>>> groups;
    for (std::size_t k = 0; k < used.size(); ++k) {
        const auto& name = impulses.name(c.sample_rate, c.paths[used[k]].impulse);
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&](const auto& g) { return g.first == name; });
        if (it == groups.end())
            groups.push_back({name, {k}});
        else
            it->second.push_back(k);
    }
    if (!groups.empty())
        out += "  # Filter each path\n";
    for (const auto& [name, members] : groups) {
        out += "  Channel:";
        for (const auto k : members)
            out += ' ' + path_channel(k);
        out += "\n  Convolution: " + name + '\n';
    }
    out += "  # Sum the paths into the outputs; channels without one are silent\n";
    for (std::size_t o = 0; o < channels; ++o) {
        std::string sum;
        for (std::size_t k = 0; o < c.output_channels && k < used.size(); ++k)
            for (const auto& route : c.paths[used[k]].outputs)
                if (route.channel == o)
                    sum += (sum.empty() ? "" : "+") + term(route.gain, path_channel(k));
        out += "  Copy: " + device_channel(o) + '=' + (sum.empty() ? "0.0" : sum) + '\n';
    }
    if (std::any_of(c.output_delays.begin(), c.output_delays.end(),
                    [](auto d) { return d != 0; })) {
        out += "  # Output delays\n";
        delays(out, c.output_delays, c.output_channels, indent);
    }
    out += "  Channel: all\nEndIf:\n";
}
} // namespace

std::size_t device_channels(const Configuration& c) noexcept {
    return std::max(c.input_channels, c.output_channels);
}

Export make_export(const std::vector<Source>& sources, const std::vector<std::string>& header) {
    Export result;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& c = sources[i].configuration;
        if (c.sample_rate <= 0 || device_channels(c) == 0)
            throw std::invalid_argument(sources[i].label + " has no sample rate or channels");
        const auto channels = device_channels(c);
        const auto earlier =
            std::find_if(result.filters.begin(), result.filters.end(), [&](const Filter& f) {
                return sources[f.source].configuration.sample_rate == c.sample_rate &&
                       f.device_channels == channels;
            });
        if (earlier != result.filters.end()) {
            result.skipped.push_back(
                describe(sources[i]) + ": left out, because Equalizer APO only knows the " +
                "device's sample rate and channel count, and those also select the earlier " +
                describe(sources[earlier->source]) + ". To use this one, export it on its own.");
            continue;
        }
        result.filters.push_back({i, channels});
    }

    auto& out = result.text;
    for (const auto& line : header)
        out += (line.empty() ? "#" : "# " + line) + '\n';
    out += "#\n"
           "# Applies the first filter below whose sample rate is the device's and\n"
           "# whose channels the device has: an exact channel count first, then a\n"
           "# device with more channels, up to " +
           std::to_string(max_device_channels) +
           ", whose extra channels are silenced.\n"
           "# Without a match every channel is silenced rather than played\n"
           "# unfiltered. Channel 1 here is Convolver's channel 0, in Windows\n"
           "# speaker order.\n\n";
    if (result.filters.empty())
        return result;
    out += "Eval: convolverFilter = 0\nEval: convolverChannels = 0\n";
    for (const bool exact : {true, false}) {
        out += exact ? "# A device with exactly the filter's channels\n"
                     : "# A device with more channels than the filter's\n";
        for (std::size_t n = 0; n < result.filters.size(); ++n) {
            const auto& f = result.filters[n];
            if (!exact && f.device_channels >= max_device_channels)
                continue;
            std::string indent = "  ";
            out +=
                "If: sampleRate == " + std::to_string(sources[f.source].configuration.sample_rate) +
                '\n';
            out += "  If: outputChannelCount " + std::string(exact ? "==" : ">") + ' ' +
                   std::to_string(f.device_channels) + '\n';
            if (!exact) {
                out += "    If: outputChannelCount < " + std::to_string(max_device_channels + 1) +
                       '\n';
                indent += "  ";
            }
            out += indent + "  If: convolverFilter == 0\n";
            out += indent + "    Eval: convolverFilter = " + std::to_string(n + 1) + '\n';
            out += indent + "    Eval: convolverChannels = " + std::to_string(f.device_channels) +
                   '\n';
            out += indent + "  EndIf:\n";
            if (!exact)
                out += "    EndIf:\n";
            out += "  EndIf:\nEndIf:\n";
        }
    }
    out += "# No filter fits this device: silence every channel (-1000 dB is far\n"
           "# below the smallest sample a 32-bit float can hold)\n"
           "If: convolverFilter == 0\n  Channel: all\n  Preamp: -1000 dB\nEndIf:\n";
    // Device channels beyond the filter's carry no filtered audio: silence them
    // rather than let them play unfiltered.
    const auto fewest = std::min_element(result.filters.begin(), result.filters.end(),
                                         [](const Filter& a, const Filter& b) {
                                             return a.device_channels < b.device_channels;
                                         })
                            ->device_channels;
    if (fewest < max_device_channels)
        out += "# Silence the device channels the filter does not use\n";
    for (auto channel = fewest + 1; channel <= max_device_channels; ++channel)
        out += "If: outputChannelCount > " + std::to_string(channel - 1) +
               "\n  If: convolverChannels < " + std::to_string(channel) +
               "\n    Copy: " + std::to_string(channel) + "=0.0\n  EndIf:\nEndIf:\n";
    Impulses impulses(result.impulses);
    for (std::size_t n = 0; n < result.filters.size(); ++n) {
        out += '\n';
        body(out, sources[result.filters[n].source], n + 1, impulses);
    }
    return result;
}
} // namespace convolver::equalizer_apo
