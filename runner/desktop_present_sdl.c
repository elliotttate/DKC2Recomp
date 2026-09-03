#include "desktop_present_sdl.h"

#include "desktop_launcher.h"
#include "desktop_viewport.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_syswm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

static void SetError(char *error, size_t capacity, const char *message);

/* OpenGL 2.0 shader entry points, resolved through SDL at context creation:
 * the platform GL header only declares the 1.x fixed-function API. */
typedef struct Dkc2GlShaderApi {
  PFNGLCREATESHADERPROC CreateShader;
  PFNGLSHADERSOURCEPROC ShaderSource;
  PFNGLCOMPILESHADERPROC CompileShader;
  PFNGLGETSHADERIVPROC GetShaderiv;
  PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
  PFNGLDELETESHADERPROC DeleteShader;
  PFNGLCREATEPROGRAMPROC CreateProgram;
  PFNGLATTACHSHADERPROC AttachShader;
  PFNGLLINKPROGRAMPROC LinkProgram;
  PFNGLGETPROGRAMIVPROC GetProgramiv;
  PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
  PFNGLDELETEPROGRAMPROC DeleteProgram;
  PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
  PFNGLUSEPROGRAMPROC UseProgram;
  PFNGLUNIFORM1IPROC Uniform1i;
  PFNGLUNIFORM1FPROC Uniform1f;
  PFNGLUNIFORM2FPROC Uniform2f;
  /* EXT_framebuffer_object, for offscreen captures. Optional. */
  PFNGLGENFRAMEBUFFERSEXTPROC GenFramebuffers;
  PFNGLBINDFRAMEBUFFEREXTPROC BindFramebuffer;
  PFNGLFRAMEBUFFERTEXTURE2DEXTPROC FramebufferTexture2D;
  PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC CheckFramebufferStatus;
  PFNGLDELETEFRAMEBUFFERSEXTPROC DeleteFramebuffers;
  bool fbo;
} Dkc2GlShaderApi;

static Dkc2GlShaderApi s_gl;

static bool LoadShaderApi(void) {
#define DKC2_GL_LOAD(name, proc) \
  s_gl.name = (proc)SDL_GL_GetProcAddress("gl" #name); \
  if (!s_gl.name) return false
  DKC2_GL_LOAD(CreateShader, PFNGLCREATESHADERPROC);
  DKC2_GL_LOAD(ShaderSource, PFNGLSHADERSOURCEPROC);
  DKC2_GL_LOAD(CompileShader, PFNGLCOMPILESHADERPROC);
  DKC2_GL_LOAD(GetShaderiv, PFNGLGETSHADERIVPROC);
  DKC2_GL_LOAD(GetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC);
  DKC2_GL_LOAD(DeleteShader, PFNGLDELETESHADERPROC);
  DKC2_GL_LOAD(CreateProgram, PFNGLCREATEPROGRAMPROC);
  DKC2_GL_LOAD(AttachShader, PFNGLATTACHSHADERPROC);
  DKC2_GL_LOAD(LinkProgram, PFNGLLINKPROGRAMPROC);
  DKC2_GL_LOAD(GetProgramiv, PFNGLGETPROGRAMIVPROC);
  DKC2_GL_LOAD(GetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC);
  DKC2_GL_LOAD(DeleteProgram, PFNGLDELETEPROGRAMPROC);
  DKC2_GL_LOAD(GetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);
  DKC2_GL_LOAD(UseProgram, PFNGLUSEPROGRAMPROC);
  DKC2_GL_LOAD(Uniform1i, PFNGLUNIFORM1IPROC);
  DKC2_GL_LOAD(Uniform1f, PFNGLUNIFORM1FPROC);
  DKC2_GL_LOAD(Uniform2f, PFNGLUNIFORM2FPROC);
#undef DKC2_GL_LOAD
  s_gl.GenFramebuffers = (PFNGLGENFRAMEBUFFERSEXTPROC)
      SDL_GL_GetProcAddress("glGenFramebuffersEXT");
  s_gl.BindFramebuffer = (PFNGLBINDFRAMEBUFFEREXTPROC)
      SDL_GL_GetProcAddress("glBindFramebufferEXT");
  s_gl.FramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
      SDL_GL_GetProcAddress("glFramebufferTexture2DEXT");
  s_gl.CheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)
      SDL_GL_GetProcAddress("glCheckFramebufferStatusEXT");
  s_gl.DeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSEXTPROC)
      SDL_GL_GetProcAddress("glDeleteFramebuffersEXT");
  s_gl.fbo = s_gl.GenFramebuffers && s_gl.BindFramebuffer &&
             s_gl.FramebufferTexture2D && s_gl.CheckFramebufferStatus &&
             s_gl.DeleteFramebuffers;
  return true;
}

