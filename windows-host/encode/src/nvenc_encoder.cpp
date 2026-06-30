// NVENC HEVC/H.264 encoder (M1.3), Windows only. Encodes a BGRA D3D11 texture to an elementary stream.
// Implemented against the verified NVENC recipe in docs/ARCHITECTURE.md (Video Codec SDK 12/13,
// nvEncodeAPI.h). NVENC takes BGRA directly as NV_ENC_BUFFER_FORMAT_ARGB and does RGB->YUV internally.
//
// ⛔ Not compiled/tested in WSL (needs the NVIDIA Video Codec SDK header + an RTX GPU). Built on Windows
//    via CMake when NVENC_INCLUDE_DIR points at nvEncodeAPI.h; verified on hardware by encode_sandbox.
//    Every struct uses the SDK's named *_VER macro (indices differ across SDK 12/13) — never a literal.
#if defined(_WIN32)

#include "td/encode/encoder.hpp"

#include <d3d11.h>
#include <wrl/client.h>
#include <nvEncodeAPI.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

namespace td::encode {
namespace {

bool ok(NVENCSTATUS s) { return s == NV_ENC_SUCCESS; }

GUID CodecGuid(Codec c) {
  return c == Codec::H264 ? NV_ENC_CODEC_H264_GUID : NV_ENC_CODEC_HEVC_GUID;
}

class NvencEncoder final : public IEncoder {
 public:
  ~NvencEncoder() override { Shutdown(); }

  EncodeStatus Initialize(void* d3d11_device, const EncoderConfig& cfg) override {
    Shutdown();  // idempotent re-Initialize
    if (!d3d11_device) return EncodeStatus::UnsupportedDevice;
    device_ = static_cast<ID3D11Device*>(d3d11_device);
    device_->GetImmediateContext(ctx_.GetAddressOf());
    cfg_ = cfg;

    lib_ = LoadLibraryW(L"nvEncodeAPI64.dll");
    if (!lib_) return EncodeStatus::SdkNotFound;
    using PFnCreate = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);
    auto create = reinterpret_cast<PFnCreate>(GetProcAddress(lib_, "NvEncodeAPICreateInstance"));
    if (!create) return EncodeStatus::SdkNotFound;

    std::memset(&fn_, 0, sizeof(fn_));
    fn_.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (!ok(create(&fn_))) return EncodeStatus::SessionInitFailed;

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS op;
    std::memset(&op, 0, sizeof(op));
    op.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    op.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    op.device = device_.Get();
    op.apiVersion = NVENCAPI_VERSION;
    if (!ok(fn_.nvEncOpenEncodeSessionEx(&op, &enc_)) || !enc_)
      return EncodeStatus::SessionInitFailed;

    // Low-latency preset config, then override for our streaming profile.
    const NV_ENC_TUNING_INFO tuning = NV_ENC_TUNING_INFO_LOW_LATENCY;
    const GUID preset = NV_ENC_PRESET_P4_GUID;
    const GUID codec = CodecGuid(cfg.codec);

    NV_ENC_PRESET_CONFIG pc;
    std::memset(&pc, 0, sizeof(pc));
    pc.version = NV_ENC_PRESET_CONFIG_VER;
    pc.presetCfg.version = NV_ENC_CONFIG_VER;  // the embedded config also needs its version
    if (!ok(fn_.nvEncGetEncodePresetConfigEx(enc_, codec, preset, tuning, &pc)))
      return EncodeStatus::SessionInitFailed;

    const RateControlPlan plan = PlanRateControl(cfg);
    NV_ENC_CONFIG ec = pc.presetCfg;
    ec.gopLength = NVENC_INFINITE_GOPLENGTH;
    ec.frameIntervalP = static_cast<int>(plan.frame_interval_p);  // 1 => no B-frames
    ec.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    ec.rcParams.multiPass = NV_ENC_MULTI_PASS_DISABLED;
    ec.rcParams.averageBitRate = plan.average_bitrate_kbps * 1000;
    ec.rcParams.maxBitRate = plan.max_bitrate_kbps * 1000;
    ec.rcParams.vbvBufferSize = plan.vbv_buffer_size_bits;
    ec.rcParams.vbvInitialDelay = plan.vbv_initial_delay_bits;
    if (cfg.codec == Codec::H264) {
      ec.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
      ec.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
    } else {
      ec.encodeCodecConfig.hevcConfig.idrPeriod = NVENC_INFINITE_GOPLENGTH;
      ec.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;  // inline VPS/SPS/PPS for a raw elementary stream
    }

