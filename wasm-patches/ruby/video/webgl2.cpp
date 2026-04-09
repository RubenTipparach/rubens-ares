// WebGL 2 video driver for Emscripten/WASM builds
// Renders via an HTML5 Canvas using Emscripten's OpenGL ES 3.0 emulation

#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

struct VideoWebGL2 : VideoDriver {
  VideoWebGL2& self = *this;
  VideoWebGL2(Video& super) : VideoDriver(super) { construct(); }
  ~VideoWebGL2() { destruct(); }

  auto create() -> bool override {
    VideoDriver::exclusive = true;
    VideoDriver::format = "ARGB24";
    return initialize();
  }

  auto driver() -> string override { return "WebGL 2"; }
  auto ready() -> bool override { return _ready; }

  auto hasFullScreen() -> bool override { return false; }
  auto hasMonitor() -> bool override { return false; }
  auto hasContext() -> bool override { return true; }
  auto hasBlocking() -> bool override { return false; }
  auto hasFlush() -> bool override { return true; }
  auto hasShader() -> bool override { return false; }

  auto hasFormats() -> vector<string> override {
    return {"ARGB24"};
  }

  auto setFullScreen(bool fullScreen) -> bool override { return true; }
  auto setMonitor(string monitor) -> bool override { return true; }

  auto setContext(uintptr context) -> bool override {
    return initialize();
  }

  auto setBlocking(bool blocking) -> bool override { return true; }

  auto setFormat(string format) -> bool override {
    if(format == "ARGB24") {
      return initialize();
    }
    return false;
  }

  auto setShader(string shader) -> bool override { return true; }
  auto focused() -> bool override { return true; }

  auto clear() -> void override {
    memory::fill<u32>(_buffer, _bufferWidth * _bufferHeight);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
  }

  auto size(u32& width, u32& height) -> void override {
    width = _canvasWidth;
    height = _canvasHeight;
  }

  auto acquire(u32*& data, u32& pitch, u32 width, u32 height) -> bool override {
    if(width != _width || height != _height) resize(width, height);
    pitch = _bufferWidth * sizeof(u32);
    return data = _buffer;
  }

  auto release() -> void override {
  }

  auto output(u32 width, u32 height) -> void override {
    if(!_ready) return;

    if(!width) width = _canvasWidth;
    if(!height) height = _canvasHeight;

    glViewport(0, 0, _canvasWidth, _canvasHeight);

    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _width, _height,
                    GL_RGBA, GL_UNSIGNED_BYTE, _buffer);

    // Draw fullscreen quad
    glUseProgram(_program);
    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glFlush();
  }

  auto poll() -> void override {
  }

private:
  auto construct() -> void {
    _canvasWidth = 640;
    _canvasHeight = 480;
  }

  auto destruct() -> void {
    terminate();
  }

  auto initialize() -> bool {
    terminate();

    // Create Emscripten WebGL2 context
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.antialias = false;
    attrs.preserveDrawingBuffer = true;

    _context = emscripten_webgl_create_context("#canvas", &attrs);
    if(_context <= 0) {
      // Try with default canvas
      _context = emscripten_webgl_create_context(nullptr, &attrs);
      if(_context <= 0) return false;
    }
    emscripten_webgl_make_context_current(_context);

    // Query canvas size
    int w, h;
    emscripten_get_canvas_element_size("#canvas", &w, &h);
    _canvasWidth = w;
    _canvasHeight = h;

    // Create shader program
    _program = createShaderProgram();
    if(!_program) return false;

    // Create fullscreen quad VAO
    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);
    // Vertex data is generated in the vertex shader
    glBindVertexArray(0);

    // Create texture
    glGenTextures(1, &_texture);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);

    resize(256, 256);
    return _ready = true;
  }

  auto terminate() -> void {
    _ready = false;

    if(_texture) {
      glDeleteTextures(1, &_texture);
      _texture = 0;
    }

    if(_vao) {
      glDeleteVertexArrays(1, &_vao);
      _vao = 0;
    }

    if(_program) {
      glDeleteProgram(_program);
      _program = 0;
    }

    if(_buffer) {
      delete[] _buffer;
      _buffer = nullptr;
    }

    if(_context > 0) {
      emscripten_webgl_destroy_context(_context);
      _context = 0;
    }

    _bufferWidth = 0;
    _bufferHeight = 0;
  }

  auto resize(u32 width, u32 height) -> void {
    _width = width;
    _height = height;

    _bufferWidth = max(_bufferWidth, width);
    _bufferHeight = max(_bufferHeight, height);
    delete[] _buffer;
    _buffer = new u32[_bufferWidth * _bufferHeight]();

    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _bufferWidth, _bufferHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, _buffer);
  }

  auto createShaderProgram() -> GLuint {
    const char* vertexSource = R"(#version 300 es
      out vec2 vTexCoord;
      void main() {
        float x = float(gl_VertexID & 1) * 2.0;
        float y = float((gl_VertexID >> 1) & 1) * 2.0;
        gl_Position = vec4(x - 1.0, y - 1.0, 0.0, 1.0);
        vTexCoord = vec2(x * 0.5, 1.0 - y * 0.5);
      }
    )";

    const char* fragmentSource = R"(#version 300 es
      precision mediump float;
      in vec2 vTexCoord;
      out vec4 fragColor;
      uniform sampler2D uTexture;
      void main() {
        fragColor = texture(uTexture, vTexCoord);
      }
    )";

    auto vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexSource, nullptr);
    glCompileShader(vs);

    auto fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentSource, nullptr);
    glCompileShader(fs);

    auto program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if(!linked) {
      glDeleteProgram(program);
      return 0;
    }

    return program;
  }

  bool _ready = false;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE _context = 0;
  u32 _canvasWidth = 640;
  u32 _canvasHeight = 480;

  u32 _width = 256;
  u32 _height = 256;

  GLuint _texture = 0;
  GLuint _vao = 0;
  GLuint _program = 0;
  u32* _buffer = nullptr;
  u32 _bufferWidth = 0;
  u32 _bufferHeight = 0;
};
