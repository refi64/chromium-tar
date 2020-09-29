// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file was generated by:
//   tools/json_schema_compiler/compiler.py.
// NOTE: The format of types has changed. 'FooType' is now
//   'chrome.terminalPrivate.FooType'.
// Please run the closure compiler before committing changes.
// See https://chromium.googlesource.com/chromium/src/+/master/docs/closure_compilation.md

/** @fileoverview Externs generated from namespace: terminalPrivate */

/** @const */
chrome.terminalPrivate = {};

/**
 * @enum {string}
 */
chrome.terminalPrivate.OutputType = {
  STDOUT: 'stdout',
  STDERR: 'stderr',
  EXIT: 'exit',
};

/**
 * Starts new process.
 * @param {string} processName Name of the process to open. May be 'crosh' or
 *     'vmshell'.
 * @param {?Array<string>|undefined} args Command line arguments to pass to the
 *     process.
 * @param {function(string): void} callback Returns id of the launched process.
 *     If no process was launched returns -1.
 */
chrome.terminalPrivate.openTerminalProcess = function(processName, args, callback) {};

/**
 * Starts new vmshell process.
 * @param {?Array<string>|undefined} args Command line arguments to pass to
 *     vmshell.
 * @param {function(string): void} callback Returns id of the launched vmshell
 *     process. If no process was launched returns -1.
 */
chrome.terminalPrivate.openVmshellProcess = function(args, callback) {};

/**
 * Closes previously opened process from either openTerminalProcess or
 * openVmshellProcess.
 * @param {string} id Unique id of the process we want to close.
 * @param {function(boolean): void=} callback Function that gets called when
 *     close operation is started for the process. Returns success of the
 *     function.
 */
chrome.terminalPrivate.closeTerminalProcess = function(id, callback) {};

/**
 * Sends input that will be routed to stdin of the process with the specified
 * id.
 * @param {string} id The id of the process to which we want to send input.
 * @param {string} input Input we are sending to the process.
 * @param {function(boolean): void=} callback Callback that will be called when
 *     sendInput method ends. Returns success.
 */
chrome.terminalPrivate.sendInput = function(id, input, callback) {};

/**
 * Notify the process with the id id that terminal window size has changed.
 * @param {string} id The id of the process.
 * @param {number} width New window width (as column count).
 * @param {number} height New window height (as row count).
 * @param {function(boolean): void=} callback Callback that will be called when
 *     sendInput method ends. Returns success.
 */
chrome.terminalPrivate.onTerminalResize = function(id, width, height, callback) {};

/**
 * Called from |onProcessOutput| when the event is dispatched to terminal
 * extension. Observing the terminal process output will be paused after
 * |onProcessOutput| is dispatched until this method is called.
 * @param {number} tabId Tab ID from |onProcessOutput| event.
 * @param {string} id The id of the process to which |onProcessOutput| was
 *     dispatched.
 */
chrome.terminalPrivate.ackOutput = function(tabId, id) {};

/**
 * Open the Terminal tabbed window.
 * @param {function(): void} callback Callback that will be called when
 *     complete.
 */
chrome.terminalPrivate.openWindow = function(callback) {};

/**
 * Open the Terminal Settings page.
 * @param {function(): void} callback Callback that will be called when
 *     complete.
 */
chrome.terminalPrivate.openOptionsPage = function(callback) {};

/**
 * Returns an object (DictionaryValue) containing UI settings such as font style
 * and color used by the crosh extension.  This function is called by the
 * terminal system app the first time it is run to migrate any previous
 * settings.
 * @param {function(Object): void} callback Callback that will be called with
 *     settings.
 */
chrome.terminalPrivate.getCroshSettings = function(callback) {};

/**
 * Returns an object (DictionaryValue) containing UI settings such as font style
 * and colors used by terminal and stored as a syncable pref.  The UI currently
 * has ~70 properties and we wish to allow flexibility for these to change in
 * the UI without updating this API, so we allow any properties.
 * @param {function(Object): void} callback Callback that will be called with
 *     settings.
 */
chrome.terminalPrivate.getSettings = function(callback) {};

/**
 * Sets terminal UI settings which are stored as a syncable pref.
 * @param {Object} settings Settings to update into prefs.
 * @param {function(): void} callback Callback that will be called when
 *     complete.
 */
chrome.terminalPrivate.setSettings = function(settings, callback) {};

/**
 * Returns a boolean indicating whether the accessibility spoken feedback is on.
 * @param {function(boolean): void} callback Callback that will be called with
 *     the a11y status.
 */
chrome.terminalPrivate.getA11yStatus = function(callback) {};

/**
 * Fired when an opened process writes something to its output. Observing
 * further process output will be blocked until |ackOutput| for the terminal is
 * called. Internally, first event argument will be ID of the tab that contains
 * terminal instance for which this event is intended. This argument will be
 * stripped before passing the event to the extension.
 * @type {!ChromeEvent}
 */
chrome.terminalPrivate.onProcessOutput;

/**
 * Fired when terminal UI settings change.
 * @type {!ChromeEvent}
 */
chrome.terminalPrivate.onSettingsChanged;

/**
 * Fired when a11y spoken feedback is enabled/disabled.
 * @type {!ChromeEvent}
 */
chrome.terminalPrivate.onA11yStatusChanged;