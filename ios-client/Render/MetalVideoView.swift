// Metal NV12 renderer (M3): wraps a decoded CVPixelBuffer's luma + chroma planes as Metal textures and
// draws them with the BT.709 video-range shader. Exposed to SwiftUI as MetalVideoView.
//
// ⛔ Built on a Mac; verified on the iPad. Requires Shaders.metal compiled into the app's default library.
import CoreVideo
import MetalKit
import SwiftUI

final class MetalVideoRenderer: NSObject, MTKViewDelegate {
    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private var textureCache: CVMetalTextureCache?

    private let lock = NSLock()
    private var pendingBuffer: CVPixelBuffer?
    // Keep the CVMetalTexture wrappers alive until the GPU finishes using them.
    private var live: [CVMetalTexture] = []

    init?(mtkView: MTKView) {
        guard let device = MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue(),
              let library = device.makeDefaultLibrary(),
              let vtx = library.makeFunction(name: "vtx"),
              let frag = library.makeFunction(name: "frag") else { return nil }
        self.device = device
        self.queue = queue

        mtkView.device = device
        mtkView.colorPixelFormat = .bgra8Unorm
        mtkView.framebufferOnly = true

        let desc = MTLRenderPipelineDescriptor()
        desc.vertexFunction = vtx
        desc.fragmentFunction = frag
        desc.colorAttachments[0].pixelFormat = mtkView.colorPixelFormat
        guard let pipeline = try? device.makeRenderPipelineState(descriptor: desc) else { return nil }
        self.pipeline = pipeline
        super.init()
        CVMetalTextureCacheCreate(kCFAllocatorDefault, nil, device, nil, &textureCache)
    }

    /// Hand a decoded frame to the renderer (thread-safe). It is drawn on the next MTKView tick.
    func setPixelBuffer(_ pb: CVPixelBuffer) {
        lock.lock(); pendingBuffer = pb; lock.unlock()
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        lock.lock(); let pb = pendingBuffer; lock.unlock()
        guard let pb,
              let cache = textureCache,
              let drawable = view.currentDrawable,
              let rpd = view.currentRenderPassDescriptor,
              let cmd = queue.makeCommandBuffer(),
              let enc = cmd.makeRenderCommandEncoder(descriptor: rpd) else { return }

        let w = CVPixelBufferGetWidth(pb)
        let h = CVPixelBufferGetHeight(pb)
        var wrappers: [CVMetalTexture] = []
        func planeTexture(_ index: Int, _ format: MTLPixelFormat, _ pw: Int, _ ph: Int) -> MTLTexture? {
            var cvtex: CVMetalTexture?
            let r = CVMetalTextureCacheCreateTextureFromImage(
                kCFAllocatorDefault, cache, pb, nil, format, pw, ph, index, &cvtex)
            guard r == kCVReturnSuccess, let cvtex, let tex = CVMetalTextureGetTexture(cvtex) else { return nil }
            wrappers.append(cvtex)
            return tex
        }
        guard let yTex = planeTexture(0, .r8Unorm, w, h),
              let cbcrTex = planeTexture(1, .rg8Unorm, w / 2, h / 2) else {
            enc.endEncoding(); return
        }

        enc.setRenderPipelineState(pipeline)
        enc.setFragmentTexture(yTex, index: 0)
        enc.setFragmentTexture(cbcrTex, index: 1)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
        cmd.present(drawable)
        // Release the previous frame's wrappers only after this frame's GPU work completes.
        let previous = live
        cmd.addCompletedHandler { _ in _ = previous }
        live = wrappers
        cmd.commit()
    }
}

struct MetalVideoView: UIViewRepresentable {
    /// Receives the renderer once the MTKView is created, so the owner can push decoded frames to it.
    let onReady: (MetalVideoRenderer) -> Void

    func makeUIView(context: Context) -> MTKView {
        let view = MTKView()
        view.preferredFramesPerSecond = 60
        view.isPaused = false
        view.enableSetNeedsDisplay = false
        if let renderer = MetalVideoRenderer(mtkView: view) {
            view.delegate = renderer
            context.coordinator.renderer = renderer
            onReady(renderer)
        }
        return view
    }

    func updateUIView(_ uiView: MTKView, context: Context) {}
    func makeCoordinator() -> Coordinator { Coordinator() }
    final class Coordinator { var renderer: MetalVideoRenderer? }
}
