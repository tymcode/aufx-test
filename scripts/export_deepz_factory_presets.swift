#!/usr/bin/env swift
/**
 Export DEEP/Z factory effect states as .aupreset files.

 Strategy: load Init Serial (or a template), rewrite DEEPZ_STATE effect/effectName,
 setState on the AU, getState back (plugin expands factory defaults), write .aupreset.

 Usage:
   swift scripts/export_deepz_factory_presets.swift \
     --out compares/deepz/presets/au \
     --only 4,6,9
 */
import Foundation
import AudioToolbox
import CoreFoundation

struct EffectSpec {
    let number: Int
    let name: String
}

// DEEP/Z effect names from manual pages 172-174 (plugin display names).
let allEffects: [EffectSpec] = [
    .init(number: 0, name: "Depthness of Space"),
    .init(number: 1, name: "Majesty Hall"),
    .init(number: 2, name: "Versa-Arena"),
    .init(number: 3, name: "Varying Hall"),
    .init(number: 4, name: "Intimacy Hall"),
    .init(number: 5, name: "Premiere Nite"),
    .init(number: 6, name: "Regal Hall"),
    .init(number: 7, name: "Max Capacity Hall"),
    .init(number: 8, name: "Voice Hall 1"),
    .init(number: 9, name: "Grand Ensemble Hall"),
    .init(number: 10, name: "Soloist's Hall"),
    .init(number: 11, name: "Contour Hall"),
    .init(number: 12, name: "Lead Voice Plate"),
    .init(number: 13, name: "Group Voice Plate"),
    .init(number: 14, name: "Peculiar Plate"),
    .init(number: 15, name: "Drum Set Plate"),
    .init(number: 16, name: "Sharp Attack Plate"),
    .init(number: 17, name: "Strummer's Plate"),
    .init(number: 18, name: "Pumping Plate"),
    .init(number: 19, name: "Petite Plate 1"),
    .init(number: 20, name: "So-Luscious Plate"),
    .init(number: 21, name: "Smooth Fizz Plate"),
    .init(number: 22, name: "Grand Plate"),
    .init(number: 23, name: "Non-Linear Drums"),
    .init(number: 24, name: "Non-Linear Vocals"),
    .init(number: 25, name: "Bigger Room"),
    .init(number: 26, name: "Mid-Size Room"),
    .init(number: 27, name: "Smaller Room"),
    .init(number: 28, name: "Very Tiny Room"),
    .init(number: 29, name: "Solid Wood Room"),
    .init(number: 30, name: "Stonewalled Room"),
    .init(number: 31, name: "Soft-Walled Room"),
    .init(number: 32, name: "Pumping Room"),
    .init(number: 33, name: "Vintage Brown Reverb!"),
    .init(number: 34, name: "Spacious Ambience"),
    .init(number: 35, name: "Famed Cathedral"),
    .init(number: 36, name: "Random-o-Tap"),
    .init(number: 37, name: "Railway Station"),
    .init(number: 38, name: "Lunar 13"),
    .init(number: 39, name: "Star Bloom"),
    .init(number: 40, name: "Big Boomer"),
    .init(number: 41, name: "Tapped-Time Reverb"),
    .init(number: 42, name: "Surrealist Vocals"),
    .init(number: 43, name: "Live Drum Chamber"),
    .init(number: 44, name: "Beam-o-Verb"),
    .init(number: 45, name: "Session Drums"),
    .init(number: 46, name: "Vintage Brown & Swirl"),
    .init(number: 47, name: "Percussive Plate"),
    .init(number: 48, name: "Snare Splash"),
    .init(number: 49, name: "Retro Room"),
    .init(number: 50, name: "Nightclub Modeler"),
    .init(number: 51, name: "Spatialized Delays"),
    .init(number: 52, name: "Tempo-Tap Bounce"),
    .init(number: 53, name: "Dark-Matter Delays"),
    .init(number: 54, name: "Resonating Delays"),
    .init(number: 55, name: "Random Delays"),
    .init(number: 56, name: "Lush Delays"),
    .init(number: 57, name: "Tweaked Taps"),
    .init(number: 58, name: "Time Modulation"),
    .init(number: 59, name: "S-p-r-e-a-d Taps"),
    .init(number: 60, name: "Plush Multi-Chorus"),
    .init(number: 61, name: "Wilder Multi-Chorus"),
    .init(number: 62, name: "Shifty Chorus"),
    .init(number: 63, name: "Diamond-Cut Chorus"),
    .init(number: 64, name: "Parametrized Chorus"),
    .init(number: 65, name: "Pretty Harmonies"),
    .init(number: 66, name: "Chorded Harmony"),
    .init(number: 67, name: "Scary Evil Harmo"),
    .init(number: 68, name: "Harmonic Synthesis"),
    .init(number: 69, name: "Haunted Souls Harmo"),
    .init(number: 70, name: "Psychedelic Flange"),
    .init(number: 71, name: "Liquified Flange"),
    .init(number: 72, name: "Rhythmic Flange"),
    .init(number: 73, name: "Multi-Phaser"),
    .init(number: 74, name: "Liquid Phaser"),
    .init(number: 75, name: "Rhythmic Phaser"),
    .init(number: 76, name: "Slower Tremolo"),
    .init(number: 77, name: "Guitar Trem"),
    .init(number: 78, name: "Pad Chop"),
    .init(number: 79, name: "Tempo Panning"),
    .init(number: 80, name: "Animated Randomness"),
    .init(number: 81, name: "Pan-o-Flange"),
    .init(number: 82, name: "Chatter-Shift"),
    .init(number: 83, name: "Phased Chatter"),
    .init(number: 84, name: "Psycho Chatter"),
    .init(number: 85, name: "Rotary in Orbit"),
    .init(number: 86, name: "Deep Ocean Echo"),
]

