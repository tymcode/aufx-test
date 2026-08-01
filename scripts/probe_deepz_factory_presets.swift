#!/usr/bin/env swift
import Foundation
import AudioToolbox

func fourCC(_ s: String) -> OSType {
    var v: OSType = 0
    for b in s.utf8 { v = (v << 8) | OSType(b) }
    return v
}

var desc = AudioComponentDescription(
    componentType: fourCC("aumf"),
    componentSubType: fourCC("DPPL"),
    componentManufacturer: fourCC("TDSP"),
    componentFlags: 0,
    componentFlagsMask: 0
)
guard let comp = AudioComponentFindNext(nil, &desc) else {
    fputs("component not found\n", stderr); exit(1)
}
var unit: AudioUnit?
precondition(AudioComponentInstanceNew(comp, &unit) == noErr)
defer { AudioComponentInstanceDispose(unit!) }
precondition(AudioUnitInitialize(unit!) == noErr)

var size: UInt32 = 0
var writable: DarwinBoolean = false
var st = AudioUnitGetPropertyInfo(unit!, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &size, &writable)
print("FactoryPresets info status=\(st) size=\(size) writable=\(writable)")

if st == noErr && size > 0 {
    let ptr = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: MemoryLayout<CFArray>.alignment)
    defer { ptr.deallocate() }
    var sz = size
    st = AudioUnitGetProperty(unit!, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, ptr, &sz)
    print("get status=\(st)")
    let cf = ptr.load(as: CFArray.self)
    let n = CFArrayGetCount(cf)
    print("count \(n)")
    for i in 0..<min(n, 40) {
        let raw = CFArrayGetValueAtIndex(cf, i)!
        let preset = raw.assumingMemoryBound(to: AUPreset.self).pointee
        let name = preset.presetName?.takeUnretainedValue() as String? ?? "?"
        print("\(preset.presetNumber)\t\(name)")
    }
}

size = 0
st = AudioUnitGetPropertyInfo(unit!, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &size, &writable)
print("PresentPreset info status=\(st) size=\(size)")
if st == noErr {
    var preset = AUPreset(presetNumber: 0, presetName: nil)
    var sz = UInt32(MemoryLayout<AUPreset>.size)
    st = AudioUnitGetProperty(unit!, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &preset, &sz)
    let name = preset.presetName?.takeUnretainedValue() as String? ?? "?"
    print("present status=\(st) num=\(preset.presetNumber) name=\(name)")
}