/*
 * Reconstruct: an experimental single-pass upscaler for pixel art on a
 * high-density display, written for the GLSL 1.20 that the legacy OpenGL
 * 2.1 context provides.
 *
 * Every output fragment locates its source texel and fetches the 21-texel
 * xBR footprint (5x5 without corners) with nearest sampling. From those it
 * derives, per mode:
 *
 *   mode 0  sharp boundaries: inside a texel the color is flat; within one
 *           output pixel of a texel edge it blends with the neighbor, so a
 *           fractional scale (the 16-inch panel shows a 342-pixel frame at
 *           about ten times) has neither uneven pixel widths nor blur.
 *   mode 1  + dither decoding: a 2x2 checkerboard or a one-texel line
 *           dither between two colors is what SNES artists used for a
 *           mid-tone a CRT would blur into; the texel takes that average.
 *   mode 2  + diagonal edges: the xBR level-1 corner test decides whether
 *           a texel corner belongs to a diagonal edge, and that corner
 *           takes the neighbor's color along an antialiased 45-degree
 *           line, evaluated analytically at the fragment rather than on a
 *           fixed 2x or 3x grid.
 *   mode 3  + level-2 slopes: 2:1 and 1:2 edge lines where the corner
 *           test says the edge continues.
 *   mode 4  + level-3 slopes: 3:1 and 1:3 lines where it continues further.
 *
 * strength scales the edge blend (1 = full); softness widens every
 * transition band from one output pixel to up to three; shading blends the
 * flat interior of a texel toward a bilinear gradient wherever its
 * neighbors are close in color (shading bands, not outlines). Colors are
 * the frame after the selected screen model, so CRT/Composite/Trinitron
 * still apply.
 */
static const char kReconstructVertexSource[] =
    "#version 120\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "  uv = gl_MultiTexCoord0.xy;\n"
    "  gl_Position = gl_Vertex;\n"
    "}\n";

