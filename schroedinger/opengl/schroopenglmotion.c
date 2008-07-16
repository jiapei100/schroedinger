 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <schroedinger/schro.h>
#include <schroedinger/opengl/schroopengl.h>
#include <schroedinger/opengl/schroopenglcanvas.h>
#include <schroedinger/opengl/schroopenglframe.h>
#include <schroedinger/opengl/schroopenglshader.h>
#include <stdio.h>

typedef struct _SchroOpenGLMotion SchroOpenGLMotion;

struct _SchroOpenGLMotion {
  SchroMotion *motion;
  SchroOpenGLCanvas *src_canvases[2][4];
  SchroOpenGLShader *shader_dc;
  SchroOpenGLShader *shader_ref_prec0;
  SchroOpenGLShader *shader_ref_prec0_weight;
  SchroOpenGLShader *shader_ref_prec1;
  SchroOpenGLShader *shader_ref_prec1_weight;
  SchroOpenGLShader *shader_ref_prec3a;
  SchroOpenGLShader *shader_ref_prec3a_weight;
  SchroOpenGLShader *shader_ref_prec3b;
  SchroOpenGLShader *shader_ref_prec3b_weight;
  SchroOpenGLCanvas *obmc_weight_canvas;
};
/*
static*/ void
schro_opengl_motion_render_dc_block (SchroOpenGLMotion *opengl_motion, int i,
    int x, int y, int u, int v)
{
  int xblen, yblen;
  SchroMotion *motion;
  SchroMotionVectorDC *motion_vector_dc;
  uint8_t dc;

  motion = opengl_motion->motion;
  motion_vector_dc = (SchroMotionVectorDC *)
      &motion->motion_vectors[v * motion->params->x_num_blocks + u];
  dc = (int) motion_vector_dc->dc[i] + 128;

  glUseProgramObjectARB (opengl_motion->shader_dc->program);

  glUniform1iARB (opengl_motion->shader_dc->textures[0], 0); /* input */ // FIXME: pre-bind on create
  glUniform1iARB (opengl_motion->shader_dc->textures[1], 1); /* obmc_weight */ // FIXME: pre-bind on create
  glUniform2fARB (opengl_motion->shader_dc->origin, x, y);
  glUniform1fARB (opengl_motion->shader_dc->dc, dc);

  if (x < 0) {
    xblen = motion->xblen + x;
    x = 0;
  } else {
    xblen = motion->xblen;
  }

  if (y < 0) {
    yblen = motion->yblen + y;
    y = 0;
  } else {
    yblen = motion->yblen;
  }

  schro_opengl_render_quad (x, y, xblen, yblen);
}
/*
static*/ void
schro_opengl_motion_render_ref_block (SchroOpenGLMotion *opengl_motion,
    int i, int x, int y, int u, int v, int ref)
{
  int /*sub1, sub2,*/ dx, dy, px, py, hx, hy, rx, ry;
  int weight, addend, divisor;
  SchroMotion *motion;
  SchroMotionVector *motion_vector;
  SchroChromaFormat chroma_format;
  SchroOpenGLShader *shader = NULL;

  motion = opengl_motion->motion;
  motion_vector = &motion->motion_vectors[v * motion->params->x_num_blocks + u];
  chroma_format = motion->params->video_format->chroma_format;

  SCHRO_ASSERT (motion_vector->using_global == FALSE);

  dx = motion_vector->dx[ref];
  dy = motion_vector->dy[ref];

  if (i > 0) {
    dx >>= SCHRO_CHROMA_FORMAT_H_SHIFT (chroma_format);
    dy >>= SCHRO_CHROMA_FORMAT_V_SHIFT (chroma_format);
  }

  px = (x << motion->mv_precision) + dx;
  py = (y << motion->mv_precision) + dy;
  weight = motion->ref1_weight + motion->ref2_weight;
  addend = 1 << (motion->ref_weight_precision - 1);
  divisor = 1 << motion->ref_weight_precision;

  #define SETUP_UNIFORMS_FOR_PREC_1_BLOCK(_index, _x, _y) \
      do { \
        int sub = (((_y) & 1) << 1) | ((_x) & 1); \
        glActiveTextureARB (GL_TEXTURE2_ARB + (_index)); \
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, \
            opengl_motion->src_canvases[ref][sub]->texture); \
        glActiveTextureARB (GL_TEXTURE0_ARB); \
        glUniform1iARB (shader->textures[2 + (_index)], 2 + (_index)); /* FIXME: pre-bind on create */ \
        glUniform2fARB (shader->offsets[_index], ((_x) >> 1) - x, ((_y) >> 1) - y); \
      } while (0)

  switch (motion->mv_precision) {
    case 0: // schro_upsampled_frame_get_block_fast_prec0
      if (weight != divisor) {
        shader = opengl_motion->shader_ref_prec0_weight;
      } else {
        shader = opengl_motion->shader_ref_prec0;
      }

      glUseProgramObjectARB (shader->program);

      glActiveTextureARB (GL_TEXTURE2_ARB);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
          opengl_motion->src_canvases[ref][0]->texture);
      glActiveTextureARB (GL_TEXTURE0_ARB);
      glUniform1iARB (shader->textures[2], 2); // FIXME: pre-bind on create

      glUniform2fARB (shader->offsets[0], px - x, py - y);
      break;
    case 1: // schro_upsampled_frame_get_block_fast_prec1
      if (weight != divisor) {
        shader = opengl_motion->shader_ref_prec1_weight;
      } else {
        shader = opengl_motion->shader_ref_prec1;
      }

      glUseProgramObjectARB (shader->program);

      SETUP_UNIFORMS_FOR_PREC_1_BLOCK (0, px, py);

      /*sub1 = ((py & 1) << 1) | (px & 1);

      glActiveTextureARB (GL_TEXTURE2_ARB);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
          opengl_motion->src_canvases[ref][sub1]->texture.handles[0]);
      glUniform1iARB (shader->textures[2], 2); // FIXME: pre-bind on create
      glActiveTextureARB (GL_TEXTURE0_ARB);

      glUniform2fARB (shader->offsets[0], (px >> 1) - x, (py >> 1) - y);*/
      break;
    case 2:
      px <<= 1;
      py <<= 1;
      /* fall through */
    case 3: // schro_upsampled_frame_get_block_fast_prec3
      hx = px >> 2;
      hy = py >> 2;
      rx = px & 0x3;
      ry = py & 0x3;

