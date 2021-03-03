// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../../img.js';
import '../module_header.js';
import 'chrome://resources/cr_elements/hidden_style_css.m.js';

import {loadTimeData} from 'chrome://resources/js/load_time_data.m.js';
import {html, PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {ModuleDescriptor} from '../module_descriptor.js';
import {TaskModuleHandlerProxy} from './task_module_handler_proxy.js';

/**
 * @fileoverview Implements the UI of a task module. This module shows a
 * currently active task search journey and provides a way for the user to
 * continue that search journey.
 */

class TaskModuleElement extends PolymerElement {
  static get is() {
    return 'ntp-task-module';
  }

  static get template() {
    return html`{__html_template__}`;
  }

  static get properties() {
    return {
      /** @type {!taskModule.mojom.TaskModuleType} */
      taskModuleType: {
        type: Number,
        observer: 'onTaskModuleTypeChange_',
      },

      /** @type {!taskModule.mojom.Task} */
      task: Object,

      /** @type {boolean} */
      showInfoDialog: Boolean,
    };
  }

  constructor() {
    super();
    /** @type {IntersectionObserver} */
    this.intersectionObserver_ = null;
  }

  /**
   * @return {boolean}
   * @private
   */
  isRecipe_() {
    return this.taskModuleType === taskModule.mojom.TaskModuleType.kRecipe;
  }

  /**
   * @return {boolean}
   * @private
   */
  isShopping_() {
    return this.taskModuleType === taskModule.mojom.TaskModuleType.kShopping;
  }

  /** @private */
  onTaskModuleTypeChange_() {
    switch (this.taskModuleType) {
      case taskModule.mojom.TaskModuleType.kRecipe:
        this.toggleAttribute('recipe');
        break;
      case taskModule.mojom.TaskModuleType.kShopping:
        this.toggleAttribute('shopping');
        break;
    }
  }

  /**
   * @param {!Event} e
   * @private
   */
  onTaskItemClick_(e) {
    const index = this.$.taskItemsRepeat.indexForElement(e.target);
    TaskModuleHandlerProxy.getInstance().handler.onTaskItemClicked(
        this.taskModuleType, index);
    this.dispatchEvent(new Event('usage', {bubbles: true, composed: true}));
  }

  /**
   * @param {!Event} e
   * @private
   */
  onPillClick_(e) {
    const index = this.$.relatedSearchesRepeat.indexForElement(e.target);
    TaskModuleHandlerProxy.getInstance().handler.onRelatedSearchClicked(
        this.taskModuleType, index);
    this.dispatchEvent(new Event('usage', {bubbles: true, composed: true}));
  }

  /** @private */
  onInfoButtonClick_() {
    this.showInfoDialog = true;
  }

  /** @private */
  onCloseClick_() {
    this.showInfoDialog = false;
  }

  /** @private */
  onDismissButtonClick_() {
    TaskModuleHandlerProxy.getInstance().handler.dismissTask(
        this.taskModuleType, this.task.name);
    this.dispatchEvent(new CustomEvent('dismiss-module', {
      bubbles: true,
      composed: true,
      detail: {
        message: loadTimeData.getStringF(
            'dismissModuleToastMessage', this.task.name),
        restoreCallback: this.onRestore_.bind(this),
      },
    }));
  }

  /** @private */
  onRestore_() {
    TaskModuleHandlerProxy.getInstance().handler.restoreTask(
        this.taskModuleType, this.task.name);
  }

  /** @private */
  onDomChange_() {
    if (!this.intersectionObserver_) {
      this.intersectionObserver_ = new IntersectionObserver(entries => {
        entries.forEach(({intersectionRatio, target}) => {
          target.style.visibility =
              intersectionRatio < 1 ? 'hidden' : 'visible';
        });
        this.dispatchEvent(new Event('visibility-update'));
      }, {root: this, threshold: 1});
    } else {
      this.intersectionObserver_.disconnect();
    }
    this.shadowRoot.querySelectorAll('.task-item, .pill')
        .forEach(el => this.intersectionObserver_.observe(el));
  }
}

customElements.define(TaskModuleElement.is, TaskModuleElement);

/** @return {!Promise<?HTMLElement>} */
async function createModule(taskModuleType) {
  const {task} =
      await TaskModuleHandlerProxy.getInstance().handler.getPrimaryTask(
          taskModuleType);
  if (!task) {
    return null;
  }
  const element = new TaskModuleElement();
  element.taskModuleType = taskModuleType;
  element.task = task;
  return element;
}

/** @type {!ModuleDescriptor} */
export const recipeTasksDescriptor = new ModuleDescriptor(
    /*id=*/ 'recipe_tasks',
    /*heightPx=*/ 300,
    createModule.bind(null, taskModule.mojom.TaskModuleType.kRecipe));

/** @type {!ModuleDescriptor} */
export const shoppingTasksDescriptor = new ModuleDescriptor(
    /*id=*/ 'shopping_tasks',
    /*heightPx=*/ 324,
    createModule.bind(null, taskModule.mojom.TaskModuleType.kShopping));