static const char kReconstructFragmentSource[] =
    "#version 120\n"
    "uniform sampler2D source;\n"
    "uniform vec2 source_size;\n"
    "uniform vec2 output_size;\n"
    "uniform int mode;\n"
    "uniform float strength;\n"
    "uniform float softness;\n"
    "uniform float shading;\n"
    "varying vec2 uv;\n"
    "vec3 tx(vec2 t) { return texture2D(source, (t + 0.5) / source_size).rgb; }\n"
    "float df(vec3 a, vec3 b) {\n"
    "  vec3 d = abs(a - b);\n"
    "  return dot(d, vec3(0.299, 0.587, 0.114)) * 2.0 +\n"
    "         abs((a.r - b.r) - (a.b - b.b)) * 0.5;\n"
    "}\n"
    "bool eq(vec3 a, vec3 b) { return df(a, b) < 0.004; }\n"
    "vec3 decode(vec3 c, vec3 n, vec3 s, vec3 w, vec3 e,\n"
    "            vec3 nw, vec3 ne, vec3 sw, vec3 se) {\n"
    "  if (mode < 1) return c;\n"
    "  if (eq(c, nw) && eq(c, ne) && eq(c, sw) && eq(c, se) &&\n"
    "      eq(n, s) && eq(n, e) && eq(n, w) && !eq(c, n))\n"
    "    return mix(c, n, 0.5);\n"
    "  if (eq(c, n) && eq(c, s) && eq(e, ne) && eq(e, se) &&\n"
    "      eq(w, nw) && eq(w, sw) && eq(e, w) && !eq(c, e))\n"
    "    return mix(c, e, 0.5);\n"
    "  if (eq(c, e) && eq(c, w) && eq(n, ne) && eq(n, nw) &&\n"
    "      eq(s, se) && eq(s, sw) && eq(n, s) && !eq(c, n))\n"
    "    return mix(c, n, 0.5);\n"
    "  return c;\n"
    "}\n"
    "void main() {\n"
    "  vec2 pos = uv * source_size;\n"
    "  vec2 t = floor(pos);\n"
    "  vec2 fp = pos - t;\n"
    "  vec2 dir = vec2(fp.x < 0.5 ? -1.0 : 1.0, fp.y < 0.5 ? -1.0 : 1.0);\n"
    "  vec2 f = abs(fp - 0.5) + 0.5;\n"
    "  vec2 scale = max(output_size / source_size, vec2(1.0));\n"
    "  /* softness widens every transition from one output pixel to three. */\n"
    "  float band = 1.0 + 2.0 * softness;\n"
    "  float aa = min(scale.x, scale.y) / band;\n"
    "  vec2 dx = vec2(dir.x, 0.0);\n"
    "  vec2 dy = vec2(0.0, dir.y);\n"
    "  vec3 A1 = tx(t - dx - dy - dy), B1 = tx(t - dy - dy), C1 = tx(t + dx - dy - dy);\n"
    "  vec3 A0 = tx(t - dx - dx - dy), A = tx(t - dx - dy), B = tx(t - dy), C = tx(t + dx - dy), C4 = tx(t + dx + dx - dy);\n"
    "  vec3 D0 = tx(t - dx - dx), D = tx(t - dx), E = tx(t), F = tx(t + dx), F4 = tx(t + dx + dx);\n"
    "  vec3 G0 = tx(t - dx - dx + dy), G = tx(t - dx + dy), H = tx(t + dy), I = tx(t + dx + dy), I4 = tx(t + dx + dx + dy);\n"
    "  vec3 G5 = tx(t - dx + dy + dy), H5 = tx(t + dy + dy), I5 = tx(t + dx + dy + dy);\n"
    "  vec3 e = decode(E, B, H, D, F, A, C, G, I);\n"
    "  vec3 fc = decode(F, C, I, E, F4, B, C4, H, I4);\n"
    "  vec3 hc = decode(H, E, H5, G, I, D, F, G5, I5);\n"
    "  vec3 ic = decode(I, F, I5, H, I4, E, F4, H5, I5);\n"
    "  /* f runs from the texel center (0.5) to its edge (1.0); blend half\n"
    "     way to the neighbor over the last output pixel before the edge,\n"
    "     and the neighbor's fragments continue the other half. */\n"
    "  vec2 adj = 0.5 * clamp((f - (1.0 - 0.5 * band / scale)) * scale / band,\n"
    "                         0.0, 1.0);\n"
    "  vec3 base = mix(mix(e, fc, adj.x), mix(hc, ic, adj.x), adj.y);\n"
    "  /* Smooth shading: where the neighbors are close in color (a shading\n"
    "     band of the pre-rendered art, not an outline), interpolate them\n"
    "     into a gradient instead of flat steps. */\n"
    "  if (shading > 0.0) {\n"
    "    vec2 g = f - 0.5;\n"
    "    vec3 bil = mix(mix(e, fc, g.x), mix(hc, ic, g.x), g.y);\n"
    "    float sim = max(max(df(e, fc), df(e, hc)), df(e, ic));\n"
    "    float w = shading * (1.0 - smoothstep(0.03, 0.14, sim));\n"
    "    base = mix(base, bil, w);\n"
    "  }\n"
    "  if (mode < 2) { gl_FragColor = vec4(base, 1.0); return; }\n"
    "  float wd1 = df(E, C) + df(E, G) + df(I, H5) + df(I, F4) + 4.0 * df(H, F);\n"
    "  float wd2 = df(H, D) + df(H, I5) + df(F, I4) + df(F, B) + 4.0 * df(E, I);\n"
    "  bool edr = wd1 < wd2 && !eq(E, H) && !eq(E, F) && !(eq(E, I) && eq(H, F));\n"
    "  if (!edr) { gl_FragColor = vec4(base, 1.0); return; }\n"
    "  vec3 nc = (df(E, F) <= df(E, H)) ? fc : hc;\n"
    "  float cov = clamp((f.x + f.y - 1.5) * aa + 0.5, 0.0, 1.0);\n"
    "  if (mode >= 3) {\n"
    "    bool left = 2.0 * df(F, G) <= df(H, C) && !eq(E, G) && !eq(D, G);\n"
    "    bool up = df(F, G) >= 2.0 * df(H, C) && !eq(E, C) && !eq(B, C);\n"
    "    if (left) cov = max(cov, clamp((2.0 * f.x + f.y - 2.0) * aa * 0.75 + 0.5, 0.0, 1.0));\n"
    "    if (up) cov = max(cov, clamp((f.x + 2.0 * f.y - 2.0) * aa * 0.75 + 0.5, 0.0, 1.0));\n"
    "    if (mode >= 4) {\n"
    "      bool left3 = left && 4.0 * df(F, G) <= df(H, C) && !eq(E, G0) && !eq(D0, G0);\n"
    "      bool up3 = up && df(F, G) >= 4.0 * df(H, C) && !eq(E, C1) && !eq(B1, C1);\n"
    "      if (left3) cov = max(cov, clamp((3.0 * f.x + f.y - 2.5) * aa * 0.6 + 0.5, 0.0, 1.0));\n"
    "      if (up3) cov = max(cov, clamp((f.x + 3.0 * f.y - 2.5) * aa * 0.6 + 0.5, 0.0, 1.0));\n"
    "    }\n"
    "  }\n"
    "  gl_FragColor = vec4(mix(base, nc, cov * strength), 1.0);\n"
    "}\n";