/*
void
schro_upsampled_frame_get_block_fast_prec1 (SchroUpsampledFrame *upframe, int k,
    int x, int y, SchroFrameData *fd)
{
  SchroFrameData *comp;
  int i;
  int j;

  i = ((y&1)<<1) | (x&1);
  x >>= 1;
  y >>= 1;

  comp = upframe->frames[i]->components + k;
  for(j=0;j<fd->height;j++) {
    uint8_t *dest = SCHRO_FRAME_DATA_GET_LINE (fd, j);
    uint8_t *src = SCHRO_FRAME_DATA_GET_LINE (comp, y + j);
    oil_memcpy (dest, src + x, fd->width);
  }
}*/

      switch ((ry << 2) | rx) {
        case 0: // schro_upsampled_frame_get_block_fast_prec1
          if (weight != divisor) {
            shader = opengl_motion->shader_ref_prec1_weight;
          } else {
            shader = opengl_motion->shader_ref_prec1;
          }

          glUseProgramObjectARB (shader->program);

          SETUP_UNIFORMS_FOR_PREC_1_BLOCK (0, hx, hy);

          /*sub1 = ((hy & 1) << 1) | (hx & 1);

          glActiveTextureARB (GL_TEXTURE2_ARB);
          glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
              opengl_motion->src_canvases[ref][sub1]->texture.handles[0]);
          glUniform1iARB (shader->textures[2 ], 2);
          glActiveTextureARB (GL_TEXTURE0_ARB);

          glUniform2fARB (shader->offsets[0], (hx >> 1) - x, (hy >> 1) - y);*/
          break;
        case 2:
        case 8:
          return;

          if (weight != divisor) {
            shader = opengl_motion->shader_ref_prec3a_weight;
          } else {
            shader = opengl_motion->shader_ref_prec3a;
          }

          glUseProgramObjectARB (shader->program);

          SETUP_UNIFORMS_FOR_PREC_1_BLOCK (0, hx, hy);

          if (rx == 0) {
            SETUP_UNIFORMS_FOR_PREC_1_BLOCK (1, hx, hy + 1);
          } else {
            SETUP_UNIFORMS_FOR_PREC_1_BLOCK (1, hx + 1, hy);
          }


          /*sub1 = ((hy & 1) << 1) | (hx & 1);

          if (rx == 0) {
            sub2 = (((hy + 1) & 1) << 1) | (hx & 1);
          } else {
            sub2 = ((hy & 1) << 1) | ((hx + 1) & 1);
          }

          glActiveTextureARB (GL_TEXTURE2_ARB);
          glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
              opengl_motion->src_canvases[ref][sub1]->texture.handles[0]);
          glUniform1iARB (shader->textures[2], 2); // FIXME: pre-bind on create
          glActiveTextureARB (GL_TEXTURE3_ARB);
          glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
              opengl_motion->src_canvases[ref][sub2]->texture.handles[0]);
          glUniform1iARB (shader->textures[3], 3); // FIXME: pre-bind on create
          glActiveTextureARB (GL_TEXTURE0_ARB);

          glUniform2fARB (shader->offsets[0], (hx >> 1) - x, (hy >> 1) - y);

          if (rx == 0) {
            glUniform2fARB (shader->offsets[1], (hx >> 1) - x, ((hy + 1) >> 1) - y);
          } else {
            glUniform2fARB (shader->offsets[1], ((hx + 1) >> 1) - x, (hy >> 1) - y);
          }*/

          break;
        default:
          return;

          if (weight != divisor) {
            shader = opengl_motion->shader_ref_prec3b_weight;
          } else {
            shader = opengl_motion->shader_ref_prec3b;
          }

          glUseProgramObjectARB (shader->program);

          SETUP_UNIFORMS_FOR_PREC_1_BLOCK (0, hx, hy);
          SETUP_UNIFORMS_FOR_PREC_1_BLOCK (1, hx + 1, hy);
          SETUP_UNIFORMS_FOR_PREC_1_BLOCK (2, hx, hy + 1);
          SETUP_UNIFORMS_FOR_PREC_1_BLOCK (3, hx + 1, hy + 1);

          glUniform2fARB (shader->remaining, rx, ry);
          break;
      }

      break;
    default:
      SCHRO_ASSERT (0);
      break;
  }

  SCHRO_ASSERT (shader != NULL);

  glUniform1iARB (shader->textures[0], 0); /* previous */ // FIXME: pre-bind on create
  glUniform1iARB (shader->textures[1], 1); /* obmc_weight */ // FIXME: pre-bind on create
  glUniform2fARB (shader->origin, x, y);

  if (weight != divisor) {
    glUniform1fARB (shader->weight, weight);
    glUniform1fARB (shader->addend, addend);
    glUniform1fARB (shader->divisor, divisor);
  }

  schro_opengl_render_quad (x, y, motion->xblen, motion->yblen);
}
/*
static*/ void
schro_opengl_motion_render_block (SchroOpenGLMotion *opengl_motion, int i,
    int x, int y, int u, int v)
{
  SchroMotion *motion;
  SchroMotionVector *motion_vector;

  motion = opengl_motion->motion;
  motion_vector = &motion->motion_vectors[v * motion->params->x_num_blocks + u];

  switch (motion_vector->pred_mode) {
    case 0:
      schro_opengl_motion_render_dc_block (opengl_motion, i, x, y, u, v);
      break;
    case 1:
      schro_opengl_motion_render_ref_block (opengl_motion, i, x, y, u, v, 0);
      break;
    case 2:
      schro_opengl_motion_render_ref_block (opengl_motion, i, x, y, u, v, 1);
      break;
    case 3:
      //schro_opengl_motion_render_biref_block (opengl_motion, i, x, y, u, v);
      break;
    default:
      SCHRO_ASSERT (0);
      break;
  }
}

