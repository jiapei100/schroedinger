
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <schroedinger/schro.h>
#include <liboil/liboil.h>
#include <string.h>
#include <math.h>

#define SCHRO_METRIC_INVALID (1<<24)

void schro_encoder_hierarchical_prediction (SchroEncoderTask *task);
void schro_encoder_hierarchical_prediction_2 (SchroEncoderTask *task);
void schro_encoder_zero_prediction (SchroEncoderTask *task);
static void schro_encoder_dc_prediction (SchroEncoderTask *task);
static void schro_motion_field_merge (SchroMotionField *dest,
    SchroMotionField **list, int n);
static void schro_motion_field_cleanup (SchroMotionField *mf, int x, int y);
static void schro_motion_field_combine (SchroMotionField *mf);
static void schro_motion_field_subpixel (SchroMotionField *mf);
void schro_motion_field_set (SchroMotionField *field, int split, int pred_mode);
void schro_motion_global_metric (SchroMotionField *mf, SchroFrame *frame,
    SchroFrame *ref);


int cost (int value)
{
  int n;
  if (value == 0) return 1;
  if (value < 0) value = -value;
  value++;
  n = 0;
  while (value) {
    n+=2;
    value>>=1;
  }
  return n;
}

void
schro_encoder_motion_predict (SchroEncoderTask *task)
{
  SchroParams *params = &task->params;
  SchroMotionField *fields[10];
  int i;
  int n;

  SCHRO_ASSERT(params->x_num_blocks != 0);
  SCHRO_ASSERT(params->y_num_blocks != 0);
  SCHRO_ASSERT(params->num_refs > 0);

  if (task->motion_field == NULL) {
    task->motion_field = schro_motion_field_new (params->x_num_blocks,
        params->y_num_blocks);
  }

  //schro_encoder_hierarchical_prediction (task);
  schro_encoder_hierarchical_prediction_2 (task);
  //schro_encoder_zero_prediction (task);

  if (params->have_global_motion) {
    schro_encoder_global_prediction (task);
  }

  schro_encoder_dc_prediction (task);

  n = 0;
  fields[n++] = task->motion_fields[SCHRO_MOTION_FIELD_HIER_REF0];
  if (params->num_refs > 1) {
    fields[n++] = task->motion_fields[SCHRO_MOTION_FIELD_HIER_REF1];
  }
  fields[n++] = task->motion_fields[SCHRO_MOTION_FIELD_DC];
  if (params->have_global_motion) {
    fields[n++] = task->motion_fields[SCHRO_MOTION_FIELD_GLOBAL_REF0];
    if (params->num_refs > 1) {
      fields[n++] = task->motion_fields[SCHRO_MOTION_FIELD_GLOBAL_REF1];
    }
  }

  schro_motion_field_merge (task->motion_field, fields, n);
  schro_motion_field_cleanup (task->motion_field,
      (params->video_format->width + params->xbsep_luma - 1)/params->xbsep_luma,
      (params->video_format->height + params->ybsep_luma - 1)/params->ybsep_luma);

  schro_motion_field_combine (task->motion_field);

  schro_motion_field_subpixel (task->motion_field);

  schro_motion_field_calculate_stats (task->motion_field, task);

  for(i=0;i<SCHRO_MOTION_FIELD_LAST;i++){
    if (task->motion_fields[i]) {
      schro_motion_field_free (task->motion_fields[i]);
      task->motion_fields[i] = NULL;
    }
  }
}

void
schro_motion_field_merge (SchroMotionField *dest,
    SchroMotionField **list, int n)
{
  int i,j,k;
  SchroMotionVector *mv;
  SchroMotionVector *mvk;

  for(k=0;k<n;k++){
    SCHRO_ASSERT(list[k]->x_num_blocks == dest->x_num_blocks);
    SCHRO_ASSERT(list[k]->y_num_blocks == dest->y_num_blocks);
  }

  for(j=0;j<dest->y_num_blocks;j++){
    for(i=0;i<dest->x_num_blocks;i++){
      mv = &dest->motion_vectors[j*dest->x_num_blocks + i];

      mvk = &list[0]->motion_vectors[j*dest->x_num_blocks + i];
      *mv = *mvk;
      for(k=1;k<n;k++){
        mvk = &list[k]->motion_vectors[j*dest->x_num_blocks + i];
        if (mvk->metric < mv->metric) {
          *mv = *mvk;
        }
      }
      if (mv->pred_mode == 2) {
        mv->x2 = mv->x1;
        mv->y2 = mv->y1;
      }
      SCHRO_ASSERT (!(mv->pred_mode == 0 && mv->using_global));
    }
  }
}

