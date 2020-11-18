// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/tab_grid/grid/horizontal_layout.h"

#import "ios/chrome/browser/ui/tab_grid/grid/grid_constants.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@implementation HorizontalLayout

- (instancetype)init {
  if (self = [super init]) {
    self.scrollDirection = UICollectionViewScrollDirectionHorizontal;
  }
  return self;
}

#pragma mark - UICollectionViewLayout

// This is called whenever the layout is invalidated, including during rotation.
// Resizes item, margins, and spacing to fit new size classes and width.
- (void)prepareLayout {
  [super prepareLayout];

  self.itemSize = kGridCellSizeSmall;
  CGFloat height = CGRectGetHeight(self.collectionView.bounds);
  CGFloat spacing = kGridLayoutLineSpacingCompactCompactLimitedWidth;
  self.sectionInset = UIEdgeInsets{
      spacing, spacing, height - self.itemSize.height - 2 * spacing, spacing};
  self.minimumLineSpacing = kGridLayoutLineSpacingRegularRegular;
}

@end