void
schro_opengl_motion_render (SchroMotion *motion, SchroFrame *dest)
{
  int i, k, u, v;
  int x, y;
  SchroParams *params = motion->params;
  SchroOpenGLCanvas *dest_canvas;
  SchroChromaFormat chroma_format;
  SchroOpenGLShader *shader_copy;
  SchroOpenGLShader *shader_weight;
  SchroOpenGLShader *shader_clear;
  SchroOpenGLShader *shader_shift;
  SchroOpenGLMotion opengl_motion;

  SCHRO_ASSERT (SCHRO_FRAME_IS_OPENGL (dest));
  SCHRO_ASSERT (SCHRO_FRAME_FORMAT_DEPTH (dest->format)
      == SCHRO_FRAME_FORMAT_DEPTH_S16);

  if (params->num_refs == 1) {
    SCHRO_ASSERT (params->picture_weight_2 == 1);
  }

  SCHRO_ASSERT (params->picture_weight_1 < (1 << params->picture_weight_bits));
  SCHRO_ASSERT (params->picture_weight_2 < (1 << params->picture_weight_bits));

  // FIXME: hack to store custom data per frame component
  //dest_canvas = *((SchroOpenGLCanvas **) dest->components[0].data);
  dest_canvas = SCHRO_OPNEGL_CANVAS_FROM_FRAMEDATA (dest->components + 0);

  SCHRO_ASSERT (dest_canvas != NULL);

  schro_opengl_lock_context (dest_canvas->opengl);

  shader_copy = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_COPY_S16);
  shader_weight = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_WEIGHT);
  shader_clear = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_CLEAR);
  shader_shift = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_SHIFT);

  SCHRO_ASSERT (shader_copy != NULL);
  SCHRO_ASSERT (shader_weight != NULL);
  SCHRO_ASSERT (shader_clear != NULL);
  SCHRO_ASSERT (shader_shift != NULL);

  opengl_motion.motion = motion;
  opengl_motion.shader_dc = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_RENDER_DC);
  opengl_motion.shader_ref_prec0 = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_RENDER_REF_PREC_0);
  opengl_motion.shader_ref_prec0_weight = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_RENDER_REF_PREC_0_WEIGHT);
  opengl_motion.shader_ref_prec1 = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_RENDER_REF_PREC_1);
  opengl_motion.shader_ref_prec1_weight = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_RENDER_REF_PREC_1_WEIGHT);
  opengl_motion.shader_ref_prec3a = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_RENDER_REF_PREC_3a);
  opengl_motion.shader_ref_prec3a_weight = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_RENDER_REF_PREC_3a_WEIGHT);
  opengl_motion.shader_ref_prec3b = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_RENDER_REF_PREC_3b);
  opengl_motion.shader_ref_prec3b_weight = schro_opengl_shader_get (dest_canvas->opengl,
      SCHRO_OPENGL_SHADER_OBMC_RENDER_REF_PREC_3b_WEIGHT);

  SCHRO_ASSERT (opengl_motion.shader_dc != NULL);
  SCHRO_ASSERT (opengl_motion.shader_ref_prec0 != NULL);
  SCHRO_ASSERT (opengl_motion.shader_ref_prec0_weight != NULL);
  SCHRO_ASSERT (opengl_motion.shader_ref_prec1 != NULL);
  SCHRO_ASSERT (opengl_motion.shader_ref_prec1_weight != NULL);
  SCHRO_ASSERT (opengl_motion.shader_ref_prec3a != NULL);
  SCHRO_ASSERT (opengl_motion.shader_ref_prec3a_weight != NULL);
  SCHRO_ASSERT (opengl_motion.shader_ref_prec3b != NULL);
  SCHRO_ASSERT (opengl_motion.shader_ref_prec3b_weight != NULL);

  motion->ref_weight_precision = params->picture_weight_bits;
  motion->ref1_weight = params->picture_weight_1;
  motion->ref2_weight = params->picture_weight_2;
  motion->mv_precision = params->mv_precision;

  chroma_format = params->video_format->chroma_format;

  for (i = 0; i < 3; ++i) {
    // FIXME: hack to store custom data per frame component
    //dest_canvas = *((SchroOpenGLCanvas **) dest->components[i].data);
    dest_canvas = SCHRO_OPNEGL_CANVAS_FROM_FRAMEDATA (dest->components + i);

    SCHRO_ASSERT (dest_canvas != NULL);

    for (k = 0; k < 4; ++k) {
      if (motion->src1->frames[k]) {
        SCHRO_ASSERT (SCHRO_FRAME_IS_OPENGL (motion->src1->frames[k]));

        // FIXME: hack to store custom data per frame component
        //opengl_motion.src_canvases[0][k] = *((SchroOpenGLCanvas **)
        //    motion->src1->frames[k]->components[i].data);
        opengl_motion.src_canvases[0][k] = SCHRO_OPNEGL_CANVAS_FROM_FRAMEDATA (motion->src1->frames[k]->components + i);

        SCHRO_ASSERT (opengl_motion.src_canvases[0][k] != NULL);
        SCHRO_ASSERT (opengl_motion.src_canvases[0][k]->opengl
            == dest_canvas->opengl);
      } else {
        opengl_motion.src_canvases[0][k] = NULL;
      }
    }

    if (params->num_refs > 1) {
      for (k = 0; k < 4; ++k) {
        if (motion->src2->frames[k]) {
          SCHRO_ASSERT (SCHRO_FRAME_IS_OPENGL (motion->src2->frames[k]));

          // FIXME: hack to store custom data per frame component
          //opengl_motion.src_canvases[1][k] = *((SchroOpenGLCanvas **)
          //    motion->src2->frames[k]->components[i].data);
          opengl_motion.src_canvases[1][k] = SCHRO_OPNEGL_CANVAS_FROM_FRAMEDATA (motion->src2->frames[k]->components + i);

          SCHRO_ASSERT (opengl_motion.src_canvases[1][k] != NULL);
          SCHRO_ASSERT (opengl_motion.src_canvases[1][k]->opengl
              == dest_canvas->opengl);
        } else {
          opengl_motion.src_canvases[1][k] = NULL;
        }
      }
    }

    if (i == 0) {
      motion->xbsep = params->xbsep_luma;
      motion->ybsep = params->ybsep_luma;
      motion->xblen = params->xblen_luma;
      motion->yblen = params->yblen_luma;
    } else {
      motion->xbsep = params->xbsep_luma
          >> SCHRO_CHROMA_FORMAT_H_SHIFT (chroma_format);
      motion->ybsep = params->ybsep_luma
          >> SCHRO_CHROMA_FORMAT_V_SHIFT (chroma_format);
      motion->xblen = params->xblen_luma
          >> SCHRO_CHROMA_FORMAT_H_SHIFT (chroma_format);
      motion->yblen = params->yblen_luma
          >> SCHRO_CHROMA_FORMAT_V_SHIFT (chroma_format);
    }

    motion->width = dest->components[i].width;
    motion->height = dest->components[i].height;
    motion->xoffset = (motion->xblen - motion->xbsep) / 2;
    motion->yoffset = (motion->yblen - motion->ybsep) / 2;
    motion->max_fast_x = (motion->width - motion->xblen) << motion->mv_precision;
    motion->max_fast_y = (motion->height - motion->yblen) << motion->mv_precision;

    /* push obmc weight to texture */
    opengl_motion.obmc_weight_canvas
        = schro_opengl_get_obmc_weight_canvas (dest_canvas->opengl,
        motion->xblen, motion->yblen);

#if 0
    motion->obmc_weight.format = SCHRO_FRAME_FORMAT_S16_444;
    motion->obmc_weight.width = opengl_motion.obmc_weight_canvas->width;
    motion->obmc_weight.height = opengl_motion.obmc_weight_canvas->height;
    motion->obmc_weight.stride = motion->obmc_weight.width * sizeof (int16_t);
    motion->obmc_weight.data = schro_malloc (motion->obmc_weight.stride
        * motion->obmc_weight.height);

    schro_motion_init_obmc_weight (motion);

    schro_opengl_canvas_push (opengl_motion.obmc_weight_canvas,
        &motion->obmc_weight);
#else
    schro_opengl_setup_viewport (motion->xblen, motion->yblen);

    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT,
        opengl_motion.obmc_weight_canvas->framebuffer);

    glUseProgramObjectARB (shader_weight->program);
    glUniform2fARB (shader_weight->size, motion->xblen, motion->yblen);
    glUniform2fARB (shader_weight->offsets[0], motion->xoffset, motion->yoffset);

    schro_opengl_render_quad (0, 0, motion->xblen, motion->yblen);

    SCHRO_OPENGL_CHECK_ERROR

    glFlush();