static void
schro_motion_field_cleanup (SchroMotionField *mf, int x_blocks, int y_blocks)
{
  int i,j;
  SchroMotionVector *mv;

  SCHRO_DEBUG("motion field cleanup %dx%d to %dx%d",
      x_blocks, y_blocks,
      mf->x_num_blocks, mf->y_num_blocks);
  if (x_blocks < mf->x_num_blocks) {
    for(j=0;j<mf->y_num_blocks;j++){
      for(i=x_blocks;i<mf->x_num_blocks;i++){
        mv = &mf->motion_vectors[j*mf->x_num_blocks + i];

        *mv = mf->motion_vectors[j*mf->x_num_blocks + (i-1)];
      }
    }
  }
  if (y_blocks < mf->y_num_blocks) {
    for(j=y_blocks;j<mf->y_num_blocks;j++){
      for(i=0;i<mf->x_num_blocks;i++){
        mv = &mf->motion_vectors[j*mf->x_num_blocks + i];

        *mv = mf->motion_vectors[(j-1)*mf->x_num_blocks + i];
      }
    }
  }
}

void
schro_motion_field_combine (SchroMotionField *mf)
{
  int i,j,k,l;
  SchroMotionVector *mv;

  for(j=0;j<mf->y_num_blocks;j+=4){
    for(i=0;i<mf->x_num_blocks;i+=4){
      mv = &mf->motion_vectors[j*mf->x_num_blocks + i];
      if (mv->pred_mode == 0) {
#if 0
        int dc[3] = { 0, 0, 0 };
        for(k=0;k<4;k++){
          for(l=0;l<4;l++){
            if (mv[k*mf->x_num_blocks + l].pred_mode != 0) goto next_mb;
            dc[0] += mv[k*mf->x_num_blocks + l].u.dc[0];
            dc[1] += mv[k*mf->x_num_blocks + l].u.dc[1];
            dc[2] += mv[k*mf->x_num_blocks + l].u.dc[2];
          }
        }
        mv->split = 0;
        mv->u.dc[0] = (dc[0] + 8)>>4;
        mv->u.dc[1] = (dc[1] + 8)>>4;
        mv->u.dc[2] = (dc[2] + 8)>>4;
#else
        continue;
#endif
      } else if (mv->using_global) {
        for(k=0;k<4;k++){
          for(l=0;l<4;l++){
            if (!mv[k*mf->x_num_blocks + l].using_global ||
                mv[k*mf->x_num_blocks + l].pred_mode != mv->pred_mode) {
              goto next_mb;
            }
          }
        }
        mv->split = 0;
      } else {
        goto next_mb;
      }
      mv[1] = mv[0];
      mv[2] = mv[0];
      mv[3] = mv[0];
      memcpy(mv + mf->x_num_blocks, mv, 4*sizeof(*mv));
      memcpy(mv + 2*mf->x_num_blocks, mv, 4*sizeof(*mv));
      memcpy(mv + 3*mf->x_num_blocks, mv, 4*sizeof(*mv));
next_mb:
      do {} while (0);
    }
  }
}

static void
schro_motion_vector_subpixel (SchroMotionField *mf, SchroMotionVector *mv,
    int ref)
{
  /* FIXME do something here */

  //mv->x1 += (rand()&0x7) - 3;
  //mv->y1 += (rand()&0x7) - 3;
}

static void
schro_motion_field_subpixel (SchroMotionField *mf)
{
  int i,j;
  SchroMotionVector *mv;

  for(j=0;j<mf->y_num_blocks;j++){
    for(i=0;i<mf->x_num_blocks;i++){
      mv = &mf->motion_vectors[j*mf->x_num_blocks + i];

      if (mv->using_global || mv->pred_mode == 0) continue;
      if (mv->pred_mode & 1) {
        schro_motion_vector_subpixel (mf, mv, 1);
      }
      if (mv->pred_mode & 2) {
        schro_motion_vector_subpixel (mf, mv, 2);
      }
    }
  }
}