func fourCC(_ s: String) -> OSType {
    precondition(s.utf8.count == 4)
    var v: OSType = 0
    for b in s.utf8 { v = (v << 8) | OSType(b) }
    return v
}

func die(_ msg: String) -> Never {
    fputs("error: \(msg)\n", stderr)
    exit(1)
}

func loadAU() -> AudioUnit {
    // Match host.config.json: AudioUnit:Effects/aumf,DPPL,TDSP
    var desc = AudioComponentDescription(
        componentType: fourCC("aumf"),
        componentSubType: fourCC("DPPL"),
        componentManufacturer: fourCC("TDSP"),
        componentFlags: 0,
        componentFlagsMask: 0
    )
    guard let comp = AudioComponentFindNext(nil, &desc) else {
        // Fallback to aufx if aumf not found
        desc.componentType = fourCC("aufx")
        guard let comp2 = AudioComponentFindNext(nil, &desc) else {
            die("DEEP/Z Audio Unit not found (aumf/aufx DPPL TDSP)")
        }
        var unit: AudioUnit?
        let st = AudioComponentInstanceNew(comp2, &unit)
        if st != noErr || unit == nil { die("AudioComponentInstanceNew failed: \(st)") }
        return unit!
    }
    var unit: AudioUnit?
    let st = AudioComponentInstanceNew(comp, &unit)
    if st != noErr || unit == nil { die("AudioComponentInstanceNew failed: \(st)") }
    return unit!
}

func getClassInfo(_ unit: AudioUnit) -> CFPropertyList {
    var size = UInt32(MemoryLayout<CFPropertyList?>.size) // unused for CF types; use property getter
    var dataSize: UInt32 = 0
    var writable: DarwinBoolean = false
    var st = AudioUnitGetPropertyInfo(
        unit,
        kAudioUnitProperty_ClassInfo,
        kAudioUnitScope_Global,
        0,
        &dataSize,
        &writable
    )
    if st != noErr { die("GetPropertyInfo ClassInfo failed: \(st)") }
    var plist: CFPropertyList?
    st = withUnsafeMutablePointer(to: &plist) { ptr in
        AudioUnitGetProperty(
            unit,
            kAudioUnitProperty_ClassInfo,
            kAudioUnitScope_Global,
            0,
            ptr,
            &dataSize
        )
    }
    if st != noErr || plist == nil { die("GetProperty ClassInfo failed: \(st)") }
    return plist!
}

func setClassInfo(_ unit: AudioUnit, _ plist: CFPropertyList) {
    var copy: CFPropertyList = plist
    let st = withUnsafePointer(to: &copy) { ptr in
        AudioUnitSetProperty(
            unit,
            kAudioUnitProperty_ClassInfo,
            kAudioUnitScope_Global,
            0,
            ptr,
            UInt32(MemoryLayout<CFPropertyList>.size)
        )
    }
    // ClassInfo set often wants the CFDictionary itself by reference differently.
    // Use the standard pattern with Unmanaged.
    _ = st
}

