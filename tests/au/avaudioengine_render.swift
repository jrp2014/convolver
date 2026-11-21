// SPDX-License-Identifier: GPL-2.0-or-later
// Renders audio through the installed Convolver Audio Unit with Apple's own
// hosting stack: AVAudioEngine in offline manual rendering mode, the unit as
// an AVAudioUnitEffect, and a preset applied through ClassInfo, as Logic and
// GarageBand restore one. tests/au/avaudioengine_tests.py runs it and checks
// the output against the shared fixtures.
//
// Usage: avaudioengine_render CODES PRESET INPUT OUTPUT RATE INPUTS OUTPUTS BLOCK FRAMES [OPTIONS]
//   CODES    the unit's type, subtype and manufacturer, as "aufx,Conv,CONV"
//   PRESET   an .aupreset
//   INPUT    raw interleaved little-endian float32, INPUTS channels; zeros follow it
//   OUTPUT   written the same way, OUTPUTS channels, FRAMES frames
//   BLOCK    frames per render call (the engine's maximum frame count)
// Options, each a host's order of events (AU-18):
//   --start-first  start the engine with the unit at its defaults and render
//                  before restoring the preset, as GarageBand does when it
//                  opens a project; the preset is then restored on the
//                  running unit
//   --latency N    choose Latency N frames through the unit's parameter once
//                  the engine runs, as a user does while a project plays
//   --round-trip   configure one unit, read its ClassInfo as a host saves a
//                  project, and render with a second unit restored from it
// With --start-first or --latency, silence is rendered until the change has
// been applied (the Status parameter reads Active and the latency the unit
// reports is the one chosen), and the input starts after it. The input's
// first frame is OUTPUT's first frame.
// Prints the latency the unit reports, in frames, as "latency N", and, for
// --latency without --round-trip, "latency notified N" once the host's
// property listener has been called while the unit reported the chosen N.

import AVFoundation
import AudioToolbox
import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data(("avaudioengine_render: " + message + "\n").utf8))
    exit(1)
}

func fourCharacterCode(_ text: String) -> OSType {
    text.utf8.reduce(0) { ($0 << 8) | OSType($1) }
}

let arguments = CommandLine.arguments
var startFirst = false
var roundTrip = false
var chosenLatency: Int?
var positional = [String]()
var index = 1
while index < arguments.count {
    switch arguments[index] {
    case "--start-first": startFirst = true
    case "--round-trip": roundTrip = true
    case "--latency":
        index += 1
        guard index < arguments.count, let frames = Int(arguments[index]) else {
            fail("--latency needs a number of frames")
        }
        chosenLatency = frames
    default: positional.append(arguments[index])
    }
    index += 1
}
guard positional.count == 9 else {
    fail("usage: avaudioengine_render CODES PRESET INPUT OUTPUT RATE INPUTS OUTPUTS BLOCK FRAMES"
        + " [--start-first] [--latency N] [--round-trip]")
}
let codes = positional[0].split(separator: ",").map(String.init)
guard codes.count == 3, codes.allSatisfy({ $0.utf8.count == 4 }) else {
    fail("CODES must be three four-character codes, as \"aufx,Conv,CONV\"")
}
let presetPath = positional[1]
let inputPath = positional[2]
let outputPath = positional[3]
guard let rate = Double(positional[4]), let inputs = Int(positional[5]), let outputs = Int(positional[6]),
      let block = AVAudioFrameCount(positional[7]), let frames = Int(positional[8]),
      inputs > 0, outputs > 0, block > 0, frames > 0
else {
    fail("RATE, INPUTS, OUTPUTS, BLOCK and FRAMES must be positive numbers")
}

// The unit's parameters (plugins/identities.json) and its Latency menu
// (host_latencies in include/convolver/host.hpp): the parameter takes the
// entry's index.
let statusParameter: AudioUnitParameterID = 1001
let latencyParameter: AudioUnitParameterID = 1004
let statusActive: AudioUnitParameterValue = 0
let latencies = [0, 64, 128, 256, 512, 1024, 2048, 4096]

// Mono and stereo as hosts label them; more channels as discrete ones.
func format(channels: Int) -> AVAudioFormat {
    let tag: AudioChannelLayoutTag = switch channels {
    case 1: kAudioChannelLayoutTag_Mono
    case 2: kAudioChannelLayoutTag_Stereo
    default: kAudioChannelLayoutTag_DiscreteInOrder | AudioChannelLayoutTag(channels)
    }
    guard let layout = AVAudioChannelLayout(layoutTag: tag) else {
        fail("no channel layout for \(channels) channels")
    }
    return AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: rate, interleaved: false,
                         channelLayout: layout)
}