void
schro_motion_field_calculate_stats (SchroMotionField *mf, SchroEncoderTask *task)
{
  int i,j;
  SchroMotionVector *mv;
  int ref1 = 0;
  int ref2 = 0;
  int bidir = 0;

  task->stats_dc = 0;
  task->stats_global = 0;
  task->stats_motion = 0;
  for(j=0;j<mf->y_num_blocks;j++){
    for(i=0;i<mf->x_num_blocks;i++){
      mv = &mf->motion_vectors[j*mf->x_num_blocks + i];
      if (mv->pred_mode == 0) {
        task->stats_dc++;
      } else {
        if (mv->using_global) {
          task->stats_global++;
        } else {
          task->stats_motion++;
        }
        if (mv->pred_mode == 1) {
          ref1++;
        } else if (mv->pred_mode == 2) {
          ref2++;
        } else {
          bidir++;
        }
      }
    }
  }
  SCHRO_INFO("dc %d global %d motion %d ref1 %d ref2 %d bidir %d",
      task->stats_dc, task->stats_global, task->stats_motion,
      ref1, ref2, bidir);
}

void
schro_encoder_global_prediction (SchroEncoderTask *task)
{
  SchroMotionField *mf, *mf_orig;
  int i;

  for(i=0;i<task->params.num_refs;i++) {
    if (i == 0) {
      mf_orig = task->motion_fields[SCHRO_MOTION_FIELD_HIER_REF0];
    } else {
      mf_orig = task->motion_fields[SCHRO_MOTION_FIELD_HIER_REF1];
    }
    mf = schro_motion_field_new (mf_orig->x_num_blocks, mf_orig->y_num_blocks);

    memcpy (mf->motion_vectors, mf_orig->motion_vectors,
        sizeof(SchroMotionVector)*mf->x_num_blocks*mf->y_num_blocks);
    schro_motion_field_global_prediction (mf, &task->params.global_motion[i],
        task->params.mv_precision);
    if (i == 0) {
      schro_motion_global_metric (mf, task->encode_frame,
          task->ref_frame0->original_frame);
    } else {
      schro_motion_global_metric (mf, task->encode_frame,
          task->ref_frame1->original_frame);
    }
    if (i == 0) {
      task->motion_fields[SCHRO_MOTION_FIELD_GLOBAL_REF0] = mf;
    } else {
      task->motion_fields[SCHRO_MOTION_FIELD_GLOBAL_REF1] = mf;
    }
  }
}

void
schro_motion_global_metric (SchroMotionField *field, SchroFrame *frame,
    SchroFrame *ref)
{
  SchroMotionVector *mv;
  int i;
  int j;
  int x,y;

  for(j=0;j<field->y_num_blocks;j++){
    for(i=0;i<field->x_num_blocks;i++){
      mv = field->motion_vectors + j*field->x_num_blocks + i;

      x = i*8 + mv->x1;
      y = j*8 + mv->y1;
#if 0
      mv->metric = schro_metric_absdiff_u8 (
            frame->components[0].data + x + y*frame->components[0].stride,
            frame->components[0].stride,
            ref->components[0].data + i*8 + j*8*ref->components[0].stride,
            ref->components[0].stride, 8, 8);
#endif
mv->metric = 0;
    }
  }
}

