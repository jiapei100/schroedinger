
#ifndef __SCHRO_OPENGL_TYPES_H__
#define __SCHRO_OPENGL_TYPES_H__

#include <schroedinger/schroutils.h>

SCHRO_BEGIN_DECLS

#ifndef SCHRO_OPENGL_TYPEDEF
#define SCHRO_OPENGL_TYPEDEF
typedef struct _SchroOpenGL SchroOpenGL;
#endif

typedef struct _SchroOpenGLShader SchroOpenGLShader;
typedef struct _SchroOpenGLShaderLibrary SchroOpenGLShaderLibrary;

typedef struct _SchroOpenGLPixelbuffer SchroOpenGLPixelbuffer;
typedef struct _SchroOpenGLCanvas SchroOpenGLCanvas;
typedef struct _SchroOpenGLCanvasPool SchroOpenGLCanvasPool;

typedef struct _SchroOpenGLSpatialWeightBlock SchroOpenGLSpatialWeightBlock;
typedef struct _SchroOpenGLSpatialWeightGrid SchroOpenGLSpatialWeightGrid;
typedef struct _SchroOpenGLSpatialWeightPool SchroOpenGLSpatialWeightPool;

SCHRO_END_DECLS

#endif
