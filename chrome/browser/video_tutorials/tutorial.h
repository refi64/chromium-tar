// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIDEO_TUTORIALS_TUTORIAL_H_
#define CHROME_BROWSER_VIDEO_TUTORIALS_TUTORIAL_H_

#include <string>
#include "url/gurl.h"

namespace video_tutorials {

// Please align this enum with
// chrome/browser/video_tutorials/proto/video_tutorials.proto.
enum class FeatureType {
  kTest = -1,
  kInvalid = 0,
  kDebug = 1,
  kDownload = 2,
  kSearch = 3,
  kMaxValue = kSearch,
};

// In memory struct of a video tutorial entry.
// Represents the metadata required to play a video tutorial.
struct Tutorial {
  Tutorial();
  Tutorial(FeatureType feature,
           const std::string& title,
           const std::string& video_url,
           const std::string& share_url,
           const std::string& poster_url,
           const std::string& caption_url,
           int video_length);
  ~Tutorial();

  bool operator==(const Tutorial& other) const;
  bool operator!=(const Tutorial& other) const;

  Tutorial(const Tutorial& other);
  Tutorial& operator=(const Tutorial& other);

  // Type of feature where this video tutorial targeted.
  FeatureType feature{FeatureType::kInvalid};

  // The title of the video.
  std::string title;

  // The URL of the video.
  GURL video_url;

  // The URL of the poster image.
  GURL share_url;

  // The URL of the subtitles.
  GURL poster_url;

  // The share URL for the video.
  GURL caption_url;

  // The length of the video in seconds.
  int video_length;
};

}  // namespace video_tutorials

#endif  // CHROME_BROWSER_VIDEO_TUTORIALS_TUTORIAL_H_