void
schro_motion_field_global_prediction (SchroMotionField *mf,
    SchroGlobalMotion *gm, int mv_precision)
{
  int i;
  int j;
  int k;
  SchroMotionVector *mv;

  for(j=0;j<mf->y_num_blocks;j++) {
    for(i=0;i<mf->x_num_blocks;i++) {
      mv = mf->motion_vectors + j*mf->x_num_blocks + i;

      mv->using_global = 1;

      /* HACK */
      if (j >= mf->y_num_blocks - 8 || i >= mf->x_num_blocks - 8) {
        mv->using_global = 0;
      }
    }
  }

  for(k=0;k<4;k++){
    double m_x, m_y;
    double m_f, m_g;
    double pan_x, pan_y;
    double ave_x, ave_y;
    double m_fx, m_fy, m_gx, m_gy;
    double m_xx, m_yy;
    double a00, a01, a10, a11;
    double sum2;
    double stddev2;
    int n = 0;

    SCHRO_DEBUG("step %d", k);
    m_x = 0;
    m_y = 0;
    m_f = 0;
    m_g = 0;
    for(j=0;j<mf->y_num_blocks;j++) {
      for(i=0;i<mf->x_num_blocks;i++) {
        mv = mf->motion_vectors + j*mf->x_num_blocks + i;
        if (mv->using_global) {
          m_f += mv->x1;
          m_g += mv->y1;
          m_x += i*8;
          m_y += j*8;
          n++;
        }
      }
    }
    pan_x = m_f / n;
    pan_y = m_g / n;
    ave_x = m_x / n;
    ave_y = m_y / n;

    SCHRO_DEBUG("pan %f %f ave %f %f n %d", pan_x, pan_y, ave_x, ave_y, n);

    m_fx = 0;
    m_fy = 0;
    m_gx = 0;
    m_gy = 0;
    m_xx = 0;
    m_yy = 0;
    n = 0;
    for(j=0;j<mf->y_num_blocks;j++) {
      for(i=0;i<mf->x_num_blocks;i++) {
        mv = mf->motion_vectors + j*mf->x_num_blocks + i;
        if (mv->using_global) {
          m_fx += (mv->x1 - pan_x) * (i*8 - ave_x);
          m_fy += (mv->x1 - pan_x) * (j*8 - ave_y);
          m_gx += (mv->y1 - pan_y) * (i*8 - ave_x);
          m_gy += (mv->y1 - pan_y) * (j*8 - ave_y);
          m_xx += (i*8 - ave_x) * (i*8 - ave_x);
          m_yy += (j*8 - ave_y) * (j*8 - ave_y);
          n++;
        }
      }
    }
    SCHRO_DEBUG("m_fx %f m_gx %f m_xx %f n %d", m_fx, m_gx, m_xx, n);
    a00 = m_fx / m_xx;
    a01 = m_fy / m_yy;
    a10 = m_gx / m_xx;
    a11 = m_gy / m_yy;

    pan_x -= a00*ave_x + a01*ave_y;
    pan_y -= a10*ave_x + a11*ave_y;

    SCHRO_DEBUG("pan %f %f a[] %f %f %f %f", pan_x, pan_y, a00, a01, a10, a11);

    sum2 = 0;
    for(j=0;j<mf->y_num_blocks;j++) {
      for(i=0;i<mf->x_num_blocks;i++) {
        mv = mf->motion_vectors + j*mf->x_num_blocks + i;
        if (mv->using_global) {
          double dx, dy;
          dx = mv->x1 - (pan_x + a00 * i + a01 * j);
          dy = mv->y1 - (pan_y + a10 * i + a11 * j);
          sum2 += dx * dx + dy * dy;
        }
      }
    }

    stddev2 = sum2/n;
    SCHRO_DEBUG("stddev %f", sqrt(sum2/n));

    if (stddev2 < 1) stddev2 = 1;

    n = 0;
    for(j=0;j<mf->y_num_blocks;j++) {
      for(i=0;i<mf->x_num_blocks;i++) {
        double dx, dy;
        mv = mf->motion_vectors + j*mf->x_num_blocks + i;
        dx = mv->x1 - (pan_x + a00 * i + a01 * j);
        dy = mv->y1 - (pan_y + a10 * i + a11 * j);
        mv->using_global = (dx * dx + dy * dy < stddev2*16);
        n += mv->using_global;
      }
    }
    SCHRO_DEBUG("using n = %d", n);

    gm->b0 = rint(pan_x*(0.125*(1<<mv_precision)));
    gm->b1 = rint(pan_y*(0.125*(1<<mv_precision)));
    gm->a_exp = 16;
    gm->a00 = rint((1.0 + a00/8) * (1<<(gm->a_exp + mv_precision)));
    gm->a01 = rint(a01/8 * (1<<(gm->a_exp + mv_precision)));
    gm->a10 = rint(a10/8 * (1<<(gm->a_exp + mv_precision)));
    gm->a11 = rint((1.0 + a11/8) * (1<<(gm->a_exp + mv_precision)));
#if 0
    gm->a_exp = 16;
    gm->a00 = 65536;
    gm->a01 = 0;
    gm->a10 = 0;
    gm->a11 = 65536;
#endif
  }

  for(j=0;j<mf->y_num_blocks;j++) {
    for(i=0;i<mf->x_num_blocks;i++) {
      mv = mf->motion_vectors + j*mf->x_num_blocks + i;
      mv->using_global = 1;
      //mv->x1 = gm->b0 + ((gm->a00 * (i*8) + gm->a01 * (j*8))>>gm->a_exp) - i*8;
      //mv->y1 = gm->b1 + ((gm->a10 * (i*8) + gm->a11 * (j*8))>>gm->a_exp) - j*8;
      mv->x1 = 0;
      mv->y1 = 0;
    }
  }
}


