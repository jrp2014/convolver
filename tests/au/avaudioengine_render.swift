// SPDX-License-Identifier: GPL-2.0-or-later
// Renders audio through the installed Convolver Audio Unit with Apple's own
// hosting stack: AVAudioEngine in offline manual rendering mode, the unit as
// an AVAudioUnitEffect, and a preset applied through ClassInfo, as Logic and
// GarageBand restore one. tests/au/avaudioengine_tests.py runs it and checks
// the output against the shared fixtures.
//
// Usage: avaudioengine_render CODES PRESET INPUT OUTPUT RATE INPUTS OUTPUTS BLOCK FRAMES
//   CODES    the unit's type, subtype and manufacturer, as "aufx,Conv,CONV"
//   PRESET   an .aupreset
//   INPUT    raw interleaved little-endian float32, INPUTS channels; zeros follow it
//   OUTPUT   written the same way, OUTPUTS channels, FRAMES frames
//   BLOCK    frames per render call (the engine's maximum frame count)
// Prints the latency the unit reports, in frames, as "latency N".

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
guard arguments.count == 10 else {
    fail("usage: avaudioengine_render CODES PRESET INPUT OUTPUT RATE INPUTS OUTPUTS BLOCK FRAMES")
}
let codes = arguments[1].split(separator: ",").map(String.init)
guard codes.count == 3, codes.allSatisfy({ $0.utf8.count == 4 }) else {
    fail("CODES must be three four-character codes, as \"aufx,Conv,CONV\"")
}
let presetPath = arguments[2]
let inputPath = arguments[3]
let outputPath = arguments[4]
guard let rate = Double(arguments[5]), let inputs = Int(arguments[6]), let outputs = Int(arguments[7]),
      let block = AVAudioFrameCount(arguments[8]), let frames = Int(arguments[9]),
      inputs > 0, outputs > 0, block > 0, frames > 0
else {
    fail("RATE, INPUTS, OUTPUTS, BLOCK and FRAMES must be positive numbers")
}

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
let effect = AVAudioUnitEffect(audioComponentDescription: description)

// The preset, before the engine starts the unit, as a host restores a project.
guard let presetData = FileManager.default.contents(atPath: presetPath),
      let preset = try? PropertyListSerialization.propertyList(from: presetData, format: nil) as? NSDictionary
else {
    fail("cannot read the preset \(presetPath)")
}
var classInfo: CFPropertyList = preset
let restored = withUnsafePointer(to: &classInfo) {
    AudioUnitSetProperty(effect.audioUnit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, $0,
                         UInt32(MemoryLayout<CFPropertyList>.size))
}
guard restored == noErr else {
    fail("the unit refused the preset (OSStatus \(restored))")
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

// The engine pulls input as it renders: the next frames of the file, then zeros.
guard let source = AVAudioPCMBuffer(pcmFormat: inputFormat, frameCapacity: block) else {
    fail("cannot allocate the input buffer")
}
var position = 0
let accepted = engine.inputNode.setManualRenderingInputPCMFormat(inputFormat) { count in
    source.frameLength = count
    for channel in 0 ..< inputs {
        let samples = source.floatChannelData![channel]
        for index in 0 ..< Int(count) {
            let frame = position + index
            samples[index] = frame < inputFrames ? inputSamples[frame * inputs + channel] : 0
        }
    }
    position += Int(count)
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
print("latency \(Int((effect.auAudioUnit.latency * rate).rounded()))")

guard let rendered = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat,
                                      frameCapacity: engine.manualRenderingMaximumFrameCount)
else {
    fail("cannot allocate the output buffer")
}
var output = [Float]()
output.reserveCapacity(frames * outputs)
while output.count < frames * outputs {
    let request = min(block, AVAudioFrameCount(frames - output.count / outputs))
    let status: AVAudioEngineManualRenderingStatus
    do {
        status = try engine.renderOffline(request, to: rendered)
    } catch {
        fail("rendering failed: \(error)")
    }
    guard status == .success, rendered.frameLength == request else {
        fail("rendering returned status \(status.rawValue) with \(rendered.frameLength) of \(request) frames")
    }
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