    NV_ENC_INITIALIZE_PARAMS ip;
    std::memset(&ip, 0, sizeof(ip));
    ip.version = NV_ENC_INITIALIZE_PARAMS_VER;
    ip.encodeGUID = codec;
    ip.presetGUID = preset;
    ip.tuningInfo = tuning;  // must match the preset query
    ip.encodeWidth = ip.darWidth = cfg.width;
    ip.encodeHeight = ip.darHeight = cfg.height;
    ip.frameRateNum = cfg.fps;
    ip.frameRateDen = 1;
    ip.enablePTD = 1;          // NVENC chooses picture types
    ip.enableEncodeAsync = 0;  // synchronous: no completion events
    ip.encodeConfig = &ec;
    if (!ok(fn_.nvEncInitializeEncoder(enc_, &ip))) return EncodeStatus::SessionInitFailed;

    NV_ENC_CREATE_BITSTREAM_BUFFER cb;
    std::memset(&cb, 0, sizeof(cb));
    cb.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    if (!ok(fn_.nvEncCreateBitstreamBuffer(enc_, &cb))) return EncodeStatus::SessionInitFailed;
    bitstream_ = cb.bitstreamBuffer;

    // The encoder owns its NVENC input texture (BGRA, DEFAULT, RENDER_TARGET, NOT shared); each frame
    // we CopyResource the captured texture into it, so register/map targets a stable resource.
    D3D11_TEXTURE2D_DESC td{};
    td.Width = cfg.width;
    td.Height = cfg.height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, input_tex_.GetAddressOf())))
      return EncodeStatus::RegisterFailed;

    NV_ENC_REGISTER_RESOURCE rr;
    std::memset(&rr, 0, sizeof(rr));
    rr.version = NV_ENC_REGISTER_RESOURCE_VER;
    rr.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    rr.width = cfg.width;
    rr.height = cfg.height;
    rr.subResourceIndex = 0;
    rr.resourceToRegister = input_tex_.Get();
    rr.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;  // == DXGI_FORMAT_B8G8R8A8_UNORM
    rr.bufferUsage = NV_ENC_INPUT_IMAGE;
    if (!ok(fn_.nvEncRegisterResource(enc_, &rr))) return EncodeStatus::RegisterFailed;
    registered_ = rr.registeredResource;

    initialized_ = true;
    return EncodeStatus::Ok;
  }

  EncodeStatus EncodeFrame(void* d3d11_texture, std::uint64_t timestamp, bool force_idr,
                           const PacketCallback& on_packet) override {
    if (!initialized_) return EncodeStatus::NotInitialized;
    if (!d3d11_texture) return EncodeStatus::EncodeFailed;

    // Bring the captured frame into our registered input texture.
    ctx_->CopyResource(input_tex_.Get(), static_cast<ID3D11Texture2D*>(d3d11_texture));

    NV_ENC_MAP_INPUT_RESOURCE mp;
    std::memset(&mp, 0, sizeof(mp));
    mp.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    mp.registeredResource = registered_;
    if (!ok(fn_.nvEncMapInputResource(enc_, &mp))) return EncodeStatus::EncodeFailed;

    NV_ENC_PIC_PARAMS pp;
    std::memset(&pp, 0, sizeof(pp));
    pp.version = NV_ENC_PIC_PARAMS_VER;
    pp.inputWidth = cfg_.width;
    pp.inputHeight = cfg_.height;
    pp.inputBuffer = mp.mappedResource;
    pp.bufferFmt = mp.mappedBufferFmt;
    pp.outputBitstream = bitstream_;
    pp.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    pp.completionEvent = nullptr;  // sync mode
    pp.inputTimeStamp = timestamp;
    pp.frameIdx = static_cast<std::uint32_t>(frame_index_);
    if (force_idr) pp.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;

    const NVENCSTATUS s = fn_.nvEncEncodePicture(enc_, &pp);
    EncodeStatus result = EncodeStatus::Ok;
    if (s == NV_ENC_SUCCESS) {
      EmitLocked(timestamp, on_packet);
    } else if (s != NV_ENC_ERR_NEED_MORE_INPUT) {  // buffered (won't happen with IPPP/no lookahead)
      result = EncodeStatus::EncodeFailed;
    }
    fn_.nvEncUnmapInputResource(enc_, mp.mappedResource);
    return result;
  }

  EncodeStatus Flush(const PacketCallback& on_packet) override {
    if (!initialized_) return EncodeStatus::NotInitialized;
    (void)on_packet;  // Nothing to drain: with frameIntervalP=1 and no lookahead, every EncodeFrame
                      // already emitted its packet, so EOS yields no NEW bitstream. Locking the
                      // persistent buffer here would re-emit the last frame — so we don't.
    NV_ENC_PIC_PARAMS eos;
    std::memset(&eos, 0, sizeof(eos));
    eos.version = NV_ENC_PIC_PARAMS_VER;
    eos.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    eos.completionEvent = nullptr;  // null input/output signals end-of-stream to the encoder
    fn_.nvEncEncodePicture(enc_, &eos);
    return EncodeStatus::Ok;
  }

  void Shutdown() override {
    if (enc_) {
      if (registered_) { fn_.nvEncUnregisterResource(enc_, registered_); registered_ = nullptr; }
      if (bitstream_) { fn_.nvEncDestroyBitstreamBuffer(enc_, bitstream_); bitstream_ = nullptr; }
      fn_.nvEncDestroyEncoder(enc_);
      enc_ = nullptr;
    }
    input_tex_.Reset();
    ctx_.Reset();
    device_.Reset();
    if (lib_) { FreeLibrary(lib_); lib_ = nullptr; }
    initialized_ = false;
    frame_index_ = 0;
  }

 private:
  void EmitLocked(std::uint64_t timestamp, const PacketCallback& on_packet) {
    NV_ENC_LOCK_BITSTREAM lb;
    std::memset(&lb, 0, sizeof(lb));
    lb.version = NV_ENC_LOCK_BITSTREAM_VER;
    lb.outputBitstream = bitstream_;
    lb.doNotWait = 0;
    if (!ok(fn_.nvEncLockBitstream(enc_, &lb))) return;
    if (on_packet) {
      EncodedPacket pkt;
      pkt.data = static_cast<const std::uint8_t*>(lb.bitstreamBufferPtr);
      pkt.size = lb.bitstreamSizeInBytes;
      pkt.is_keyframe = (lb.pictureType == NV_ENC_PIC_TYPE_IDR);
      pkt.timestamp = timestamp;
      pkt.frame_index = frame_index_;
      on_packet(pkt);  // data valid only until Unlock below — the callback must copy it out
    }
    fn_.nvEncUnlockBitstream(enc_, bitstream_);
    ++frame_index_;
  }

  HMODULE lib_ = nullptr;
  NV_ENCODE_API_FUNCTION_LIST fn_{};
  void* enc_ = nullptr;
  NV_ENC_OUTPUT_PTR bitstream_ = nullptr;
  NV_ENC_REGISTERED_PTR registered_ = nullptr;
  ComPtr<ID3D11Device> device_;  // own a ref (don't rely on ctx_ to keep the device alive)
  ComPtr<ID3D11DeviceContext> ctx_;
  ComPtr<ID3D11Texture2D> input_tex_;
  EncoderConfig cfg_{};
  bool initialized_ = false;
  std::uint64_t frame_index_ = 0;
};

}  // namespace

std::unique_ptr<IEncoder> MakeNvencEncoder() { return std::make_unique<NvencEncoder>(); }

}  // namespace td::encode

#endif  // _WIN32