void
schro_motion_vector_scan (SchroMotionVector *mv, SchroFrame *frame,
    SchroFrame *ref, int x, int y, int dist)
{
  int i,j;
  int xmin;
  int xmax;
  int ymin;
  int ymax;
  int metric;
  int dx, dy;
  uint32_t metric_array[32];

  dx = mv->x1;
  dy = mv->y1;
  xmin = MAX(0, x + dx - dist);
  ymin = MAX(0, y + dy - dist);
  xmax = MIN(frame->width - 8, x + dx + dist);
  ymax = MIN(frame->height - 8, y + dy + dist);

  mv->metric = 256*8*8;

  if (xmin > xmax || ymin > ymax) return;

  if (ymax - ymin + 1 <= 32) {
    for(i=xmin;i<xmax;i++){
      oil_sad8x8_8xn_u8 (metric_array,
          frame->components[0].data + x + y*frame->components[0].stride,
          frame->components[0].stride,
          ref->components[0].data + i + ymin*ref->components[0].stride,
          ref->components[0].stride,
          ymax - ymin + 1);
      for(j=ymin;j<=ymax;j++){
        metric = metric_array[j-ymin] + abs(i - x) + abs(j - y);
        if (metric < mv->metric) {
          mv->x1 = i - x;
          mv->y1 = j - y;
          mv->metric = metric;
        }
      }
    }
  } else {
    for(j=ymin;j<=ymax;j++){
      for(i=xmin;i<=xmax;i++){

        metric = schro_metric_absdiff_u8 (
            frame->components[0].data + x + y*frame->components[0].stride,
            frame->components[0].stride,
            ref->components[0].data + i + j*ref->components[0].stride,
            ref->components[0].stride, 8, 8);
        metric += abs(i - x) + abs(j - y);
        if (metric < mv->metric) {
          mv->x1 = i - x;
          mv->y1 = j - y;
          mv->metric = metric;
        }
      }
    }  
  }  
}


SchroMotionField *
schro_motion_field_new (int x_num_blocks, int y_num_blocks)
{
  SchroMotionField *mf;

  mf = malloc(sizeof(SchroMotionField));
  memset (mf, 0, sizeof(SchroMotionField));
  mf->x_num_blocks = x_num_blocks;
  mf->y_num_blocks = y_num_blocks;
  mf->motion_vectors = malloc(sizeof(SchroMotionVector)*
      x_num_blocks*y_num_blocks);
  memset (mf->motion_vectors, 0, sizeof(SchroMotionVector)*
      x_num_blocks*y_num_blocks);

  return mf;
}

void
schro_motion_field_free (SchroMotionField *field)
{
  free (field->motion_vectors);
  free (field);
}

void
schro_motion_field_set (SchroMotionField *field, int split, int pred_mode)
{
  SchroMotionVector *mv;
  int i;
  int j;

  for(j=0;j<field->y_num_blocks;j++){
    for(i=0;i<field->x_num_blocks;i++){
      mv = field->motion_vectors + j*field->x_num_blocks + i;
      memset (mv, 0, sizeof (*mv));
      mv->split = split;
      mv->pred_mode = pred_mode;
    }
  }
}

void
schro_motion_field_scan (SchroMotionField *field,
    SchroFrame *frame, SchroFrame *ref, int dist)
{
  SchroMotionVector *mv;
  int i;
  int j;

  for(j=0;j<field->y_num_blocks;j++){
    for(i=0;i<field->x_num_blocks;i++){
      mv = field->motion_vectors + j*field->x_num_blocks + i;

      schro_motion_vector_scan (mv, frame, ref, i*8, j*8, dist);
    }
  }
}

void
schro_motion_field_inherit (SchroMotionField *field,
    SchroMotionField *parent)
{
  SchroMotionVector *mv;
  SchroMotionVector *pv;
  int i;
  int j;

  for(j=0;j<field->y_num_blocks;j++){
    for(i=0;i<field->x_num_blocks;i++){
      mv = field->motion_vectors + j*field->x_num_blocks + i;
      pv = parent->motion_vectors + (j>>1)*parent->x_num_blocks + (i>>1);
      *mv = *pv;
      mv->x1 *= 2;
      mv->y1 *= 2;
    }
  }
}