static GLuint CompileShader(GLenum kind, const char *source, char *error,
                            size_t capacity) {
  GLuint shader = s_gl.CreateShader(kind);
  if (!shader) {
    SetError(error, capacity, "glCreateShader failed");
    return 0;
  }
  const GLchar *sources[1] = {source};
  s_gl.ShaderSource(shader, 1, sources, NULL);
  s_gl.CompileShader(shader);
  GLint status = GL_FALSE;
  s_gl.GetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    char log[512] = {0};
    s_gl.GetShaderInfoLog(shader, sizeof log - 1, NULL, log);
    (void)snprintf(error, capacity, "reconstruct shader failed to compile: %s",
                   log);
    s_gl.DeleteShader(shader);
    return 0;
  }
  return shader;
}

static void BuildReconstructProgram(Dkc2SdlPresenter *presenter) {
  presenter->program = 0;
  if (!LoadShaderApi()) {
    SetError(presenter->shader_error, sizeof presenter->shader_error,
             "reconstruct shader unavailable: OpenGL 2.0 shader entry "
             "points missing");
    return;
  }
  GLuint vertex = CompileShader(GL_VERTEX_SHADER, kReconstructVertexSource,
                                presenter->shader_error,
                                sizeof presenter->shader_error);
  if (!vertex) return;
  GLuint fragment = CompileShader(GL_FRAGMENT_SHADER,
                                  kReconstructFragmentSource,
                                  presenter->shader_error,
                                  sizeof presenter->shader_error);
  if (!fragment) {
    s_gl.DeleteShader(vertex);
    return;
  }
  GLuint program = s_gl.CreateProgram();
  s_gl.AttachShader(program, vertex);
  s_gl.AttachShader(program, fragment);
  s_gl.LinkProgram(program);
  s_gl.DeleteShader(vertex);
  s_gl.DeleteShader(fragment);
  GLint status = GL_FALSE;
  s_gl.GetProgramiv(program, GL_LINK_STATUS, &status);
  if (status != GL_TRUE) {
    char log[512] = {0};
    s_gl.GetProgramInfoLog(program, sizeof log - 1, NULL, log);
    (void)snprintf(presenter->shader_error, sizeof presenter->shader_error,
                   "reconstruct shader failed to link: %s", log);
    s_gl.DeleteProgram(program);
    return;
  }
  presenter->program = program;
  presenter->uniform_source = s_gl.GetUniformLocation(program, "source");
  presenter->uniform_source_size =
      s_gl.GetUniformLocation(program, "source_size");
  presenter->uniform_output_size =
      s_gl.GetUniformLocation(program, "output_size");
  presenter->uniform_mode = s_gl.GetUniformLocation(program, "mode");
  presenter->uniform_strength = s_gl.GetUniformLocation(program, "strength");
  presenter->uniform_softness = s_gl.GetUniformLocation(program, "softness");
  presenter->uniform_shading = s_gl.GetUniformLocation(program, "shading");
}

