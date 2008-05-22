
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <schroedinger/schro.h>
#include <schroedinger/opengl/schroopengl.h>
#include <schroedinger/opengl/schroopenglframe.h>
#include <schroedinger/opengl/schroopenglshader.h>
#include <liboil/liboil.h>

#define CONVERT_PROTOTYPE(func) \
    static void schro_opengl_frame_convert_##func (SchroFrame *dest, \
    SchroFrame *src)

CONVERT_PROTOTYPE (u8_s16);
CONVERT_PROTOTYPE (s16_u8);
CONVERT_PROTOTYPE (u8_u8);
CONVERT_PROTOTYPE (s16_s16);
CONVERT_PROTOTYPE (u8_422_yuyv);
CONVERT_PROTOTYPE (u8_422_uyvy);
CONVERT_PROTOTYPE (u8_444_ayuv);
CONVERT_PROTOTYPE (yuyv_u8_422);
CONVERT_PROTOTYPE (uyvy_u8_422);
CONVERT_PROTOTYPE (ayuv_u8_444);

#undef CONVERT_PROTOTYPE

typedef void (*SchroOpenGLFrameBinaryFunc) (SchroFrame *dest, SchroFrame *src);

struct FormatToFunction {
  SchroFrameFormat dest;
  SchroFrameFormat src;
  SchroOpenGLFrameBinaryFunc func;
};

#define CONVERT_MAPPING(dest, src, func) \
    { SCHRO_FRAME_FORMAT_##dest, SCHRO_FRAME_FORMAT_##src, \
    schro_opengl_frame_convert_##func }

static struct FormatToFunction schro_opengl_frame_convert_func_list[] = {
  /* S16 -> U8 */
  CONVERT_MAPPING (U8_444, S16_444, u8_s16),
  CONVERT_MAPPING (U8_422, S16_422, u8_s16),
  CONVERT_MAPPING (U8_420, S16_420, u8_s16),

  /* U8 -> S16 */
  CONVERT_MAPPING (S16_444, U8_444, s16_u8),
  CONVERT_MAPPING (S16_422, U8_422, s16_u8),
  CONVERT_MAPPING (S16_420, U8_420, s16_u8),

  /* U8 -> U8 */
  CONVERT_MAPPING (U8_444, U8_444, u8_u8),
  CONVERT_MAPPING (U8_422, U8_422, u8_u8),
  CONVERT_MAPPING (U8_420, U8_420, u8_u8),

  /* S16 -> S16 */
  CONVERT_MAPPING (S16_444, S16_444, s16_s16),
  CONVERT_MAPPING (S16_422, S16_422, s16_s16),
  CONVERT_MAPPING (S16_420, S16_420, s16_s16),

  /* packed -> U8 */
  CONVERT_MAPPING (U8_422, YUYV, u8_422_yuyv),
  CONVERT_MAPPING (U8_422, UYVY, u8_422_uyvy),
  CONVERT_MAPPING (U8_444, AYUV, u8_444_ayuv),

  /* U8 -> packed */
  CONVERT_MAPPING (YUYV, U8_422, yuyv_u8_422),
  CONVERT_MAPPING (UYVY, U8_422, uyvy_u8_422),
  CONVERT_MAPPING (AYUV, U8_444, ayuv_u8_444),

  { 0, 0, NULL }
};

#undef CONVERT_MAPPING

void
schro_opengl_frame_convert (SchroFrame *dest, SchroFrame *src)
{
  int i;

  SCHRO_ASSERT (dest != NULL);
  SCHRO_ASSERT (src != NULL);
  SCHRO_ASSERT (SCHRO_FRAME_IS_OPENGL (dest));
  SCHRO_ASSERT (SCHRO_FRAME_IS_OPENGL (src));

  for (i = 0; schro_opengl_frame_convert_func_list[i].func; ++i) {
    if (schro_opengl_frame_convert_func_list[i].dest == dest->format
        && schro_opengl_frame_convert_func_list[i].src == src->format) {
      schro_opengl_frame_convert_func_list[i].func (dest, src);
      return;
    }
  }

  SCHRO_ERROR ("conversion unimplemented (%d -> %d)", src->format,
      dest->format);
  SCHRO_ASSERT (0);
}

static void
schro_opengl_frame_convert_with_shader (SchroFrame *dest, SchroFrame *src,
    int shader_index)
{
  int i;
  int width, height;
  SchroOpenGLFrameData *dest_opengl_data = NULL;
  SchroOpenGLFrameData *src_opengl_data = NULL;
  SchroOpenGLShader *shader;

  schro_opengl_lock ();

  shader = schro_opengl_shader_get (shader_index);

  SCHRO_ASSERT (shader);

  for (i = 0; i < 3; ++i) {
    dest_opengl_data = (SchroOpenGLFrameData *) dest->components[i].data;
    src_opengl_data = (SchroOpenGLFrameData *) src->components[i].data;

    SCHRO_ASSERT (dest_opengl_data != NULL);
    SCHRO_ASSERT (src_opengl_data != NULL);

    width = MAX (dest->components[i].width, src->components[i].width);
    height = MAX (dest->components[i].height, src->components[i].height);

    schro_opengl_setup_viewport (width, height);

    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT,
                          dest_opengl_data->framebuffers[0]);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
                   src_opengl_data->texture.handles[0]);

    SCHRO_OPENGL_CHECK_ERROR

    glUseProgramObjectARB (shader->program);
    glUniform1iARB (shader->textures[0], 0);

    schro_opengl_render_quad (0, 0, width, height);

    glUseProgramObjectARB (0);

    glFlush ();

    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
  }

  schro_opengl_unlock ();
}