#if 0
void
schro_motion_field_copy (SchroMotionField *field, SchroMotionField *parent)
{
  SchroMotionVector *mv;
  SchroMotionVector *pv;
  int i;
  int j;

  for(j=0;j<field->y_num_blocks;j++){
    for(i=0;i<field->x_num_blocks;i++){
      mv = field->motion_vectors + j*field->x_num_blocks + i;
      pv = parent->motion_vectors + (j>>1)*parent->x_num_blocks + (i>>1);
      *mv = *pv;
    }
  }
}
#endif

#if 0
void
schro_motion_field_dump (SchroMotionField *field)
{
  SchroMotionVector *mv;
  int i;
  int j;

  for(j=0;j<field->y_num_blocks;j++){
    for(i=0;i<field->x_num_blocks;i++){
      mv = field->motion_vectors + j*field->x_num_blocks + i;
      printf("%d %d %d %d\n", i, j, mv->x1, mv->y1);
    }
  }
  exit(0);
}
#endif

static SchroFrame *
get_downsampled(SchroEncoderFrame *frame, int i)
{
  if (i==0) {
    return frame->original_frame;
  }
  return frame->downsampled_frames[i-1];
}

void
schro_encoder_hierarchical_prediction (SchroEncoderTask *task)
{
  SchroParams *params = &task->params;
  int i;
  int x_blocks;
  int y_blocks;
  SchroFrame *downsampled_ref0;
  SchroFrame *downsampled_frame;
  int shift;

  for(i=0;i<task->params.num_refs;i++){
    SchroMotionField **motion_fields;

    if (i == 0) {
      motion_fields = task->motion_fields + SCHRO_MOTION_FIELD_HIER_REF0;
    } else {
      motion_fields = task->motion_fields + SCHRO_MOTION_FIELD_HIER_REF1;
    }

    for(shift=3;shift>=0;shift--) {
      if (i == 0) {
        downsampled_ref0 = get_downsampled(task->ref_frame0,shift);
      } else {
        downsampled_ref0 = get_downsampled(task->ref_frame1,shift);
      }
      downsampled_frame = get_downsampled(task->encoder_frame,shift);

      x_blocks = ROUND_UP_SHIFT(params->x_num_blocks,shift);
      y_blocks = ROUND_UP_SHIFT(params->y_num_blocks,shift);

      motion_fields[shift] = schro_motion_field_new (x_blocks, y_blocks);
      if (shift == 3) {
        schro_motion_field_set (motion_fields[shift], 2, 1<<i);
        schro_motion_field_scan (motion_fields[shift], downsampled_frame,
            downsampled_ref0, 12);
      } else {
        schro_motion_field_inherit (motion_fields[shift],
            motion_fields[shift+1]);
        schro_motion_field_scan (motion_fields[shift], downsampled_frame,
            downsampled_ref0, 4);
      }
    }

    //schro_motion_field_dump (motion_fields[0]);
  }
}

static int
schro_block_average (uint8_t *dest, SchroFrameComponent *comp,
    int x, int y, int w, int h)
{
  int xmax = MIN(x + w, comp->width);
  int ymax = MIN(y + h, comp->height);
  int i,j;
  int n = 0;
  int sum = 0;
  int ave;

  for(j=y;j<ymax;j++){
    for(i=x;i<xmax;i++){
      sum += SCHRO_GET(comp->data, j*comp->stride + i, uint8_t);
    }
    n += xmax - x;
  }

  if (n == 0) {
    return SCHRO_METRIC_INVALID;
  }

  ave = (sum + n/2)/n;

  sum = 0;
  for(j=y;j<ymax;j++){
    for(i=x;i<xmax;i++){
      sum += abs(ave - SCHRO_GET(comp->data, j*comp->stride + i, uint8_t));
    }
  }

  *dest = ave;
  return sum;
}