guard let inputData = FileManager.default.contents(atPath: inputPath) else {
    fail("cannot read \(inputPath)")
}
let inputSamples: [Float] = inputData.withUnsafeBytes { Array($0.bindMemory(to: Float.self)) }
let inputFrames = inputSamples.count / inputs

let description = AudioComponentDescription(
    componentType: fourCharacterCode(codes[0]), componentSubType: fourCharacterCode(codes[1]),
    componentManufacturer: fourCharacterCode(codes[2]), componentFlags: 0, componentFlagsMask: 0)
guard !AVAudioUnitComponentManager.shared().components(matching: description).isEmpty else {
    fail("no Audio Unit \(codes.joined(separator: " ")) is installed")
}

guard let presetData = FileManager.default.contents(atPath: presetPath),
      let preset = try? PropertyListSerialization.propertyList(from: presetData, format: nil) as? NSDictionary
else {
    fail("cannot read the preset \(presetPath)")
}

func restore(_ unit: AudioUnit, _ state: CFPropertyList, _ what: String) {
    var classInfo: CFPropertyList = state
    let status = withUnsafePointer(to: &classInfo) {
        AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, $0,
                             UInt32(MemoryLayout<CFPropertyList>.size))
    }
    guard status == noErr else {
        fail("the unit refused \(what) (OSStatus \(status))")
    }
}

func setLatency(_ unit: AudioUnit, _ latency: Int) {
    guard let entry = latencies.firstIndex(of: latency) else {
        fail("Latency \(latency) is not in the menu \(latencies)")
    }
    let status = AudioUnitSetParameter(unit, latencyParameter, kAudioUnitScope_Global, 0,
                                       AudioUnitParameterValue(entry), 0)
    guard status == noErr else {
        fail("the unit refused Latency \(latency) (OSStatus \(status))")
    }
}

func parameter(_ unit: AudioUnit, _ id: AudioUnitParameterID) -> AudioUnitParameterValue {
    var value: AudioUnitParameterValue = -1
    let status = AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &value)
    guard status == noErr else {
        fail("cannot read parameter \(id) (OSStatus \(status))")
    }
    return value
}

func reportedLatency(_ unit: AudioUnit) -> Int {
    var seconds: Float64 = -1
    var size = UInt32(MemoryLayout<Float64>.size)
    let status = AudioUnitGetProperty(unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &seconds, &size)
    guard status == noErr else {
        fail("cannot read the latency (OSStatus \(status))")
    }
    return Int((seconds * rate).rounded())
}

let effect = AVAudioUnitEffect(audioComponentDescription: description)

// --round-trip: a first unit takes the preset and the Latency, and its
// ClassInfo, as a host saves it in a project, restores the unit that plays.
if roundTrip {
    let saved = AVAudioUnitEffect(audioComponentDescription: description)
    restore(saved.audioUnit, preset, "the preset")
    if let latency = chosenLatency {
        setLatency(saved.audioUnit, latency)
    }
    var state: Unmanaged<CFPropertyList>?
    var size = UInt32(MemoryLayout<Unmanaged<CFPropertyList>?>.size)
    let status = AudioUnitGetProperty(saved.audioUnit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                                      &state, &size)
    guard status == noErr, let classInfo = state?.takeRetainedValue() else {
        fail("cannot read the first unit's ClassInfo (OSStatus \(status))")
    }
    restore(effect.audioUnit, classInfo, "the first unit's ClassInfo")
    print("restored a second unit from the first one's ClassInfo")
} else if !startFirst {
    // The preset, before the engine starts the unit, as a host restores a project.
    restore(effect.audioUnit, preset, "the preset")
}

// The host's property listener, as GarageBand and Logic register one for
// the latency; the unit calls it on the main thread (AU-17). Each call
// records the latency the unit reports then, so that the notice for a
// chosen Latency is told apart from the one initialization sends. A C
// callback cannot call reportedLatency (a top-level function, which
// captures context), so it records the seconds and the frames are worked
// out after.
var notifiedSeconds = [Float64]()
let listener: AudioUnitPropertyListenerProc = { _, unit, id, _, _ in
    if id == kAudioUnitProperty_Latency {
        var seconds: Float64 = -1
        var size = UInt32(MemoryLayout<Float64>.size)
        _ = AudioUnitGetProperty(unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &seconds, &size)
        notifiedSeconds.append(seconds)
    }
}
func notifiedLatencies() -> [Int] {
    notifiedSeconds.map { Int(($0 * rate).rounded()) }
}
guard AudioUnitAddPropertyListener(effect.audioUnit, kAudioUnitProperty_Latency, listener, nil) == noErr else {
    fail("cannot add a property listener")
}