static void
schro_opengl_frame_unpack_with_shader (SchroFrame *dest, SchroFrame *src,
    int shader_y_index, int shader_u_index, int shader_v_index)
{
  int i;
  int width, height;
  SchroOpenGLFrameData *dest_opengl_data = NULL;
  SchroOpenGLFrameData *src_opengl_data = NULL;
  SchroOpenGLShader *shader;
  int shader_indices[] = { shader_y_index, shader_u_index, shader_v_index };

  schro_opengl_lock ();

  src_opengl_data = (SchroOpenGLFrameData *) src->components[0].data;

  SCHRO_ASSERT (src_opengl_data != NULL);

  for (i = 0; i < 3; ++i) {
    dest_opengl_data = (SchroOpenGLFrameData *) dest->components[i].data;

    SCHRO_ASSERT (dest_opengl_data != NULL);

    shader = schro_opengl_shader_get (shader_indices[i]);

    SCHRO_ASSERT (shader);

    /*width = MAX (dest->components[i].width, src->components[0].width);
    height = MAX (dest->components[i].height, src->components[0].height);*/
    // FIXME: edge extend is unimplemented
    width = dest->components[i].width;
    height = dest->components[i].height;

    schro_opengl_setup_viewport (width, height);

    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT,
                          dest_opengl_data->framebuffers[0]);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
                   src_opengl_data->texture.handles[0]);

    SCHRO_OPENGL_CHECK_ERROR

    glUseProgramObjectARB (shader->program);
    glUniform1iARB (shader->textures[0], 0);

    schro_opengl_render_quad (0, 0, width, height);

    glUseProgramObjectARB (0);

    glFlush ();

    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
  }

  schro_opengl_unlock ();
}

static void
schro_opengl_frame_pack_with_shader (SchroFrame *dest, SchroFrame *src,
    int shader_index)
{
  int width, height;
  SchroOpenGLFrameData *dest_opengl_data = NULL;
  SchroOpenGLFrameData *src_opengl_y_data = NULL;
  SchroOpenGLFrameData *src_opengl_u_data = NULL;
  SchroOpenGLFrameData *src_opengl_v_data = NULL;
  SchroOpenGLShader *shader;

  schro_opengl_lock ();

  src_opengl_y_data = (SchroOpenGLFrameData *) src->components[0].data;
  src_opengl_u_data = (SchroOpenGLFrameData *) src->components[1].data;
  src_opengl_v_data = (SchroOpenGLFrameData *) src->components[2].data;

  SCHRO_ASSERT (src_opengl_y_data != NULL);
  SCHRO_ASSERT (src_opengl_u_data != NULL);
  SCHRO_ASSERT (src_opengl_v_data != NULL);

  shader = schro_opengl_shader_get (shader_index);

  SCHRO_ASSERT (shader);

  dest_opengl_data = (SchroOpenGLFrameData *) dest->components[0].data;

  SCHRO_ASSERT (dest_opengl_data != NULL);

  /*width = MAX (dest->components[i].width, src->components[0].width);
  height = MAX (dest->components[i].height, src->components[0].height);*/
  // FIXME: edge extend is unimplemented
  width = dest->components[0].width;
  height = dest->components[0].height;

  schro_opengl_setup_viewport (width, height);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, dest_opengl_data->framebuffers[0]);
  glActiveTextureARB (GL_TEXTURE0_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
      src_opengl_y_data->texture.handles[0]);
  glActiveTextureARB (GL_TEXTURE1_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
      src_opengl_u_data->texture.handles[0]);
  glActiveTextureARB (GL_TEXTURE2_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB,
      src_opengl_v_data->texture.handles[0]);
  glActiveTextureARB (GL_TEXTURE0_ARB);

  SCHRO_OPENGL_CHECK_ERROR

  glUseProgramObjectARB (shader->program);
  glUniform1iARB (shader->textures[0], 0);
  glUniform1iARB (shader->textures[1], 1);
  glUniform1iARB (shader->textures[2], 2);

  schro_opengl_render_quad (0, 0, width, height);

  glUseProgramObjectARB (0);

  glFlush ();

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);

  schro_opengl_unlock ();
}