void
schro_encoder_dc_prediction (SchroEncoderTask *task)
{
  SchroParams *params = &task->params;
  uint8_t const_data[16];
  int i;
  int j;
  int luma_w, luma_h;
  int chroma_w, chroma_h;
  SchroMotionField *motion_field;
  SchroFrame *frame = task->encode_frame;

  motion_field = schro_motion_field_new (params->x_num_blocks,
      params->y_num_blocks);

  /* FIXME 8x8 blocks */
  luma_w = 8;
  luma_h = 8;
  chroma_w = luma_w>>params->video_format->chroma_h_shift;
  chroma_h = luma_h>>params->video_format->chroma_v_shift;

  for(j=0;j<params->y_num_blocks;j++){
    for(i=0;i<params->x_num_blocks;i++){
      SchroMotionVectorDC *mvdc;
      int x,y;
      
      mvdc = (SchroMotionVectorDC *)(motion_field->motion_vectors + j*motion_field->x_num_blocks + i);

      memset(mvdc, 0, sizeof(*mvdc));
      mvdc->pred_mode = 0;
      mvdc->split = 2;
      mvdc->using_global = 0;
      schro_block_average (&mvdc->dc[0], frame->components + 0, i*luma_w, j*luma_h, luma_w, luma_h);
      schro_block_average (&mvdc->dc[1], frame->components + 1, i*chroma_w, j*chroma_h, chroma_w, chroma_h);
      schro_block_average (&mvdc->dc[2], frame->components + 2, i*chroma_w, j*chroma_h, chroma_w, chroma_h);

      memset (const_data, mvdc->dc[0], 16);

      x = i*8;
      y = j*8;
      mvdc->metric = schro_metric_absdiff_u8 (
          frame->components[0].data + x + y*frame->components[0].stride,
          frame->components[0].stride,
          const_data, 0, 8, 8);
      mvdc->metric += 50;
    }
  }

  task->motion_fields[SCHRO_MOTION_FIELD_DC] = motion_field;
}

#define motion_field_get(mf,x,y) \
  ((mf)->motion_vectors + (y)*(mf)->x_num_blocks + (x))

static int
schro_frame_get_metric (SchroFrame *frame1, int x1, int y1,
    SchroFrame *frame2, int x2, int y2)
{
  int metric;

  if (x1 < 0 || y1 < 0 || x1+8 > frame1->width ||
      y1+8 > frame1->height) return 64*255;
  if (x2 < 0 || y2 < 0 || x2+8 > frame2->width ||
      y2+8 > frame2->height) return 64*255;

  metric = schro_metric_absdiff_u8 (
      frame1->components[0].data + x1 + y1*frame1->components[0].stride,
      frame1->components[0].stride,
      frame2->components[0].data + x2 + y2*frame2->components[0].stride,
      frame2->components[0].stride, 8, 8);
  metric += abs(x1 - x2) + abs(y1 - y2);
  
  return metric;
}

