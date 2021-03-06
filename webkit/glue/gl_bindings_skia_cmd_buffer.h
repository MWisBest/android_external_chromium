// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_GLUE_GL_BINDINGS_SKIA_CMD_BUFFER_H_
#define WEBKIT_GLUE_GL_BINDINGS_SKIA_CMD_BUFFER_H_
#pragma once

namespace webkit_glue {

// The GPU back-end for skia requires pointers to GL functions. This function
// binds skia-gpu to the cmd buffers GL.
void BindSkiaToCommandBufferGL();

}  // namespace webkit_glue

#endif  // WEBKIT_GLUE_GL_BINDINGS_SKIA_CMD_BUFFER_H_