static void SetError(char *error, size_t capacity, const char *message) {
  if (!error || capacity == 0) return;
  (void)snprintf(error, capacity, "%s", message ? message : "SDL error");
}

static bool SetSdlSwapInterval(void *user, int interval) {
  (void)user;
  return SDL_GL_SetSwapInterval(interval) == 0;
}

static bool EnvironmentEnabled(const char *name) {
  const char *value = getenv(name);
  return value && *value && *value != '0';
}

bool Dkc2SdlPresenterInit(Dkc2SdlPresenter *presenter, int window_scale,
                          int fullscreen, bool hidden, bool linear_filter,
                          int source_width, int source_height,
                          char *error, size_t error_capacity) {
  if (!presenter || window_scale < 1 ||
      source_width <= 0 || source_height <= 0) {
    SetError(error, error_capacity, "invalid SDL presenter settings");
    return false;
  }
  memset(presenter, 0, sizeof *presenter);
  (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  (void)SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  Uint32 flags =
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_OPENGL;
  flags |= hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
  if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  int base_height = 240;
  int base_width =
      source_width * 7 * base_height / (source_height * 6);
  SDL_Window *window = SDL_CreateWindow(
      DKC2_PRODUCT_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      base_width * window_scale, base_height * window_scale, flags);
  if (!window) {
    SetError(error, error_capacity, SDL_GetError());
    return false;
  }
  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context || SDL_GL_MakeCurrent(window, context) != 0) {
    SetError(error, error_capacity, SDL_GetError());
    if (context) SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return false;
  }
#ifdef __APPLE__
  /* The visible Mac host uses one exact Mach deadline as its presentation
   * authority. A second blocking OpenGL-vsync gate can quantize a timely
   * frame onto the following 60/120-Hz display callback and produce the
   * alternating micro-hitches seen during horizontal traversal. macOS still
   * composites the window atomically. Keep the old gate only as an explicit
   * diagnostic override. */
  presenter->software_paced = !hidden &&
      !EnvironmentEnabled("DKC2_KEEP_OPENGL_VSYNC");
#else
  presenter->software_paced = false;
#endif
  if (presenter->software_paced || hidden) {
    (void)SDL_GL_SetSwapInterval(0);
    presenter->vsync_status = kDkc2DesktopVsyncDisabled;
  } else {
    presenter->vsync_status =
        Dkc2DesktopEnableVsync(SetSdlSwapInterval, NULL);
    if (presenter->vsync_status != kDkc2DesktopVsyncEnabled)
      (void)SDL_GL_SetSwapInterval(0);
  }
  GLuint texture = 0;
  glGenTextures(1, &texture);
  if (!texture) {
    SetError(error, error_capacity, "OpenGL texture creation failed");
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return false;
  }
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  const GLubyte *version = glGetString(GL_VERSION);
  (void)snprintf(presenter->backend, sizeof presenter->backend,
                 "SDL2/OpenGL %s; vsync=%s; pacing=%s",
                 version ? (const char *)version : "unknown",
                 Dkc2DesktopVsyncStatusName(presenter->vsync_status),
                 presenter->software_paced ? "mach" : "swap");
  presenter->window = window;
  presenter->gl_context = context;
  presenter->texture = texture;
  presenter->linear_filter = linear_filter;
  presenter->upscaler = linear_filter ? kDkc2UpscalerBilinear
                                      : kDkc2UpscalerNearest;
  presenter->reconstruct_mode = 3;
  presenter->reconstruct_strength = 1.0f;
  presenter->reconstruct_softness = 0.5f;
  presenter->reconstruct_shading = 0.6f;
  BuildReconstructProgram(presenter);
  if (presenter->program == 0 && presenter->shader_error[0])
    fprintf(stderr, "warning: %s\n", presenter->shader_error);
  return true;
}