let engine = AVAudioEngine()
let inputFormat = format(channels: inputs)
let outputFormat = format(channels: outputs)
do {
    try engine.enableManualRenderingMode(.offline, format: outputFormat, maximumFrameCount: block)
} catch {
    fail("cannot enable offline rendering: \(error)")
}
engine.attach(effect)
engine.connect(engine.inputNode, to: effect, format: inputFormat)
engine.connect(effect, to: engine.outputNode, format: outputFormat)

// The engine pulls input as it renders: silence while `playing` is false,
// then the next frames of the file, then zeros.
guard let source = AVAudioPCMBuffer(pcmFormat: inputFormat, frameCapacity: block) else {
    fail("cannot allocate the input buffer")
}
var playing = false
var position = 0
let accepted = engine.inputNode.setManualRenderingInputPCMFormat(inputFormat) { count in
    source.frameLength = count
    for channel in 0 ..< inputs {
        let samples = source.floatChannelData![channel]
        for index in 0 ..< Int(count) {
            let frame = position + index
            samples[index] = playing && frame < inputFrames ? inputSamples[frame * inputs + channel] : 0
        }
    }
    if playing {
        position += Int(count)
    }
    return UnsafePointer(source.audioBufferList)
}
guard accepted else {
    fail("the engine refused the input format")
}

engine.prepare()
do {
    try engine.start()
} catch {
    fail("cannot start the engine: \(error)")
}

guard let rendered = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat,
                                      frameCapacity: engine.manualRenderingMaximumFrameCount)
else {
    fail("cannot allocate the output buffer")
}

func renderBlock(_ request: AVAudioFrameCount) {
    let status: AVAudioEngineManualRenderingStatus
    do {
        status = try engine.renderOffline(request, to: rendered)
    } catch {
        fail("rendering failed: \(error)")
    }
    guard status == .success, rendered.frameLength == request else {
        fail("rendering returned status \(status.rawValue) with \(rendered.frameLength) of \(request) frames")
    }
}

// Renders silence, and lets the main queue run the unit's notices, until
// `done` holds; at most 30 seconds of wall time.
func settle(_ what: String, until done: () -> Bool) {
    let deadline = Date().addingTimeInterval(30)
    while !done() {
        guard Date() < deadline else {
            fail("\(what) did not happen within 30 s (Status \(parameter(effect.audioUnit, statusParameter)), "
                + "latency \(reportedLatency(effect.audioUnit)))")
        }
        renderBlock(block)
        RunLoop.main.run(until: Date().addingTimeInterval(0.01))
    }
}

if startFirst {
    // GarageBand starts the unit at its defaults, plays, then restores the
    // project on the main thread while the engine runs.
    for _ in 0 ..< 8 {
        renderBlock(block)
    }
    restore(effect.audioUnit, preset, "the preset while running")
    settle("the restored filter's Active Status") { parameter(effect.audioUnit, statusParameter) == statusActive }
    print("restored the preset while running")
}
if let latency = chosenLatency, !roundTrip {
    // Chosen while the engine runs: the unit prepares it on its Latency
    // queue, then reports it and tells the host's listener.
    RunLoop.main.run(until: Date().addingTimeInterval(0.1))
    notifiedSeconds.removeAll()
    setLatency(effect.audioUnit, latency)
    settle("Latency \(latency) and its notice") {
        reportedLatency(effect.audioUnit) == latency && parameter(effect.audioUnit, statusParameter) == statusActive
            && notifiedLatencies().contains(latency)
    }
    print("chose Latency \(latency) while running")
    print("latency notified \(latency)")
}
// Notices still queued for the main thread arrive before the result.
RunLoop.main.run(until: Date().addingTimeInterval(0.1))
print("latency \(reportedLatency(effect.audioUnit))")

playing = true
var output = [Float]()
output.reserveCapacity(frames * outputs)
while output.count < frames * outputs {
    let request = min(block, AVAudioFrameCount(frames - output.count / outputs))
    renderBlock(request)
    for index in 0 ..< Int(rendered.frameLength) {
        for channel in 0 ..< outputs {
            output.append(rendered.floatChannelData![channel][index])
        }
    }
}
engine.stop()

let data = output.withUnsafeBufferPointer { Data(buffer: $0) }
guard FileManager.default.createFile(atPath: outputPath, contents: data) else {
    fail("cannot write \(outputPath)")
}
