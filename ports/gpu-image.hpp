// SPDX-License-Identifier: MIT
#pragma once
#include <artcraft/raster.hpp>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/gl.h>
#include <algorithm>
#include <memory>
#include <string>
namespace dome_office {
struct ScreenRect {
  int x, y, w, h;
};
// The patched GL driver can widen nested FLTK clips; canvas/dock content uses
// an explicit physical viewport so partial rows cannot paint over neighboring controls.
class GpuClip {
  bool active_ = false;

public:
  GpuClip(Fl_Widget &widget, ScreenRect clip) {
    if (auto *window = dynamic_cast<Fl_Gl_Window *>(widget.window())) {
      active_ = true;
      double scale = window->pixels_per_unit();
      glPushAttrib(GL_ENABLE_BIT | GL_SCISSOR_BIT);
      glEnable(GL_SCISSOR_TEST);
      glScissor(int(clip.x * scale), int((window->h() - clip.y - clip.h) * scale),
                std::max(0, int(clip.w * scale)), std::max(0, int(clip.h * scale)));
    }
  }
  ~GpuClip() {
    if (active_)
      glPopAttrib();
  }
  GpuClip(const GpuClip &) = delete;
  GpuClip &operator=(const GpuClip &) = delete;
};
// Transient textures follow the native window's current GL context, including XR recreation.
class GpuImage {
  std::unique_ptr<Fl_PNG_Image> image_;
  std::string encoded_;

public:
  bool set(const std::string &encoded) {
    if (encoded == encoded_)
      return false;
    auto bytes = artcraft::decode_base64(encoded, 16 * 1024 * 1024);
    artcraft::png_dimensions(bytes);
    auto image = std::make_unique<Fl_PNG_Image>(nullptr, bytes.data(), int(bytes.size()));
    if (image->fail())
      throw std::runtime_error("Cannot decode bitmap preview");
    image_ = std::move(image);
    encoded_ = encoded;
    return true;
  }
  const Fl_PNG_Image *pixels() const { return image_.get(); }
  void clear() {
    image_.reset();
    encoded_.clear();
  }
  int width() const { return image_ ? image_->data_w() : 0; }
  int height() const { return image_ ? image_->data_h() : 0; }
  bool empty() const { return !image_; }
  void draw(Fl_Widget &widget, ScreenRect rect, ScreenRect clip) const {
    if (!image_)
      return;
    auto *window = dynamic_cast<Fl_Gl_Window *>(widget.window());
    if (!window)
      return;
    int depth = image_->d();
    if (depth != 3 && depth != 4)
      throw std::runtime_error("Unsupported preview channels");
    double scale = window->pixels_per_unit();
    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT |
                 GL_PIXEL_MODE_BIT | GL_SCISSOR_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(int(clip.x * scale), int((window->h() - clip.y - clip.h) * scale),
              std::max(0, int(clip.w * scale)), std::max(0, int(clip.h * scale)));
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, depth == 4 ? GL_RGBA : GL_RGB, width(), height(), 0,
                 depth == 4 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, image_->array);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2i(rect.x, rect.y);
    glTexCoord2f(1, 0);
    glVertex2i(rect.x + rect.w, rect.y);
    glTexCoord2f(1, 1);
    glVertex2i(rect.x + rect.w, rect.y + rect.h);
    glTexCoord2f(0, 1);
    glVertex2i(rect.x, rect.y + rect.h);
    glEnd();
    glDeleteTextures(1, &texture);
    glPopClientAttrib();
    glPopAttrib();
  }
};
} // namespace dome_office
