// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_ACCESSIBILITY_RENDERER_ACCESSIBILITY_COMPLETE_H_
#define CONTENT_RENDERER_ACCESSIBILITY_RENDERER_ACCESSIBILITY_COMPLETE_H_

#include <set>
#include <vector>

#include "base/containers/hash_tables.h"
#include "base/memory/weak_ptr.h"
#include "content/public/renderer/render_view_observer.h"
#include "content/renderer/accessibility/renderer_accessibility.h"
#include "third_party/WebKit/public/web/WebAXEnums.h"
#include "third_party/WebKit/public/web/WebAXObject.h"
#include "ui/accessibility/ax_node_data.h"

namespace blink {
class WebDocument;
class WebNode;
};

namespace content {
class RenderViewImpl;

// This is the subclass of RendererAccessibility that implements
// complete accessibility support for assistive technology (as opposed to
// partial support - see RendererAccessibilityFocusOnly).
//
// This version turns on Blink's accessibility code and sends
// a serialized representation of that tree whenever it changes. It also
// handles requests from the browser to perform accessibility actions on
// nodes in the tree (e.g., change focus, or click on a button).
class CONTENT_EXPORT RendererAccessibilityComplete
    : public RendererAccessibility {
 public:
  explicit RendererAccessibilityComplete(RenderViewImpl* render_view);
  virtual ~RendererAccessibilityComplete();

  // RenderView::Observer implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;
  virtual void FocusedNodeChanged(const blink::WebNode& node) OVERRIDE;
  virtual void DidFinishLoad(blink::WebFrame* frame) OVERRIDE;

  // RendererAccessibility.
  virtual void HandleWebAccessibilityEvent(
      const blink::WebAXObject& obj, blink::WebAXEvent event) OVERRIDE;
  virtual RendererAccessibilityType GetType() OVERRIDE;

  void HandleAXEvent(const blink::WebAXObject& obj, ui::AXEvent event);

  // In order to keep track of what nodes the browser knows about, we keep a
  // representation of the browser tree - just IDs and parent/child
  // relationships.
  struct CONTENT_EXPORT BrowserTreeNode {
    BrowserTreeNode();
    virtual ~BrowserTreeNode();
    int32 id;
    gfx::Rect location;
    BrowserTreeNode* parent;
    std::vector<BrowserTreeNode*> children;
  };

  virtual BrowserTreeNode* CreateBrowserTreeNode();

 protected:
  // Send queued events from the renderer to the browser.
  void SendPendingAccessibilityEvents();

  // Check the entire accessibility tree to see if any nodes have
  // changed location, by comparing their locations to the cached
  // versions. If any have moved, send an IPC with the new locations.
  void SendLocationChanges();

 private:
  // Serialize the given accessibility object |obj| and append it to
  // |dst|, and then recursively also serialize any *new* children of
  // |obj|, based on what object ids we know the browser already has.
  // The set of ids serialized is added to |ids_serialized|, and any
  // ids previously in that set are not serialized again.
  void SerializeChangedNodes(const blink::WebAXObject& obj,
                             std::vector<ui::AXNodeData>* dst,
                             std::set<int>* ids_serialized);

  // Clear the given node and recursively delete all of its descendants
  // from the browser tree. (Does not delete |browser_node|).
  void ClearBrowserTreeNode(BrowserTreeNode* browser_node);

  // Handlers for messages from the browser to the renderer.
  void OnDoDefaultAction(int acc_obj_id);
  void OnEventsAck();
  void OnChangeScrollPosition(int acc_obj_id, int scroll_x, int scroll_y);
  void OnScrollToMakeVisible(int acc_obj_id, gfx::Rect subfocus);
  void OnScrollToPoint(int acc_obj_id, gfx::Point point);
  void OnSetFocus(int acc_obj_id);
  void OnSetTextSelection(int acc_obj_id, int start_offset, int end_offset);
  void OnFatalError();

  // Checks if a Blink accessibility object is an editable text node.
  bool IsEditableText(const blink::WebAXObject& node);

  // Recursively explore the tree of Blink accessibility objects rooted
  // at |src|, and for each editable text node encountered, add a
  // corresponding WebAccessibility node as a child of |dst|.
  void RecursiveAddEditableTextNodesToTree(
      const blink::WebAXObject& src,
      ui::AXNodeData* dst);

  // Build a tree of serializable ui::AXNodeData nodes to send to the
  // browser process, given a WebAXObject node from Blink.
  // Modifies |dst| in-place, it's assumed to be empty.
  void BuildAccessibilityTree(const blink::WebAXObject& src,
                              bool include_children,
                              ui::AXNodeData* dst);

  // So we can queue up tasks to be executed later.
  base::WeakPtrFactory<RendererAccessibilityComplete> weak_factory_;

  // Events from Blink are collected until they are ready to be
  // sent to the browser.
  std::vector<AccessibilityHostMsg_EventParams> pending_events_;

  // Our representation of the browser tree.
  BrowserTreeNode* browser_root_;

  // A map from IDs to nodes in the browser tree.
  base::hash_map<int32, BrowserTreeNode*> browser_id_map_;

  // The most recently observed scroll offset of the root document element.
  // TODO(dmazzoni): remove once https://bugs.webkit.org/show_bug.cgi?id=73460
  // is fixed.
  gfx::Size last_scroll_offset_;

  // Set if we are waiting for an accessibility event ack.
  bool ack_pending_;

  DISALLOW_COPY_AND_ASSIGN(RendererAccessibilityComplete);
};

#endif  // CONTENT_RENDERER_ACCESSIBILITY_RENDERER_ACCESSIBILITY_COMPLETE_H_

}  // namespace content
