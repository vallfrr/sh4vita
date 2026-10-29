"""Counts the IDirect3DDevice8 methods called through the global device pointer.

Input: re/out/decomp/*.c (REA/Ghidra pseudo-code). Output: table method -> number of calls, functions.
ponytail: only sees calls made directly on DAT_006e7d94; a copy into a local
escapes the pattern. Enough for the method list, not for an exact count.
"""
import collections, pathlib, re, sys

DEVICE8 = """QueryInterface AddRef Release TestCooperativeLevel GetAvailableTextureMem
ResourceManagerDiscardBytes GetDirect3D GetDeviceCaps GetDisplayMode GetCreationParameters
SetCursorProperties SetCursorPosition ShowCursor CreateAdditionalSwapChain Reset Present
GetBackBuffer GetRasterStatus SetGammaRamp GetGammaRamp CreateTexture CreateVolumeTexture
CreateCubeTexture CreateVertexBuffer CreateIndexBuffer CreateRenderTarget
CreateDepthStencilSurface CreateImageSurface CopyRects UpdateTexture GetFrontBuffer
SetRenderTarget GetRenderTarget GetDepthStencilSurface BeginScene EndScene Clear SetTransform
GetTransform MultiplyTransform SetViewport GetViewport SetMaterial GetMaterial SetLight GetLight
LightEnable GetLightEnable SetClipPlane GetClipPlane SetRenderState GetRenderState
BeginStateBlock EndStateBlock ApplyStateBlock CaptureStateBlock DeleteStateBlock
CreateStateBlock SetClipStatus GetClipStatus GetTexture SetTexture GetTextureStageState
SetTextureStageState ValidateDevice GetInfo SetPaletteEntries GetPaletteEntries
SetCurrentTexturePalette GetCurrentTexturePalette DrawPrimitive DrawIndexedPrimitive
DrawPrimitiveUP DrawIndexedPrimitiveUP ProcessVertices CreateVertexShader SetVertexShader
GetVertexShader DeleteVertexShader SetVertexShaderConstant GetVertexShaderConstant
GetVertexShaderDeclaration GetVertexShaderFunction SetStreamSource GetStreamSource SetIndices
GetIndices CreatePixelShader SetPixelShader GetPixelShader DeletePixelShader
SetPixelShaderConstant GetPixelShaderConstant GetPixelShaderFunction DrawRectPatch
DrawTriPatch DeletePatch""".split()

CALL = re.compile(r"\*DAT_006e7d94\s*\+\s*(0x[0-9a-f]+)\)")


def census(decomp_dir):
    hits = collections.defaultdict(lambda: [0, set()])
    for f in pathlib.Path(decomp_dir).glob("*.c"):
        for m in CALL.finditer(f.read_text(encoding="utf-8", errors="replace")):
            name = DEVICE8[int(m.group(1), 16) // 4]
            hits[name][0] += 1
            hits[name][1].add(f.stem)
    return hits


def demo():
    assert DEVICE8[0x88 // 4] == "BeginScene"
    assert DEVICE8[0x90 // 4] == "Clear"
    assert DEVICE8[0x3C // 4] == "Present"
    assert len(DEVICE8) == 97


if __name__ == "__main__":
    demo()
    hits = census(sys.argv[1])
    for name in sorted(hits, key=lambda n: DEVICE8.index(n)):
        n, fns = hits[name]
        print(f"{DEVICE8.index(name):3} {name:28} {n:5} calls  {len(fns):4} functions")