func setClassInfoDict(_ unit: AudioUnit, _ dict: NSDictionary) {
    var cf: CFPropertyList = dict as CFPropertyList
    let size = UInt32(MemoryLayout<CFPropertyList>.size)
    let st = withUnsafePointer(to: &cf) { ptr in
        AudioUnitSetProperty(
            unit,
            kAudioUnitProperty_ClassInfo,
            kAudioUnitScope_Global,
            0,
            UnsafeRawPointer(ptr),
            size
        )
    }
    if st != noErr {
        // Alternate: some hosts pass the dictionary pointer directly
        let st2 = AudioUnitSetProperty(
            unit,
            kAudioUnitProperty_ClassInfo,
            kAudioUnitScope_Global,
            0,
            UnsafeRawPointer(Unmanaged.passUnretained(dict).toOpaque()),
            UInt32(MemoryLayout<CFPropertyList>.size)
        )
        if st2 != noErr { die("SetProperty ClassInfo failed: \(st)/\(st2)") }
    }
}

func rewriteEffectInStateData(_ data: Data, effect: Int, name: String) -> Data? {
    guard let xmlStart = data.range(of: Data("<?xml".utf8)) else { return nil }
    let header = data.subdata(in: data.startIndex..<xmlStart.lowerBound)
    var xmlData = data.subdata(in: xmlStart.lowerBound..<data.endIndex)
    guard var xml = String(data: xmlData, encoding: .utf8)
            ?? String(data: xmlData, encoding: .ascii) else { return nil }

    guard let tagRange = xml.range(of: #"<DEEPZ_STATE\b[^>]*?/?>"#, options: .regularExpression) else {
        return nil
    }
    var tag = String(xml[tagRange])
    if let r = tag.range(of: #"effect="\d+""#, options: .regularExpression) {
        tag.replaceSubrange(r, with: "effect=\"\(effect)\"")
    } else {
        tag = tag.replacingOccurrences(of: "<DEEPZ_STATE", with: "<DEEPZ_STATE effect=\"\(effect)\"")
    }
    if let r = tag.range(of: #"effectName="[^"]*""#, options: .regularExpression) {
        tag.replaceSubrange(r, with: "effectName=\"\(name)\"")
    } else {
        tag = tag.replacingOccurrences(of: "<DEEPZ_STATE", with: "<DEEPZ_STATE effectName=\"\(name)\"")
    }
    xml.replaceSubrange(tagRange, with: tag)
    guard let newXml = xml.data(using: .utf8) else { return nil }
    var out = Data()
    out.append(header)
    out.append(newXml)
    return out
}

func extractStateBlob(from classInfo: NSDictionary) -> (key: String, data: Data)? {
    for key in ["jucePluginState", "data"] {
        if let d = classInfo[key] as? Data { return (key, d) }
    }
    // Nested AU preset style
    return nil
}

func patchClassInfo(_ classInfo: NSDictionary, effect: Int, name: String) -> NSMutableDictionary {
    let out = NSMutableDictionary(dictionary: classInfo)
    // Prefer rewriting jucePluginState / data blobs that embed DEEPZ_STATE
    for key in ["jucePluginState", "data"] {
        if let d = out[key] as? Data, let patched = rewriteEffectInStateData(d, effect: effect, name: name) {
            out[key] = patched
        }
    }
    // Also name the preset
    out["name"] = String(format: "%02d-%@", effect, name)
    return out
}

func writeAUpreset(path: URL, classInfo: NSDictionary) throws {
    // Minimal AU preset: data + jucePluginState from ClassInfo if present
    let dict = NSMutableDictionary()
    if let d = classInfo["jucePluginState"] as? Data {
        dict["jucePluginState"] = d
        dict["data"] = d
    } else if let d = classInfo["data"] as? Data {
        dict["data"] = d
        dict["jucePluginState"] = d
    } else {
        // Persist whole class info under data as serialized plist bytes? Fall back to classInfo itself.
        let data = try PropertyListSerialization.data(fromPropertyList: classInfo, format: .binary, options: 0)
        dict["data"] = data
        dict["jucePluginState"] = data
    }
    if let n = classInfo["name"] { dict["name"] = n }
    let out = try PropertyListSerialization.data(fromPropertyList: dict, format: .binary, options: 0)
    try out.write(to: path, options: .atomic)
}

func readTemplatePreset(_ url: URL) -> NSDictionary {
    let data = try! Data(contentsOf: url)
    let obj = try! PropertyListSerialization.propertyList(from: data, options: [], format: nil)
    guard let dict = obj as? NSDictionary else { die("template not a dict") }
    return dict
}

// MARK: - main

var outDir = URL(fileURLWithPath: "compares/deepz/presets/au")
var only: Set<Int>? = nil
var templateURL: URL? = nil
var force = false

