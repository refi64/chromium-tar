// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pdf/pdf_view_plugin_base.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "pdf/pdfium/pdfium_engine.h"
#include "pdf/ppapi_migration/url_loader.h"
#include "ui/gfx/geometry/rect.h"

namespace chrome_pdf {

// static
constexpr double PdfViewPluginBase::kMinZoom;

PdfViewPluginBase::PdfViewPluginBase() = default;

PdfViewPluginBase::~PdfViewPluginBase() = default;

uint32_t PdfViewPluginBase::GetBackgroundColor() {
  return background_color_;
}

void PdfViewPluginBase::OnPaint(const std::vector<gfx::Rect>& paint_rects,
                                std::vector<PaintReadyRect>* ready,
                                std::vector<gfx::Rect>* pending) {
  base::AutoReset<bool> auto_reset_in_paint(&in_paint_, true);
  DoPaint(paint_rects, ready, pending);
}

void PdfViewPluginBase::InitializeEngine(
    PDFiumFormFiller::ScriptOption script_option) {
  engine_ = std::make_unique<PDFiumEngine>(this, script_option);
}

void PdfViewPluginBase::DestroyEngine() {
  engine_.reset();
}

void PdfViewPluginBase::LoadUrl(const std::string& url, bool is_print_preview) {
  UrlRequest request;
  request.url = url;
  request.method = "GET";
  request.ignore_redirects = true;

  std::unique_ptr<UrlLoader> loader = CreateUrlLoaderInternal();
  UrlLoader* raw_loader = loader.get();
  raw_loader->Open(
      request,
      base::BindOnce(is_print_preview ? &PdfViewPluginBase::DidOpenPreview
                                      : &PdfViewPluginBase::DidOpen,
                     GetWeakPtr(), std::move(loader)));
}

void PdfViewPluginBase::CalculateBackgroundParts() {
  background_parts_.clear();
  int left_width = available_area_.x();
  int right_start = available_area_.right();
  int right_width = std::abs(plugin_size().width() - available_area_.right());
  int bottom = std::min(available_area_.bottom(), plugin_size().height());

  // Note: we assume the display of the PDF document is always centered
  // horizontally, but not necessarily centered vertically.
  // Add the left rectangle.
  BackgroundPart part = {gfx::Rect(left_width, bottom), GetBackgroundColor()};
  if (!part.location.IsEmpty())
    background_parts_.push_back(part);

  // Add the right rectangle.
  part.location = gfx::Rect(right_start, 0, right_width, bottom);
  if (!part.location.IsEmpty())
    background_parts_.push_back(part);

  // Add the bottom rectangle.
  part.location = gfx::Rect(0, bottom, plugin_size().width(),
                            plugin_size().height() - bottom);
  if (!part.location.IsEmpty())
    background_parts_.push_back(part);
}

int PdfViewPluginBase::GetDocumentPixelWidth() const {
  return static_cast<int>(
      std::ceil(document_size_.width() * zoom() * device_scale()));
}

int PdfViewPluginBase::GetDocumentPixelHeight() const {
  return static_cast<int>(
      std::ceil(document_size_.height() * zoom() * device_scale()));
}

void PdfViewPluginBase::SetZoom(double scale) {
  double old_zoom = zoom_;
  zoom_ = scale;
  OnGeometryChanged(old_zoom, device_scale_);
}

}  // namespace chrome_pdf