#endif

    /* clear */
    schro_opengl_setup_viewport (motion->width, motion->height);

    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, dest_canvas->secondary->framebuffer);

    glUseProgramObjectARB (shader_clear->program);

    schro_opengl_render_quad (0, 0, motion->width, motion->height);

    SCHRO_OPENGL_CHECK_ERROR

    glFlush();

    /* render blocks */
    int passes[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };

    for (k = 0; k < 4; ++k) {
      /* copy */
      /*if (GLEW_EXT_framebuffer_blit) {
        glBindFramebufferEXT (GL_READ_FRAMEBUFFER_EXT,
            dest_canvas->framebuffers[1]);
        glBindFramebufferEXT (GL_DRAW_FRAMEBUFFER_EXT,
            dest_canvas->framebuffers[0]);
        glBlitFramebufferEXT (0, 0, motion->width, motion->height, 0, 0,
            motion->width, motion->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        SCHRO_OPENGL_CHECK_ERROR

        glFlush();
      } else*/ {
        glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, dest_canvas->framebuffer);
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
            dest_canvas->secondary->texture);

        glUseProgramObjectARB (shader_copy->program);

        schro_opengl_render_quad (0, 0, motion->width, motion->height);

        SCHRO_OPENGL_CHECK_ERROR

        glFlush();
      }

      /* render */
      glBindFramebufferEXT (GL_FRAMEBUFFER_EXT,
          dest_canvas->secondary->framebuffer);

      //glActiveTextureARB (GL_TEXTURE0_ARB);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, dest_canvas->texture);

      glActiveTextureARB (GL_TEXTURE1_ARB);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
          opengl_motion.obmc_weight_canvas->texture);
      glActiveTextureARB (GL_TEXTURE0_ARB);

      SCHRO_OPENGL_CHECK_ERROR

      for (v = passes[k][0]; v < params->y_num_blocks; v += 2) {
        y = motion->ybsep * v - motion->yoffset;

        for (u = passes[k][1]; u < params->x_num_blocks; u += 2) {
          x = motion->xbsep * u - motion->xoffset;

          schro_opengl_motion_render_block (&opengl_motion, i, x, y, u, v);
        }
      }

      SCHRO_OPENGL_CHECK_ERROR

      glFlush();
    }

    /* shift */
    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, dest_canvas->framebuffer);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, dest_canvas->secondary->texture);

    glUseProgramObjectARB (shader_shift->program);
    glUniform1iARB (shader_shift->textures[0], 0); // FIXME: pre-bind on create

    schro_opengl_render_quad (0, 0, motion->width, motion->height);

    SCHRO_OPENGL_CHECK_ERROR

    glFlush();

    schro_free (motion->obmc_weight.data);
  }

  glUseProgramObjectARB (0);
#if SCHRO_OPENGL_UNBIND_TEXTURES
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
  glActiveTextureARB (GL_TEXTURE1_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
  glActiveTextureARB (GL_TEXTURE2_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
  glActiveTextureARB (GL_TEXTURE3_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
  glActiveTextureARB (GL_TEXTURE4_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
  glActiveTextureARB (GL_TEXTURE5_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
  glActiveTextureARB (GL_TEXTURE0_ARB);
#endif
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);

  /*if (GLEW_EXT_framebuffer_blit) {
    glBindFramebufferEXT (GL_READ_FRAMEBUFFER_EXT, 0);
    glBindFramebufferEXT (GL_DRAW_FRAMEBUFFER_EXT, 0);
  }*/

  schro_opengl_unlock_context (dest_canvas->opengl);
}

