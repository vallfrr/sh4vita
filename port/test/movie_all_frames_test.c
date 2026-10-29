// Batched clock updates must deliver the original decoded frames, including B pictures.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PLM_VIDEO_SKIP_B_ALLOWED() 0
#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg/pl_mpeg.h"
typedef struct { plm_t *reference; unsigned frames; } Check;
static void compare(plm_t *decoder, plm_frame_t *frame, void *user) {
    Check *c = user;
    assert(!decoder->video_decoder->skipped);
    plm_frame_t *r = plm_decode_video(c->reference); assert(r);
    assert(frame->time == r->time && frame->width == r->width && frame->height == r->height);
    plm_plane_t *a[3] = {&frame->y,&frame->cb,&frame->cr}, *b[3] = {&r->y,&r->cb,&r->cr};
    for (int i=0;i<3;i++) assert(a[i]->width==b[i]->width && a[i]->height==b[i]->height &&
                                !memcmp(a[i]->data,b[i]->data,(size_t)a[i]->width*a[i]->height));
    c->frames++;
}
int main(int argc,char **argv) {
    assert(argc>1);unsigned total=0;
    for(int i=1;i<argc;i++) {
        plm_t *p=plm_create_with_filename(argv[i]); Check c={plm_create_with_filename(argv[i]),0};
        assert(p&&c.reference);plm_set_audio_enabled(p,0);plm_set_audio_enabled(c.reference,0);
        plm_set_video_decode_callback(p,compare,&c);
        const double tick[]={0.005,0.080,0.130,0.240,0.009};
        unsigned j=0;
        while(plm_get_time(p)<12 && !plm_has_ended(p)) {plm_decode(p,tick[j%5]);j++;}
        assert(c.frames);printf("%s: %u timed frames identical to full sequential decode\n",argv[i],c.frames);total+=c.frames;
        plm_destroy(p);plm_destroy(c.reference);
    }
    printf("Total original YUV/PTS frames compared: %u\n",total);
}
