// SPDX-License-Identifier: GPL-3.0-or-later
// Task/mesh shader translator for NVN 2 native (Ampere) task and mesh stages, which the Ryujinx-based totk_xlate
// cannot translate. It links Eglantine's shader recompiler (GPL; refs/eglantine, src/shader_recompiler) and follows
// its environment and lowering layout: shader_environment.cpp, GenericEnvironment::LowerNativeCode, and
// frontend/maxwell/translate_program.cpp at 724aede4ea51fae8224cdd02549cdbdf7ceecd25.
//
// Protocol (same as tools/xlate/Program.cs; stdout carries replies only, diagnostics go to stderr):
//   mesh_xlate --serve     one request per stdin line: {"job": {...}, "out": "<dir>"}; one reply line per request:
//                          "ok <key>" or "fail <key> <message>".
//   mesh_xlate <job.json> <outdir>   one job, for tests.
//   job: { "key", "stage": "ts|ms", "control": path to the NVN 2 control block, "code": path to the native code
//          (0x80-byte SPH followed by instructions), "tex": {slot: type}, "img": {slot: {type, fmt}} }
//   Output: <out>/<key>.spv and <out>/<key>.res.json, in totk_xlate's reflection format (gfx_shader.c, reflect),
//   plus a "mesh" object: workgroup size, payload words, max vertices and primitives, output topology.
//
// Bindings: set 0, binding 0 is left to the runtime's support buffer as with totk_xlate; resources follow in
// Eglantine's emission order (backend/spirv/spirv_emit_context.cpp, EmitContext::EmitContext): constant buffers,
// storage buffers, texture buffers, image buffers, textures, images, each descriptor taking `count` bindings.
//
// Texture handles: Eglantine reads the handle from the texture bank (c2) and asks for its type. ReadCbufValue on
// that bank returns a tag carrying the byte offset, and ReadTextureType turns the offset into totk_xlate's slot
// (combined handles at c2+0x20, images at c2+0x120, separate textures at c2+0x168, 8 bytes each) and looks the
// type up in the job, as Program.cs QuerySamplerType does. Separate texture/sampler handle pairs are rejected.
//
// Mesh metadata registers: the three words Eglantine needs (Environment::mesh_registers) are inferred from control
// fields (kind at +0x56, threads/payload at +0x58, vertices/primitives at +0x60, topology at +0x5f). This is a
// hypothesis about the NVN 2 control layout, not a verified driver ABI; the reply marks it "inferred_registers".
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "shader_recompiler/environment.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ampere/program_capture.h"
#include "shader_recompiler/frontend/ampere/program_address_map.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"