var args = CommandLine.arguments.dropFirst()
while let a = args.first {
    args = args.dropFirst()
    switch a {
    case "--out":
        guard let v = args.first else { die("--out needs path") }
        args = args.dropFirst()
        outDir = URL(fileURLWithPath: v)
    case "--only":
        guard let v = args.first else { die("--only needs list") }
        args = args.dropFirst()
        only = Set(v.split(separator: ",").compactMap { Int($0.trimmingCharacters(in: .whitespaces)) })
    case "--template":
        guard let v = args.first else { die("--template needs path") }
        args = args.dropFirst()
        templateURL = URL(fileURLWithPath: v)
    case "--force":
        force = true
    case "-h", "--help":
        print("export_deepz_factory_presets.swift --out DIR [--only 4,6,9] [--template PATH] [--force]")
        exit(0)
    default:
        die("unknown arg \(a)")
    }
}

try? FileManager.default.createDirectory(at: outDir, withIntermediateDirectories: true)

let home = FileManager.default.homeDirectoryForCurrentUser
let defaultTemplates = [
    home.appendingPathComponent("Library/Audio/Presets/Temecula DSP/DEEP/Z/Init Serial.aupreset"),
    home.appendingPathComponent("Library/Audio/Presets/Temecula DSP/DEEP:Z/ Init Serial.aupreset"),
]
let template = templateURL ?? defaultTemplates.first { FileManager.default.fileExists(atPath: $0.path) }
guard let templatePath = template else { die("No Init Serial template found; pass --template") }
print("template: \(templatePath.path)")

let templatePreset = readTemplatePreset(templatePath)
let unit = loadAU()
defer { AudioComponentInstanceDispose(unit) }

let stInit = AudioUnitInitialize(unit)
if stInit != noErr { die("AudioUnitInitialize failed: \(stInit)") }

var targets = allEffects
if let only {
    targets = allEffects.filter { only.contains($0.number) }
}

var exported = 0
var skipped = 0
for spec in targets {
    let filename = String(format: "%02d-%@.aupreset", spec.number, spec.name)
    let dest = outDir.appendingPathComponent(filename)
    if !force && FileManager.default.fileExists(atPath: dest.path) {
        print("skip exists \(filename)")
        skipped += 1
        continue
    }

    // Start from template ClassInfo-like dict (aupreset keys)
    let patchedPreset = patchClassInfo(templatePreset, effect: spec.number, name: spec.name)

    // Build ClassInfo for AU: type/subtype/manufacturer + data blob
    let classInfo = NSMutableDictionary()
    classInfo["type"] = fourCC("aumf")
    classInfo["subtype"] = fourCC("DPPL")
    classInfo["manufacturer"] = fourCC("TDSP")
    classInfo["name"] = filename
    if let d = patchedPreset["data"] as? Data {
        classInfo["data"] = d
    } else if let d = patchedPreset["jucePluginState"] as? Data {
        classInfo["data"] = d
    } else {
        die("template missing data blob")
    }

    setClassInfoDict(unit, classInfo)

    // Read back expanded state
    let restored = getClassInfo(unit) as! NSDictionary
    let outDict = NSMutableDictionary()
    if let d = restored["jucePluginState"] as? Data {
        outDict["jucePluginState"] = d
        outDict["data"] = d
    } else if let d = restored["data"] as? Data {
        outDict["data"] = d
        outDict["jucePluginState"] = d
    } else {
        die("restored ClassInfo missing data for \(spec.name)")
    }
    outDict["name"] = String(format: "%02d-%@", spec.number, spec.name)

    // Verify effect number stuck
    if let d = outDict["data"] as? Data,
       let tag = String(data: d, encoding: .utf8) ?? String(data: d, encoding: .ascii),
       tag.contains("effect=\"\(spec.number)\"") || d.range(of: Data("effect=\"\(spec.number)\"".utf8)) != nil {
        // ok
    } else if let d = outDict["data"] as? Data {
        let m = String(data: d, encoding: .ascii) ?? ""
        if !m.contains("effect=\"\(spec.number)\"") && d.range(of: Data("effect=\"\(spec.number)\"".utf8)) == nil {
            fputs("warning: \(filename) may not contain effect=\(spec.number) after round-trip\n", stderr)
        }
    }

    do {
        try writeAUpreset(path: dest, classInfo: outDict)
        // Confirm on disk
        let check = try Data(contentsOf: dest)
        if check.range(of: Data("effect=\"\(spec.number)\"".utf8)) == nil {
            fputs("warning: wrote \(filename) but effect=\(spec.number) not found in file bytes\n", stderr)
        }
        print("wrote \(filename)")
        exported += 1
    } catch {
        die("write \(filename): \(error)")
    }
}

print("done: exported \(exported), skipped existing \(skipped)")