static void
schro_opengl_frame_convert_u8_s16 (SchroFrame *dest, SchroFrame *src)
{
  schro_opengl_frame_convert_with_shader (dest, src,
      SCHRO_OPENGL_SHADER_CONVERT_U8_S16);
}

static void
schro_opengl_frame_convert_s16_u8 (SchroFrame *dest, SchroFrame *src)
{
  schro_opengl_frame_convert_with_shader (dest, src,
      SCHRO_OPENGL_SHADER_CONVERT_S16_U8);
}

static void
schro_opengl_frame_convert_u8_u8 (SchroFrame *dest, SchroFrame *src)
{
  schro_opengl_frame_convert_with_shader (dest, src,
      SCHRO_OPENGL_SHADER_CONVERT_U8_U8);
}

static void
schro_opengl_frame_convert_s16_s16 (SchroFrame *dest, SchroFrame *src)
{
  schro_opengl_frame_convert_with_shader (dest, src,
      SCHRO_OPENGL_SHADER_CONVERT_S16_S16);
}

static void
schro_opengl_frame_convert_u8_422_yuyv (SchroFrame *dest, SchroFrame *src)
{
  schro_opengl_frame_unpack_with_shader (dest, src,
      SCHRO_OPENGL_SHADER_CONVERT_U8_Y4_YUYV,
      SCHRO_OPENGL_SHADER_CONVERT_U8_U2_YUYV,
      SCHRO_OPENGL_SHADER_CONVERT_U8_V2_YUYV);
}

static void
schro_opengl_frame_convert_u8_422_uyvy (SchroFrame *dest, SchroFrame *src)
{
  schro_opengl_frame_unpack_with_shader (dest, src,
      SCHRO_OPENGL_SHADER_CONVERT_U8_Y4_UYVY,
      SCHRO_OPENGL_SHADER_CONVERT_U8_U2_UYVY,
      SCHRO_OPENGL_SHADER_CONVERT_U8_V2_UYVY);
}

static void
schro_opengl_frame_convert_u8_444_ayuv (SchroFrame *dest, SchroFrame *src)
{
  schro_opengl_frame_unpack_with_shader (dest, src,
      SCHRO_OPENGL_SHADER_CONVERT_U8_Y4_AYUV,
      SCHRO_OPENGL_SHADER_CONVERT_U8_U4_AYUV,
      SCHRO_OPENGL_SHADER_CONVERT_U8_V4_AYUV);
}

static void
schro_opengl_frame_convert_yuyv_u8_422 (SchroFrame *dest, SchroFrame *src)
{
  schro_opengl_frame_pack_with_shader (dest, src,
      SCHRO_OPENGL_SHADER_CONVERT_YUYV_U8_422);
}

static void
schro_opengl_frame_convert_uyvy_u8_422 (SchroFrame *dest, SchroFrame *src)
{
  schro_opengl_frame_pack_with_shader (dest, src,
      SCHRO_OPENGL_SHADER_CONVERT_UYVY_U8_422);
}

static void
schro_opengl_frame_convert_ayuv_u8_444 (SchroFrame *dest, SchroFrame *src)
{
  schro_opengl_frame_pack_with_shader (dest, src,
      SCHRO_OPENGL_SHADER_CONVERT_AYUV_U8_444);
}

