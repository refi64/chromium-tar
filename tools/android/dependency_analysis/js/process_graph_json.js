// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {Node, GraphModel} from './graph_model.js';
import {shortenPackageName} from './chrome_hooks.js';

/**
 * A graph read from JSON.
 * @typedef {Object} JsonGraph
 * @property {Array<Object>} nodes
 * @property {Array<Object>} edges
 */

/**
 * Transforms a graph JSON generated by Python scripts
 * (generate_json_dependency_graph.py) into a working format for d3.
 * @param {!JsonGraph} jsonGraph The JSON graph to parse.
 * @return {!GraphModel} The parsed out GraphModel object.
 */
function parseGraphModelFromJson(jsonGraph) {
  const graph = new GraphModel();
  for (const nodeData of jsonGraph.nodes) {
    const node = new Node(nodeData.name, shortenPackageName(nodeData.name));
    graph.addNodeIfNew(node);
  }
  for (const edgeData of jsonGraph.edges) {
    // Assuming correctness of the JSON, we can assert non-null Nodes here.
    const /** !Node */ beginNode = graph.getNodeById(edgeData.begin);
    const /** !Node */ endNode = graph.getNodeById(edgeData.end);
    graph.addEdgeIfNew(beginNode, endNode);
  }
  return graph;
}

export {
  parseGraphModelFromJson,
};