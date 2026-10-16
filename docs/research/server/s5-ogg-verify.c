/* s5-ogg-verify.c -- prove the minimal Ogg page writer is byte-exact vs libogg.
 *
 * libogg's own test suite (src/framing.c) hard-codes the expected page headers
 * for ten packet sequences.  ogg_vectors.h is generated from that file by
 * s5-ogg-vectors.h's documented producer; this program replays those sequences
 * through the writer and compares the emitted page headers byte for byte.
 *
 * VERIFIED RESULT: 27/27 page headers identical to libogg (see the research note).
 *
 *   curl -sLO https://github.com/xiph/ogg/archive/refs/heads/master.tar.gz
 *   tar xzf master.tar.gz && cd ogg-master
 *   cc -O2 -I../docs/research/server -o s5-ogg-verify ../docs/research/server/s5-ogg-verify.c
 *   ./s5-ogg-verify
 */
#include <stdio.h>
#include <string.h>
#include "s5-ogg-vectors.h"
#include "s5-ogg-page-writer.c"

static int walker_len(const int *hdr){ return 27+hdr[26]; }   /* header bytes only */

int main(void){
    int fails=0, checks=0;
    for(unsigned c=0;c<VEC_NCASES;c++){
        const vec_case_t*v=&VEC_CASES[c];
        static oggmux_t m; static uint8_t out[2000000]; static uint8_t pkt[300000];
        oggmux_init(&m,0x04030201); int np=0;
        for(int i=0;i<v->npk;i++){
            for(int j=0;j<v->lens[i];j++)pkt[j]=(uint8_t)(i+j);
            np+=oggmux_packet(&m,pkt,v->lens[i],7+1024ULL*(uint64_t)i,out+np,i==v->npk-1);
            for(;;){int w=oggmux_flush(&m,out+np,0); if(!w)break; np+=w;}
        }
        for(;;){int w=oggmux_flush(&m,out+np,1); if(!w)break; np+=w;}
        int off=0,pg=0,ok=1;
        while(off<np){
            int ns=out[off+26],blen=0;
            for(int i=0;i<ns;i++)blen+=out[off+27+i];
            if(pg<v->nexp){
                int hl=walker_len(v->hdrs[pg]);
                checks++;
                int same=1; for(int i=0;i<hl;i++) if(out[off+i]!=(uint8_t)v->hdrs[pg][i]) { same=0; break; }
                if(!same){
                    ok=0; fails++;
                    printf("  FAIL [%s] page%d\n    got=",v->label,pg);
                    for(int i=0;i<hl;i++)printf("%02x",out[off+i]);
                    printf("\n    exp=");
                    for(int i=0;i<hl;i++)printf("%02x",(uint8_t)v->hdrs[pg][i]);
                    printf("\n");
                }
            }
            off+=27+ns+blen; pg++;
        }
        if(pg!=v->nexp){ ok=0; fails++; printf("  FAIL [%s]: %d pages, expected %d\n",v->label,pg,v->nexp); }
        if(ok) printf("  OK   %-42s %d page(s), headers byte-identical to libogg\n",v->label,pg);
    }
    printf("\n%s: %d/%d page headers match libogg exactly (%d failures)\n",
           fails?"FAILED":"ALL CHECKS PASSED",checks-fails,checks,fails);
    return fails!=0;
}
