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
guard let comp = AudioComponentFindNext(nil, &desc) else { fputs("not found\n", stderr); exit(1) }
var unit: AudioUnit?
precondition(AudioComponentInstanceNew(comp, &unit) == noErr)
defer { AudioComponentInstanceDispose(unit!) }
precondition(AudioUnitInitialize(unit!) == noErr)

var size: UInt32 = 0
var writable: DarwinBoolean = false
var st = AudioUnitGetPropertyInfo(unit!, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, &writable)
print("ParameterList size=\(size) status=\(st)")
let count = Int(size) / MemoryLayout<AudioUnitParameterID>.size
var ids = [AudioUnitParameterID](repeating: 0, count: max(count, 0))
if count > 0 {
    st = AudioUnitGetProperty(unit!, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &ids, &size)
}
print("count=\(count) status=\(st)")

for id in ids {
    var info = AudioUnitParameterInfo()
    var sz = UInt32(MemoryLayout<AudioUnitParameterInfo>.size)
    st = AudioUnitGetProperty(unit!, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &sz)
    var cname = info.name
    let name2 = withUnsafePointer(to: &cname) {
        $0.withMemoryRebound(to: CChar.self, capacity: 52) { String(cString: $0) }
    }
    let cfName = info.cfNameString?.takeUnretainedValue() as String?
    var value: AudioUnitParameterValue = 0
    AudioUnitGetParameter(unit!, id, kAudioUnitScope_Global, 0, &value)
    print("id=\(id) name=\(cfName ?? name2) min=\(info.minValue) max=\(info.maxValue) val=\(value)")
}