namespace {

// ---- minimal JSON: objects, strings, numbers, booleans, null; arrays are parsed and kept as raw text ----
struct Json {
    enum Kind { Null, Bool, Number, String, Object, Array } kind = Null;
    double number = 0;
    bool boolean = false;
    std::string text;
    std::map<std::string, Json> members;
    std::vector<Json> items;
    const Json* get(const std::string& k) const {
        auto it = members.find(k);
        return it == members.end() ? nullptr : &it->second;
    }
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    Json parse() {
        Json v = value();
        space();
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }
private:
    const std::string& s_;
    size_t i_ = 0;
    [[noreturn]] void fail(const char* what) { throw Shader::RuntimeError("JSON: {} at {}", what, i_); }
    void space() { while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) i_++; }
    bool eat(char c) { space(); if (i_ < s_.size() && s_[i_] == c) { i_++; return true; } return false; }
    std::string str() {
        if (!eat('"')) fail("expected string");
        std::string out;
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c == '\\') {
                if (i_ >= s_.size()) fail("bad escape");
                char e = s_[i_++];
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i_ + 4 > s_.size()) fail("bad \\u escape");
                    unsigned cp = std::stoul(s_.substr(i_, 4), nullptr, 16);
                    i_ += 4;
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) out += static_cast<char>(0xc0 | cp >> 6), out += static_cast<char>(0x80 | (cp & 63));
                    else out += static_cast<char>(0xe0 | cp >> 12), out += static_cast<char>(0x80 | ((cp >> 6) & 63)),
                         out += static_cast<char>(0x80 | (cp & 63));
                    break;
                }
                default: out += e;
                }
            } else out += c;
        }
        if (!eat('"')) fail("unterminated string");
        return out;
    }
    Json value() {
        space();
        Json v;
        if (i_ >= s_.size()) fail("unexpected end");
        char c = s_[i_];
        if (c == '{') {
            i_++;
            v.kind = Json::Object;
            if (eat('}')) return v;
            do {
                std::string k = str();
                if (!eat(':')) fail("expected ':'");
                v.members[k] = value();
            } while (eat(','));
            if (!eat('}')) fail("expected '}'");
        } else if (c == '[') {
            i_++;
            v.kind = Json::Array;
            if (eat(']')) return v;
            do v.items.push_back(value()); while (eat(','));
            if (!eat(']')) fail("expected ']'");
        } else if (c == '"') {
            v.kind = Json::String, v.text = str();
        } else if (!s_.compare(i_, 4, "true")) {
            v.kind = Json::Bool, v.boolean = true, i_ += 4;
        } else if (!s_.compare(i_, 5, "false")) {
            v.kind = Json::Bool, i_ += 5;
        } else if (!s_.compare(i_, 4, "null")) {
            i_ += 4;
        } else {
            size_t end;
            v.kind = Json::Number, v.number = std::stod(s_.substr(i_), &end);
            if (!end) fail("bad value");
            i_ += end;
        }
        return v;
    }
};

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (c == '\n') o += "\\n";
        else if (static_cast<unsigned char>(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o + "\"";
}

std::vector<u8> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Shader::RuntimeError("Cannot read {}", path);
    return {std::istreambuf_iterator<char>(f), {}};
}

u32 word(const std::vector<u8>& v, size_t offset) {
    if (offset + 4 > v.size()) throw Shader::RuntimeError("Read outside supplied bytes: {:#x}", offset);
    u32 value;
    std::memcpy(&value, v.data() + offset, 4);
    return value;
}
u16 half(const std::vector<u8>& v, size_t offset) {
    if (offset + 2 > v.size()) throw Shader::RuntimeError("Read outside supplied bytes: {:#x}", offset);
    u16 value;
    std::memcpy(&value, v.data() + offset, 2);
    return value;
}

constexpr u32 TEXTURE_BANK = 2;
constexpr u32 HANDLE_TAG = 0x5a000000u;   // ReadCbufValue result on the texture bank: tag | byte offset

// totk_xlate's c2 slot of a texture/image handle (tools/xlate/Program.cs, TexSlot and HandleTable).
int handle_slot(u32 bytes, bool& image) {
    image = false;
    if (bytes >= 0x120 && bytes < 0x168) { image = true; return (bytes - 0x120) % 8 ? -1 : static_cast<int>((bytes - 0x120) / 8); }
    if (bytes >= 0x20 && bytes < 0x120) return (bytes - 0x20) % 8 ? -1 : static_cast<int>((bytes - 0x20) / 8);
    if (bytes >= 0x168 && bytes < 0x568) return (bytes - 0x168) % 8 ? -1 : static_cast<int>((bytes - 0x168) / 8);
    return -1;
}

class CapturedEnvironment final : public Shader::Environment {
public:
    std::vector<u64> instructions;
    std::vector<u8> source;
    u32 constants_begin{}, constants_end{}, shared{};
    std::array<u32, 3> registers{};
    u32 topology{};
    const Json& job;
    std::vector<std::string> notes;
    std::vector<int> tex_queried, img_queried;