void
schro_encoder_hierarchical_prediction_2 (SchroEncoderTask *task)
{
  SchroParams *params = &task->params;
  int ref;
  int x_blocks;
  int y_blocks;
  SchroFrame *downsampled_ref;
  SchroFrame *downsampled_frame;
  int shift;
  SchroMotionField *mf;
  SchroMotionField *parent_mf = NULL;

  for(ref=0;ref<task->params.num_refs;ref++){

    shift = 3;
    if (ref == 0) {
      downsampled_ref = get_downsampled(task->ref_frame0,shift);
    } else {
      downsampled_ref = get_downsampled(task->ref_frame1,shift);
    }
    downsampled_frame = get_downsampled(task->encoder_frame,shift);

    x_blocks = params->x_num_blocks>>shift;
    y_blocks = params->y_num_blocks>>shift;
    parent_mf = schro_motion_field_new (x_blocks, y_blocks);

    schro_motion_field_set (parent_mf, 2, 1<<ref);
    schro_motion_field_scan (parent_mf, downsampled_frame, downsampled_ref,
        12);

    for(shift=2;shift>=0;shift--) {
      int i,j;
      SchroMotionVector *mv;

      x_blocks = params->x_num_blocks>>shift;
      y_blocks = params->y_num_blocks>>shift;

      mf = schro_motion_field_new (x_blocks, y_blocks);

      if (ref == 0) {
        downsampled_ref = get_downsampled(task->ref_frame0,shift);
      } else {
        downsampled_ref = get_downsampled(task->ref_frame1,shift);
      }
      downsampled_frame = get_downsampled(task->encoder_frame,shift);

      for(j=0;j<mf->y_num_blocks;j++){
        for(i=0;i<mf->x_num_blocks;i++){
#define LIST_LENGTH 20
          int list_x[LIST_LENGTH], list_y[LIST_LENGTH];
          int n = 0;
          int l, k;
          int x, y;
          int metric;

          /* always test the zero vector */
          list_x[n] = 0;
          list_y[n] = 0;
          n++;

          /* inherit from parent */
          for(k=0;k<4;k++){
            int l = (i-1+2*(k&1))>>1;
            int m = (j-1+(k&2))>>1;
            if (l >= 0 && l < parent_mf->x_num_blocks &&
                m >= 0 && m < parent_mf->y_num_blocks) {
              mv = motion_field_get(parent_mf, l, m);
              list_x[n] = mv->x1 * 2;
              list_y[n] = mv->y1 * 2;
              n++;
            }
          }

          /* inherit from neighbors (only towards SE) */
          if (i > 0) {
            mv = motion_field_get (mf, i-1, j);
            list_x[n] = mv->x1;
            list_y[n] = mv->y1;
            n++;
          }
          if (j > 0) {
            mv = motion_field_get (mf, i, j-1);
            list_x[n] = mv->x1;
            list_y[n] = mv->y1;
            n++;
          }
          if (i > 0 && j > 0) {
            mv = motion_field_get (mf, i-1, j-1);
            list_x[n] = mv->x1;
            list_y[n] = mv->y1;
            n++;
          }
          
          SCHRO_ASSERT(n<=LIST_LENGTH);
          metric = schro_frame_get_metric (downsampled_frame,
              i * 8, j * 8, downsampled_ref, i*8 + list_x[0],
              j*8 + list_y[0]);
          x = list_x[0];
          y = list_y[0];
          for (l = 1; l < n; l++) {
            int m;

            m = schro_frame_get_metric (downsampled_frame,
                i * 8, j * 8, downsampled_ref, i*8 + list_x[l],
                j*8 + list_y[l]);
            if (m < metric) {
              metric = m;
              x = list_x[l];
              y = list_y[l];
            }
          }

          mv = motion_field_get (mf, i, j);
          mv->x1 = x;
          mv->y1 = y;
          mv->metric = metric;
          mv->split = 2;
          mv->pred_mode = (1<<ref);
        }
      }

      schro_motion_field_scan (mf, downsampled_frame, downsampled_ref, 4);

      schro_motion_field_free (parent_mf);
      parent_mf = mf;
    }

    {
      int i,j;
      SchroMotionVector *mv;

      /* look at subpixel */
      for(j=0;j<mf->y_num_blocks;j++){
        for(i=0;i<mf->x_num_blocks;i++){
          mv = motion_field_get (mf, i, j);
          mv->x1 <<= 3;
          mv->y1 <<= 3;
        }
      }
    }
    if (ref == 0) {
      task->motion_fields[SCHRO_MOTION_FIELD_HIER_REF0] = parent_mf;
    } else {
      task->motion_fields[SCHRO_MOTION_FIELD_HIER_REF1] = parent_mf;
    }
  }

}

void
schro_encoder_zero_prediction (SchroEncoderTask *task)
{
  SchroParams *params = &task->params;
  int ref;
  int x_blocks;
  int y_blocks;
  SchroFrame *downsampled_ref;
  SchroFrame *downsampled_frame;
  SchroMotionField *mf;

  for(ref=0;ref<task->params.num_refs;ref++){
    int i,j;
    SchroMotionVector *mv;

    x_blocks = params->x_num_blocks;
    y_blocks = params->y_num_blocks;

    mf = schro_motion_field_new (x_blocks, y_blocks);

    if (ref == 0) {
      downsampled_ref = get_downsampled(task->ref_frame0,0);
    } else {
      downsampled_ref = get_downsampled(task->ref_frame1,0);
    }
    downsampled_frame = get_downsampled(task->encoder_frame,0);

    for(j=0;j<mf->y_num_blocks;j++){
      for(i=0;i<mf->x_num_blocks;i++){
        int metric;
        
        metric = schro_frame_get_metric (downsampled_frame,
            i * 8, j * 8, downsampled_ref, i*8, j*8);

        mv = motion_field_get (mf, i, j);
        mv->x1 = 0;
        mv->y1 = 0;
        mv->metric = metric;
        mv->split = 2;
        mv->pred_mode = (1<<ref);
      }
    }

    if (ref == 0) {
      task->motion_fields[SCHRO_MOTION_FIELD_HIER_REF0] = mf;
    } else {
      task->motion_fields[SCHRO_MOTION_FIELD_HIER_REF1] = mf;
    }
  }
}

