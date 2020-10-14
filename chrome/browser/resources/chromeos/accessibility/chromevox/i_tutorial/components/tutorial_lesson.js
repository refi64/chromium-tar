// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_button/cr_button.m.js';
import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.m.js';

import {html, Polymer} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

export const TutorialLesson = Polymer({
  is: 'tutorial-lesson',

  _template: html`{__html_template__}`,

  properties: {
    lessonNum: {type: Number},

    title: {type: String},

    content: {type: Array},

    medium: {type: String},

    curriculums: {type: Array},

    practiceTitle: {type: String},

    practiceInstructions: {type: String},

    practiceFile: {type: String},

    practiceState: {type: Object},

    events: {type: Array},

    hints: {type: Array},

    hintCounter: {type: Number, value: 0},

    hintIntervalId: {type: Number},

    goalStateReached: {type: Boolean, value: false},

    actions: {type: Array},

    autoInteractive: {type: Boolean, value: false},

    // Observed properties.

    activeLessonNum: {type: Number, observer: 'setVisibility'},
  },

  /** @override */
  ready() {
    if (this.practiceFile) {
      this.populatePracticeContent();
      for (const evt of this.events) {
        this.$.practiceContent.addEventListener(
            evt, this.onPracticeEvent.bind(this), true);
      }
    }
  },

  /**
   * Updates this lessons visibility whenever the active lesson of the tutorial
   * changes.
   * @private
   */
  setVisibility() {
    if (this.lessonNum === this.activeLessonNum) {
      this.show();
    } else {
      this.hide();
    }
  },

  /** @private */
  show() {
    this.$.container.hidden = false;
    // Shorthand for Polymer.dom(this.root).querySelector(...).
    const focus = this.$$('[tabindex]');
    if (!focus) {
      throw new Error(
          'A lesson must have an element which specifies tabindex.');
    }
    focus.focus();
    if (!focus.isEqualNode(this.shadowRoot.activeElement)) {
      // Call show() again if we weren't able to focus the target element.
      setTimeout(this.show.bind(this), 500);
    }
  },

  /** @private */
  hide() {
    this.$.container.hidden = true;
  },


  // Methods for managing the practice area.


  /**
   * Asynchronously populates practice area.
   * @private
   */
  populatePracticeContent() {
    const path = '../i_tutorial/lessons/' + this.practiceFile + '.html';
    const xhr = new XMLHttpRequest();
    xhr.open('GET', path, true);
    xhr.onload = (evt) => {
      if (xhr.readyState === 4 && xhr.status === 200) {
        this.$.practiceContent.innerHTML = xhr.responseText;
      } else {
        console.error(xhr.statusText);
      }
    };
    xhr.onerror = function(evt) {
      console.error('Failed to open practice file: ' + path);
      console.error(xhr.statusText);
    };
    xhr.send(null);
  },

  /** @private */
  startPractice() {
    this.$.practice.showModal();
    this.startHints();
  },

  /** @private */
  endPractice() {
    this.stopHints();
    this.$.startPractice.focus();
  },


  // Methods for tracking the state of the practice area.


  /**
   * @param {Event} event
   * @private
   */
  onPracticeEvent(event) {
    const elt = event.target.id;
    const type = event.type;
    // Maybe update goal state.
    if (elt in this.practiceState) {
      if (type in this.practiceState[elt]) {
        this.practiceState[elt][type] = true;
      }
    }

    if (this.isGoalStateReached()) {
      this.onGoalStateReached();
    }
  },

  /**
   * @return {boolean}
   * @private
   */
  isGoalStateReached() {
    if (!this.practiceState) {
      return false;
    }

    if (this.goalStateReached === true) {
      return true;
    }

    for (const [elt, state] of Object.entries(this.practiceState)) {
      for (const [evt, performed] of Object.entries(state)) {
        if (performed == false) {
          return false;
        }
      }
    }
    return true;
  },

  /** @private */
  onGoalStateReached() {
    const previousState = this.goalStateReached;
    this.goalStateReached = true;
    if (previousState === false) {
      // Only perform when crossing the threshold from not reached to reached.
      this.stopHints();
      this.requestSpeech(
          'You have passed this tutorial lesson. Find and press the exit ' +
          'practice area button to continue');
    }
  },


  // Methods for managing hints.

  /** @private */
  startHints() {
    this.hintCounter = 0;
    this.hintIntervalId = setInterval(() => {
      if (this.hintCounter >= this.hints.length) {
        this.stopHints();
        return;
      }
      this.requestSpeech(this.hints[this.hintCounter]);
      this.hintCounter += 1;
    }, 20 * 1000);
  },

  /** @private */
  stopHints() {
    if (this.hintIntervalId) {
      clearInterval(this.hintIntervalId);
    }
  },


  // Miscellaneous methods.

  /**
   * @param {string} medium
   * @param {string} curriculum
   * @return {boolean}
   */
  shouldInclude(medium, curriculum) {
    if (this.medium === medium && this.curriculums.includes(curriculum)) {
      return true;
    }

    return false;
  },

  /**
   * Requests speech from the Panel.
   * @param {string} text
   * @private
   */
  requestSpeech(text) {
    this.dispatchEvent(
        new CustomEvent('requestspeech', {composed: true, detail: {text}}));
  },

  /**
   * @return {boolean}
   * @private
   */
  shouldHidePracticeButton() {
    if (!this.practiceFile) {
      return true;
    }

    return false;
  },
});