    CapturedEnvironment(const std::vector<u8>& control, std::vector<u8> bytes, const Json& job_)
        : source(std::move(bytes)), job(job_) {
        if (word(control, 0) != 0x98761234 || word(control, 4) != 0xc9)
            throw Shader::RuntimeError("Not a native NVN 2 control block");
        const u32 kind = half(control, 0x56);
        if (kind != 6 && kind != 7) throw Shader::RuntimeError("Not a task or mesh stage: kind {}", kind);
        stage = kind == 6 ? Shader::Stage::Task : Shader::Stage::Mesh;
        const u16 threads = half(control, 0x58), payload = half(control, 0x5a);
        const u16 vertices = half(control, 0x60), primitives = half(control, 0x62);
        topology = control.size() > 0x5f ? control[0x5f] : 0;
        if (stage == Shader::Stage::Mesh && topology != 3)
            throw Shader::RuntimeError("Mesh output topology {} is not triangles", topology);
        const u32 lines = payload ? (payload + 127u) / 128u : 1u;
        registers = {stage == Shader::Stage::Task ? 4u : 4u | (u32{vertices} << 4) | (u32{primitives} << 13),
                     u32{threads} << 20, u32{threads} | (lines << 12) | (lines << 22)};
        mesh_registers = registers;
        if (source.size() < 0x80) throw Shader::RuntimeError("Missing native graphics header");
        std::memcpy(&sph, source.data(), sizeof sph);
        is_proprietary_driver = true;
        constants_begin = word(control, 0x40);
        constants_end = word(control, 0x44);
        shared = word(control, 0x64);
        if (constants_begin > constants_end || constants_end > source.size())
            throw Shader::RuntimeError("Inline constant range outside supplied bytes");
        const u32 end = word(control, 0x38);
        if (end > source.size()) throw Shader::RuntimeError("Native code length outside supplied bytes");
        std::vector<Shader::Ampere::Instruction> native;
        Shader::Ampere::ProgramCaptureState capture;
        for (u32 at = 0x80; at + 16 <= end; at += 16) {
            Shader::Ampere::Instruction ins;
            std::memcpy(ins.words.data(), source.data() + at, 16);
            native.push_back(ins);
            auto status = capture.Observe(ins, static_cast<u32>(native.size() - 1));
            if (status == Shader::Ampere::ProgramCaptureState::Result::InvalidTarget)
                throw Shader::RuntimeError("Native branch outside capture");
            if (status == Shader::Ampere::ProgramCaptureState::Result::Complete) break;
        }
        Shader::Ampere::LoweringOptions options;
        options.tensor = true;
        // The stage comes from the control block, so a task/mesh SPH type 3 is not lowered as tessellation.
        auto lowered = Shader::Ampere::LowerToMaxwell(native, options);
        if (!lowered) throw Shader::RuntimeError("Lowering at {:#x}: {}", lowered.error_offset, lowered.error);
        instructions.resize(sizeof sph / 8);
        std::memcpy(instructions.data(), &sph, sizeof sph);
        for (u64 ins : lowered.instructions) {
            if ((instructions.size() * 8) % 32 == 0) instructions.push_back(0);
            instructions.push_back(ins);
        }
        instructions.push_back(0);
        Shader::Ampere::AppendNativeAddressMap(instructions, lowered.source_instruction_offsets);
    }
    u64 ReadInstruction(u32 address) override {
        if (address % 8 || address / 8 >= instructions.size())
            throw Shader::RuntimeError("Instruction out of range {:#x}", address);
        return instructions[address / 8];
    }
    u32 TranslateProgramAddress(u32 address) const override {
        if (auto at = Shader::Ampere::NativeProgramAddress(instructions, address)) return *at;
        throw Shader::RuntimeError("Native indirect target out of range {:#x}", address);
    }
    bool IsNativeProgram() const override { return true; }
    bool HasNativeInstructions() const override { return true; }
    bool IsCbufStorageBufferPointer(u32 bank, u32 offset) override {
        const u32 base = stage == Shader::Stage::Task ? 0x110 : 0x310;
        return bank == 0 && offset >= base && offset < base + 16 * 16 && (offset - base) % 16 == 0;
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override {
        if (bank == 1 && constants_begin + offset + 4 <= constants_end) return word(source, constants_begin + offset);
        if (bank == TEXTURE_BANK && offset < 0x800) return HANDLE_TAG | offset;
        throw Shader::RuntimeError("Missing static constant c{}+{:#x}", bank, offset);
    }
    // Type of the texture whose handle Eglantine read from the texture bank, from the job's slot tables.
    std::string slot_type(u32 raw, bool want_image, bool* depth = nullptr) {
        if ((raw & 0xff000000u) != HANDLE_TAG)
            throw Shader::RuntimeError("Texture handle {:#x} is not a single c{} word (separate pairs unsupported)", raw,
                                       TEXTURE_BANK);
        bool image;
        const int slot = handle_slot(raw & 0xffffffu, image);
        if (slot < 0) throw Shader::RuntimeError("Texture handle at c{}+{:#x} outside the handle tables", TEXTURE_BANK,
                                                 raw & 0xffffffu);
        (image || want_image ? img_queried : tex_queried).push_back(slot);
        const Json* table = job.get(image ? "img" : "tex");
        const Json* entry = table ? table->get(std::to_string(slot)) : nullptr;
        std::string t = entry ? (entry->kind == Json::Object ? (entry->get("type") ? entry->get("type")->text : "")
                                                              : entry->text)
                              : "";
        if (t.empty()) {
            notes.push_back("texture slot " + std::to_string(slot) + " has no bound type; 2D assumed");
            t = "2d";
        }
        if (t.size() > 6 && t.compare(t.size() - 6, 6, "_depth") == 0) {
            t.resize(t.size() - 6);
            if (depth) *depth = true;
        }
        return t;
    }
    Shader::TextureType ReadTextureType(u32 raw) override {
        const std::string t = slot_type(raw, false);
        if (t == "1d") return Shader::TextureType::Color1D;
        if (t == "1darray") return Shader::TextureType::ColorArray1D;
        if (t == "2darray") return Shader::TextureType::ColorArray2D;
        if (t == "3d") return Shader::TextureType::Color3D;
        if (t == "cube") return Shader::TextureType::ColorCube;
        if (t == "cubearray") return Shader::TextureType::ColorArrayCube;
        if (t == "buffer") return Shader::TextureType::Buffer;
        if (t == "2d") return Shader::TextureType::Color2D;
        throw Shader::RuntimeError("Unsupported texture type '{}'", t);
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32 raw) override {
        bool depth = false;
        slot_type(raw, false, &depth);
        return depth ? Shader::TexturePixelFormat::D32_FLOAT : Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32 raw) override {
        bool image;
        const int slot = handle_slot(raw & 0xffffffu, image);
        const Json* table = job.get(image ? "img" : "tex");
        const Json* entry = slot >= 0 && table ? table->get(std::to_string(slot)) : nullptr;
        const Json* fmt = entry && entry->kind == Json::Object ? entry->get("fmt") : nullptr;
        return fmt && (fmt->text.find("Uint") != std::string::npos || fmt->text.find("Sint") != std::string::npos);
    }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return TEXTURE_BANK; }
    u32 LocalMemorySize() const override { return static_cast<u32>(sph.LocalMemorySize()); }
    u32 SharedMemorySize() const override { return shared; }
    std::array<u32, 3> WorkgroupSize() const override { return {1, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override { return std::nullopt; }
    void Dump(u64, u64) override {}
};

// Ryujinx SamplerType (tools/vendor/ryujinx/src/Ryujinx.Graphics.Shader/SamplerType.cs), as the runtime reads it.
int sampler_type(Shader::TextureType t, bool depth, bool ms) {
    int v;
    switch (t) {
    case Shader::TextureType::Color1D: v = 1; break;
    case Shader::TextureType::ColorArray1D: v = 1 | 0x100; break;
    case Shader::TextureType::Color2D: case Shader::TextureType::Color2DRect: v = 3; break;
    case Shader::TextureType::ColorArray2D: v = 3 | 0x100; break;
    case Shader::TextureType::Color3D: v = 4; break;
    case Shader::TextureType::ColorCube: v = 5; break;
    case Shader::TextureType::ColorArrayCube: v = 5 | 0x100; break;
    case Shader::TextureType::Buffer: v = 2; break;
    default: throw Shader::RuntimeError("Unmapped texture type {}", static_cast<int>(t));
    }
    return v | (ms ? 0x200 : 0) | (depth ? 0x400 : 0);
}

std::string translate(const Json& job, const std::string& out_dir) {
    const Json* key = job.get("key");
    const Json* stage = job.get("stage");
    const Json* control = job.get("control");
    const Json* code = job.get("code");
    if (!key || !stage || !control || !code) throw Shader::RuntimeError("job needs key, stage, control and code");
    if (stage->text != "ts" && stage->text != "ms") throw Shader::RuntimeError("stage must be ts or ms");
    CapturedEnvironment env(read_file(control->text), read_file(code->text), job);
    if ((env.ShaderStage() == Shader::Stage::Task) != (stage->text == "ts"))
        throw Shader::RuntimeError("job stage {} disagrees with the control block", stage->text);
    Shader::ObjectPool<Shader::IR::Inst> inst;
    Shader::ObjectPool<Shader::IR::Block> blocks;
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow;
    Shader::Maxwell::Flow::CFG cfg(env, flow, Shader::Maxwell::Location(0x50));
    Shader::HostTranslateInfo host{};
    host.support_float64 = true, host.support_int64 = true, host.min_ssbo_alignment = 16;
    host.support_conditional_barrier = true;
    auto program = Shader::Maxwell::TranslateProgram(inst, blocks, env, cfg, host);
    const Shader::Info& info = program.info;
    if (info.uses_rescaling_uniform || info.uses_render_area)
        throw Shader::RuntimeError("shader needs rescaling/render-area push constants the runtime does not provide");
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010500, profile.support_int64 = true;
    profile.support_vertex_instance_id = true, profile.support_vote = true;
    profile.support_shuffle_relative = true, profile.max_subgroup_size = 32;
    profile.min_ssbo_alignment = 16, profile.max_user_clip_distances = 8;
    profile.unified_descriptor_binding = true, profile.support_descriptor_aliasing = false;
    Shader::RuntimeInfo runtime{};
    runtime.convert_depth_mode = true;
    Shader::Backend::Bindings bindings{};
    bindings.unified = 1;   // binding 0 stays the runtime's support buffer
    auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);

    // Reflection in emission order, starting at binding 1.
    u32 binding = 1;
    std::ostringstream cb, sb, tx, im;
    const char* sep = "";
    for (const auto& d : info.constant_buffer_descriptors) {
        for (u32 i = 0; i < d.count; i++, binding++)
            cb << sep << "{\"binding\": " << binding << ", \"slot\": " << d.index + i << "}", sep = ", ";
    }
    sep = "";
    int ssbo = 0;
    for (const auto& d : info.storage_buffers_descriptors) {
        if (d.count != 1) throw Shader::RuntimeError("storage buffer array of {} unsupported", d.count);
        sb << sep << "{\"binding\": " << binding++ << ", \"slot\": " << ssbo++ << ", \"cb_slot\": " << d.cbuf_index
           << ", \"cb_offset\": " << d.cbuf_offset / 4 << ", \"flags\": " << (d.is_written ? 1 : 0) << "}";
        sep = ", ";
    }
    if (!info.texture_buffer_descriptors.empty() || !info.image_buffer_descriptors.empty())
        throw Shader::RuntimeError("texture/image buffers are not mapped yet");
    sep = "";
    for (const auto& d : info.texture_descriptors) {
        if (d.has_secondary) throw Shader::RuntimeError("separate texture/sampler handles are not mapped yet");
        tx << sep << "{\"binding\": " << binding << ", \"type\": " << sampler_type(d.type, d.is_depth, d.is_multisample)
           << ", \"format\": \"Unknown\", \"cbuf_slot\": " << d.cbuf_index << ", \"handle\": " << d.cbuf_offset / 4
           << ", \"array\": " << d.count << ", \"separate\": false, \"flags\": 0}";
        binding += d.count, sep = ", ";
    }
    sep = "";
    for (const auto& d : info.image_descriptors) {
        im << sep << "{\"binding\": " << binding << ", \"type\": " << sampler_type(d.type, false, false)
           << ", \"format\": \"Unknown\", \"cbuf_slot\": " << d.cbuf_index << ", \"handle\": " << d.cbuf_offset / 4
           << ", \"array\": " << d.count << ", \"separate\": false, \"flags\": " << (d.is_written ? 4 : 0) << "}";
        binding += d.count, sep = ", ";
    }
    auto ints = [](const std::vector<int>& v) {
        std::string s;
        for (size_t i = 0; i < v.size(); i++) s += (i ? ", " : "") + std::to_string(v[i]);
        return s;
    };
    std::string notes;
    for (size_t i = 0; i < env.notes.size(); i++) notes += (i ? ", " : "") + quote(env.notes[i]);
    std::ostringstream res;
    res << "{\n  \"key\": " << quote(key->text) << ",\n  \"stage\": " << quote(stage->text) << ",\n  \"notes\": [" << notes
        << "],\n  \"cbufs\": [" << cb.str() << "],\n  \"sbufs\": [" << sb.str() << "],\n  \"textures\": [" << tx.str()
        << "],\n  \"images\": [" << im.str() << "],\n  \"uses_frag_coord\": false,\n  \"frag_out_map\": 0,\n"
        << "  \"storage_candidates_version\": 0,\n  \"tex_queried\": [" << ints(env.tex_queried)
        << "],\n  \"img_queried\": [" << ints(env.img_queried) << "],\n  \"mesh\": {\"version\": 1, \"local\": [" << program.workgroup_size[0]
        << ", " << program.workgroup_size[1] << ", " << program.workgroup_size[2] << "], \"payload_words\": "
        << program.mesh.payload_words << ", \"max_vertices\": " << program.mesh.max_vertices << ", \"max_primitives\": "
        << program.mesh.max_primitives << ", \"topology\": " << env.topology << ", \"inferred_registers\": true, "
        << "\"registers\": [" << env.registers[0] << ", " << env.registers[1] << ", " << env.registers[2] << "]}\n}\n";
    const std::string base = out_dir + "/" + key->text;
    std::ofstream spv(base + ".spv", std::ios::binary);
    spv.write(reinterpret_cast<const char*>(spirv.data()), static_cast<std::streamsize>(spirv.size() * 4));
    if (!spv) throw Shader::RuntimeError("Cannot write {}.spv", base);
    spv.close();
    std::ofstream json(base + ".res.json", std::ios::binary);
    json << res.str();
    if (!json) throw Shader::RuntimeError("Cannot write {}.res.json", base);
    return key->text;
}

std::string one_line(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "--serve")) {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty() || line == "\r") continue;
            std::string key = "?";
            try {
                Json request = Parser(line).parse();
                const Json* job = request.get("job");
                const Json* out = request.get("out");
                if (!job || !out) throw Shader::RuntimeError("request needs job and out");
                if (const Json* k = job->get("key")) key = k->text;
                translate(*job, out->text);
                std::cout << "ok " << key << std::endl;
            } catch (const std::exception& e) {
                std::cout << "fail " << key << " " << one_line(e.what()) << std::endl;
            }
        }
        return 0;
    }
    if (argc == 3) {
        try {
            std::ifstream f(argv[1], std::ios::binary);
            std::string text{std::istreambuf_iterator<char>(f), {}};
            Json job = Parser(text).parse();
            std::printf("ok %s\n", translate(job, argv[2]).c_str());
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "fail %s\n", e.what());
            return 1;
        }
    }
    std::fprintf(stderr, "usage: mesh_xlate --serve | mesh_xlate <job.json> <outdir>\n");
    return 2;
}