static float ClampUnit(float value) {
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

int Dkc2SdlPresenterSetUpscaler(Dkc2SdlPresenter *presenter, int upscaler,
                                int mode, float strength, float softness,
                                float shading) {
  if (!presenter) return kDkc2UpscalerNearest;
  if (upscaler < 0 || upscaler >= kDkc2UpscalerCount)
    upscaler = kDkc2UpscalerNearest;
  if (upscaler == kDkc2UpscalerReconstruct && presenter->program == 0)
    upscaler = presenter->linear_filter ? kDkc2UpscalerBilinear
                                        : kDkc2UpscalerNearest;
  presenter->upscaler = upscaler;
  presenter->reconstruct_mode = mode < 0 ? 0 : (mode > 4 ? 4 : mode);
  presenter->reconstruct_strength = ClampUnit(strength);
  presenter->reconstruct_softness = ClampUnit(softness);
  presenter->reconstruct_shading = ClampUnit(shading);
  if (upscaler != kDkc2UpscalerReconstruct)
    presenter->linear_filter = upscaler == kDkc2UpscalerBilinear;
  return upscaler;
}

const char *Dkc2SdlPresenterUpscalerName(int upscaler) {
  switch (upscaler) {
    case kDkc2UpscalerBilinear: return "bilinear";
    case kDkc2UpscalerReconstruct: return "reconstruct";
    default: return "nearest";
  }
}

bool Dkc2SdlPresenterUpscalerFromName(const char *name, int *upscaler) {
  if (!name || !upscaler) return false;
  if (strcmp(name, "nearest") == 0 || strcmp(name, "0") == 0) {
    *upscaler = kDkc2UpscalerNearest;
    return true;
  }
  if (strcmp(name, "bilinear") == 0 || strcmp(name, "linear") == 0 ||
      strcmp(name, "1") == 0) {
    *upscaler = kDkc2UpscalerBilinear;
    return true;
  }
  if (strcmp(name, "reconstruct") == 0 || strcmp(name, "2") == 0) {
    *upscaler = kDkc2UpscalerReconstruct;
    return true;
  }
  return false;
}

void Dkc2SdlPresenterDrawableSize(Dkc2SdlPresenter *presenter, int *width,
                                  int *height) {
  if (width) *width = 0;
  if (height) *height = 0;
  if (!presenter || !presenter->window) return;
  SDL_GL_GetDrawableSize((SDL_Window *)presenter->window, width, height);
}

void Dkc2SdlPresenterArmCapture(Dkc2SdlPresenter *presenter, uint8_t *rgb,
                                int width, int height) {
  if (!presenter) return;
  presenter->capture_rgb = rgb;
  presenter->capture_width = width;
  presenter->capture_height = height;
  presenter->capture_done = false;
}

static void DrawFrameQuad(void) {
  glBegin(GL_QUADS);
  glTexCoord2f(0.0f, 1.0f);
  glVertex2f(-1.0f, -1.0f);
  glTexCoord2f(1.0f, 1.0f);
  glVertex2f(1.0f, -1.0f);
  glTexCoord2f(1.0f, 0.0f);
  glVertex2f(1.0f, 1.0f);
  glTexCoord2f(0.0f, 0.0f);
  glVertex2f(-1.0f, 1.0f);
  glEnd();
}

bool Dkc2SdlPresenterPresent(Dkc2SdlPresenter *presenter,
                             const uint8_t *pixels, int source_width,
                             int source_height,
                             Dkc2SdlOverlayDraw overlay_draw,
                             void *overlay_user) {
  if (!presenter || !presenter->window || !presenter->gl_context ||
      !presenter->texture || !pixels || source_width <= 0 ||
      source_height <= 0)
    return false;
  SDL_Window *window = (SDL_Window *)presenter->window;
  SDL_GLContext context = (SDL_GLContext)presenter->gl_context;
  if (SDL_GL_MakeCurrent(window, context) != 0) return false;
  int output_width = 0;
  int output_height = 0;
  SDL_GL_GetDrawableSize(window, &output_width, &output_height);
  Dkc2DesktopViewport viewport;
  if (!Dkc2DesktopComputeViewport(output_width, output_height,
                                  source_width, source_height, &viewport))
    return true;

  glViewport(0, 0, output_width, output_height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(viewport.x, output_height - viewport.y - viewport.height,
             viewport.width, viewport.height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, presenter->texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  if (presenter->texture_width != source_width ||
      presenter->texture_height != source_height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source_width, source_height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    presenter->texture_width = source_width;
    presenter->texture_height = source_height;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source_width, source_height,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
  }
  const bool reconstruct =
      presenter->upscaler == kDkc2UpscalerReconstruct && presenter->program;
  GLint sampling = presenter->linear_filter && !reconstruct ? GL_LINEAR
                                                             : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampling);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sampling);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  if (reconstruct) {
    s_gl.UseProgram(presenter->program);
    s_gl.Uniform1i(presenter->uniform_source, 0);
    s_gl.Uniform2f(presenter->uniform_source_size, (float)source_width,
                (float)source_height);
    s_gl.Uniform2f(presenter->uniform_output_size, (float)viewport.width,
                (float)viewport.height);
    s_gl.Uniform1i(presenter->uniform_mode, presenter->reconstruct_mode);
    s_gl.Uniform1f(presenter->uniform_strength,
                presenter->reconstruct_strength);
    s_gl.Uniform1f(presenter->uniform_softness,
                presenter->reconstruct_softness);
    s_gl.Uniform1f(presenter->uniform_shading,
                presenter->reconstruct_shading);
  }
  DrawFrameQuad();
  /* Offscreen capture: draw the same frame into a framebuffer object and
   * read it back. A hidden window's back buffer reads back empty on macOS,
   * so the capture never depends on the window being displayed. */
  if (presenter->capture_rgb && !presenter->capture_done &&
      presenter->capture_width == output_width &&
      presenter->capture_height == output_height && s_gl.fbo) {
    GLuint fbo = 0, color = 0;
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, output_width, output_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, presenter->texture);
    s_gl.GenFramebuffers(1, &fbo);
    s_gl.BindFramebuffer(GL_FRAMEBUFFER_EXT, fbo);
    s_gl.FramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                              GL_TEXTURE_2D, color, 0);
    if (s_gl.CheckFramebufferStatus(GL_FRAMEBUFFER_EXT) ==
        GL_FRAMEBUFFER_COMPLETE_EXT) {
      glViewport(0, 0, output_width, output_height);
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      glViewport(viewport.x, output_height - viewport.y - viewport.height,
                 viewport.width, viewport.height);
      DrawFrameQuad();
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, output_width, output_height, GL_RGB,
                   GL_UNSIGNED_BYTE, presenter->capture_rgb);
      const size_t row = (size_t)output_width * 3u;
      uint8_t *tmp = (uint8_t *)malloc(row);
      if (tmp) {
        for (int y = 0; y < output_height / 2; y++) {
          uint8_t *a = presenter->capture_rgb + (size_t)y * row;
          uint8_t *b = presenter->capture_rgb +
                       (size_t)(output_height - 1 - y) * row;
          memcpy(tmp, a, row);
          memcpy(a, b, row);
          memcpy(b, tmp, row);
        }
        free(tmp);
      }
      presenter->capture_done = true;
    }
    s_gl.BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
    s_gl.DeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, presenter->texture);
  }
  if (reconstruct)
    s_gl.UseProgram(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  if (overlay_draw) {
    glViewport(0, 0, output_width, output_height);
    overlay_draw(overlay_user, output_width, output_height);
  }
  glFlush();
  SDL_GL_SwapWindow(window);
  return true;
}

