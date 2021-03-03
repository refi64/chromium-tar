// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_NTP_NEW_TAB_PAGE_VIEW_CONTROLLER_H_
#define IOS_CHROME_BROWSER_UI_NTP_NEW_TAB_PAGE_VIEW_CONTROLLER_H_

#import <UIKit/UIKit.h>

#import "ios/chrome/browser/ui/content_suggestions/content_suggestions_collection_controlling.h"

@class ContentSuggestionsHeaderViewController;
@class ContentSuggestionsViewController;
@class DiscoverFeedWrapperViewController;
@protocol NewTabPageContentDelegate;
@protocol OverscrollActionsControllerDelegate;

// View controller containing all the content presented on a standard,
// non-incognito new tab page.
@interface NewTabPageViewController
    : UIViewController <ContentSuggestionsCollectionControlling,
                        UIScrollViewDelegate>

// View controller wrapping the Discover feed.
@property(nonatomic, strong)
    DiscoverFeedWrapperViewController* discoverFeedWrapperViewController;

// Delegate for the overscroll actions.
@property(nonatomic, weak) id<OverscrollActionsControllerDelegate>
    overscrollDelegate;

// The content suggestions header, containing the fake omnibox and the doodle.
@property(nonatomic, weak) UIViewController* headerController;

// Delegate for actions relating to the NTP content.
@property(nonatomic, weak) id<NewTabPageContentDelegate> ntpContentDelegate;

// Initializes view controller with NTP content view controllers.
// |discoverFeedViewController| represents the Discover feed for suggesting
// articles. |contentSuggestionsViewController| represents other content
// suggestions, such as the most visited site tiles.
- (instancetype)initWithContentSuggestionsViewController:
    (UICollectionViewController*)contentSuggestionsViewController
    NS_DESIGNATED_INITIALIZER;

- (instancetype)initWithNibName:(NSString*)name
                         bundle:(NSBundle*)bundle NS_UNAVAILABLE;
- (instancetype)initWithCoder:(NSCoder*)coder NS_UNAVAILABLE;

// Called when a snapshot of the content will be taken.
- (void)willUpdateSnapshot;

// Sets the feed collection contentOffset to |offset| to set the initial scroll
// position.
- (void)setContentOffset:(CGFloat)offset;

// Updates this ViewController layout to match the new ContentSuggestions
// layout.
// TODO(crbug.com/1170995): Remove once ContentSuggestions can be added as part
// of a header.
- (void)updateLayoutForContentSuggestions;

// Returns the current height of the content suggestions content.
- (CGFloat)contentSuggestionsContentHeight;

// Handles device rotation logic.
// TODO(crbug.com/1177953): Detect device rotation in NewTabPageViewController.
- (void)handleDeviceRotation;

@end

#endif  // IOS_CHROME_BROWSER_UI_NTP_NEW_TAB_PAGE_VIEW_CONTROLLER_H_
