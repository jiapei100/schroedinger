
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <schroedinger/schro.h>
#include <schroedinger/schromotion.h>
#include <schroedinger/schropredict.h>
#include <schroedinger/schrodebug.h>
#include <schroedinger/schroutils.h>
#include <schroedinger/schrooil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <liboil/liboilrandom.h>
#include <liboil/liboil.h>

#include "common.h"

void upsample_line (uint8_t *dest, int dstr, uint8_t *src, int sstr, int n);
void ref_h_upsample (SchroFrame *dest, SchroFrame *src);
void ref_v_upsample (SchroFrame *dest, SchroFrame *src);

void test (int width, int height);

int failed = 0;

int
main (int argc, char *argv[])
{
  int width, height;

  schro_init();

  for(width=1;width<=20;width++){
    for(height=1;height<=20;height++){
      test (width, height);
    }
  }

  if (failed) {
    printf("FAILED\n");
  } else {
    printf("SUCCESS\n");
  }

  return failed;
}

void test (int width, int height)
{
  SchroFrame *frame;
  SchroFrame *frame_ref;
  SchroFrame *frame_test;
  char name[TEST_PATTERN_NAME_SIZE];
  int i;

  frame = schro_frame_new_and_alloc (SCHRO_FRAME_FORMAT_U8_420, width, height);
  frame_ref = schro_frame_new_and_alloc (SCHRO_FRAME_FORMAT_U8_420, width, height);
  frame_test = schro_frame_new_and_alloc (SCHRO_FRAME_FORMAT_U8_420, width, height);

  printf("HORIZONTAL %dx%d\n", width, height);
  for(i=0;i<test_pattern_get_n_generators();i++){
    test_pattern_generate (frame->components + 0, name, i);

    ref_h_upsample (frame_ref, frame);
    schro_frame_upsample_horiz (frame_test, frame);

    if (frame_compare (frame_ref, frame_test)) {
      printf("  pattern %s: OK\n", name);
    } else {
      printf("  pattern %s: broken\n", name);
      frame_data_dump_full (frame_test->components + 0,
          frame_ref->components + 0, frame->components + 0);
      failed = TRUE;
    }
  }

  printf("VERTICAL %dx%d\n", width, height);
  for(i=0;i<test_pattern_get_n_generators();i++){
    test_pattern_generate (frame->components + 0, name, i);

    ref_v_upsample (frame_ref, frame);
    schro_frame_upsample_vert (frame_test, frame);

    if (frame_compare (frame_ref, frame_test)) {
      printf("  pattern %s: OK\n", name);
    } else {
      printf("  pattern %s: broken\n", name);
      frame_data_dump_full (frame_test->components + 0,
          frame_ref->components + 0, frame->components + 0);
      failed = TRUE;
    }
  }
}


void
upsample_line (uint8_t *dest, int dstr, uint8_t *src, int sstr, int n)
{
  int i;
  int j;
  int x;
  int weights[10] = { 3, -11, 25, -56, 167, 167, -56, 25, -11, 3 };

  for(i=0;i<n;i++){
    x = 0;
    for(j=0;j<10;j++){
      x += weights[j] * SCHRO_GET(src, sstr * CLAMP(i+j-4,0,n-1), uint8_t);
    }
    x += 128;
    x >>= 8;
    SCHRO_GET(dest, dstr * i, uint8_t) = CLAMP(x, 0, 255);
  }
}

void
ref_h_upsample (SchroFrame *dest, SchroFrame *src)
{
  int j,k;
  uint8_t *d;
  uint8_t *s;

  for(k=0;k<3;k++){
    for(j=0;j<dest->components[k].height;j++){
      d = OFFSET(dest->components[k].data, dest->components[k].stride * j);
      s = OFFSET(src->components[k].data, src->components[k].stride * j);
      upsample_line (d, 1, s, 1, dest->components[k].width);
    }
  }
}

void
ref_v_upsample (SchroFrame *dest, SchroFrame *src)
{
  int i,k;
  SchroFrameData *scomp;
  SchroFrameData *dcomp;

  for(k=0;k<3;k++){
    dcomp = dest->components + k;
    scomp = src->components + k;
    for(i=0;i<dest->components[k].width;i++){
      upsample_line (OFFSET(dcomp->data, i), dcomp->stride,
          OFFSET(scomp->data, i), scomp->stride,
          dcomp->height);
    }
  }
}