void Dkc2SdlPresenterSetTitle(Dkc2SdlPresenter *presenter,
                              const char *title) {
  if (presenter && presenter->window && title)
    SDL_SetWindowTitle((SDL_Window *)presenter->window, title);
}

bool Dkc2SdlPresenterSetFullscreen(Dkc2SdlPresenter *presenter,
                                   bool fullscreen) {
  if (!presenter || !presenter->window)
    return false;
  Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
  return SDL_SetWindowFullscreen((SDL_Window *)presenter->window, flags) == 0;
}

bool Dkc2SdlPresenterIsFullscreen(const Dkc2SdlPresenter *presenter) {
  if (!presenter || !presenter->window)
    return false;
  return (SDL_GetWindowFlags((SDL_Window *)presenter->window) &
          (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
}

const char *Dkc2SdlPresenterBackend(const Dkc2SdlPresenter *presenter) {
  return presenter && presenter->backend[0] ? presenter->backend : "SDL2";
}

Dkc2DesktopVsyncStatus Dkc2SdlPresenterVsyncStatus(
    const Dkc2SdlPresenter *presenter) {
  return presenter ? presenter->vsync_status
                   : kDkc2DesktopVsyncUnsupported;
}

void *Dkc2SdlPresenterNativeWindow(const Dkc2SdlPresenter *presenter) {
  if (!presenter || !presenter->window) return NULL;
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo((SDL_Window *)presenter->window, &info))
    return NULL;
#if defined(SDL_VIDEO_DRIVER_COCOA)
  if (info.subsystem == SDL_SYSWM_COCOA) return (void *)info.info.cocoa.window;
#endif
  return NULL;
}

bool Dkc2SdlPresenterUsesSoftwarePacing(
    const Dkc2SdlPresenter *presenter) {
  return presenter && presenter->software_paced;
}

void Dkc2SdlPresenterDestroy(Dkc2SdlPresenter *presenter) {
  if (!presenter) return;
  if (presenter->window && presenter->gl_context)
    (void)SDL_GL_MakeCurrent((SDL_Window *)presenter->window,
                            (SDL_GLContext)presenter->gl_context);
  if (presenter->texture) glDeleteTextures(1, &presenter->texture);
  if (presenter->program) s_gl.DeleteProgram(presenter->program);
  if (presenter->gl_context)
    SDL_GL_DeleteContext((SDL_GLContext)presenter->gl_context);
  if (presenter->window) SDL_DestroyWindow((SDL_Window *)presenter->window);
  memset(presenter, 0, sizeof *presenter);
}
