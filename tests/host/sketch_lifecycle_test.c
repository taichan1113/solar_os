/* Exercise the actual Sketch callbacks/canvas/PNG paths with host peripherals. */
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
#include "../../src/apps/solar_os_sketch.c"

struct solar_os_gfx { size_t width, height; };
static struct solar_os_gfx display = {800,480};
static size_t allocations, allocation_number, fail_at;
static bool leased, manual_on, other_pointer, fail_enable;
static unsigned submits, synchronous_presents;
static char directory[100];

size_t strlcpy(char *d,const char *s,size_t n)
{size_t l=strlen(s);if(n){size_t c=l<n-1?l:n-1;memcpy(d,s,c);d[c]=0;}return l;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "fixture error";}
esp_err_t solar_os_log_write(solar_os_log_level_t l,const char *t,const char *f,...)
{(void)l;(void)t;(void)f;return ESP_OK;}
void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t c,const char *tag)
{(void)c;(void)tag;if(++allocation_number==fail_at)return NULL;void *p=malloc(n);if(p)allocations++;return p;}
void solar_os_memory_free(void *p){if(p){assert(allocations);allocations--;free(p);}}
esp_err_t solar_os_expansion_enable_acquire(const char *n,const void *owner)
{assert(!strcmp(n,"fixture-pointer") && owner==sketch_state_storage);if(fail_enable)return ESP_FAIL;leased=true;return ESP_OK;}
esp_err_t solar_os_expansion_enable_release(const char *n,const void *owner)
{assert(!strcmp(n,"fixture-pointer") && owner==sketch_state_storage);leased=false;return ESP_OK;}
size_t solar_os_input_source_count(void){return 1;}
bool solar_os_input_source_get(size_t n,solar_os_input_source_info_t *i)
{assert(n==0);i->ready=other_pointer || (!fail_enable && (leased || manual_on));i->capabilities=SOLAR_OS_INPUT_CAP_POINTER_ABSOLUTE;return true;}
solar_os_gfx_t *solar_os_context_gfx(solar_os_context_t *c){return c->gfx;}
void solar_os_context_set_graphics_active(solar_os_context_t *c,bool v){c->graphics_active=v;}
int solar_os_context_argc(const solar_os_context_t *c){return c->argc;}
const char *solar_os_context_argv(const solar_os_context_t *c,int i){return c->argv[i];}
void solar_os_context_finish(solar_os_context_t *c,int e,const char *s)
{(void)s;c->exit_requested=true;c->exit_code=e;}
size_t solar_os_gfx_width(const solar_os_gfx_t *g){return g->width;}
size_t solar_os_gfx_height(const solar_os_gfx_t *g){return g->height;}
solar_os_display_format_t solar_os_gfx_format(const solar_os_gfx_t *g){(void)g;return SOLAR_OS_DISPLAY_FORMAT_MONO1;}
void solar_os_gfx_clear(solar_os_gfx_t *g,solar_os_gfx_color_t c){(void)g;(void)c;}
void solar_os_gfx_set_color(solar_os_gfx_t *g,solar_os_gfx_color_t c){(void)g;(void)c;}
void solar_os_gfx_set_font(solar_os_gfx_t *g,solar_os_gfx_font_t f){(void)g;(void)f;}
size_t solar_os_gfx_text_fit(solar_os_gfx_t *g,const char *text,int width,char *out,size_t cap){(void)g;size_t n=strlen(text);if(n>=cap)n=cap-1;if(n>(size_t)(width>0?width/6:0))n=width>0?width/6:0;memmove(out,text,n);out[n]=0;return n;}
size_t solar_os_gfx_text_width(solar_os_gfx_t *g,const char *t){(void)g;return strlen(t)*(solar_os_ui_scale_percent()==140U?9:6);}
void solar_os_gfx_text(solar_os_gfx_t *g,int x,int y,const char *t){(void)g;(void)x;(void)y;(void)t;}
void solar_os_gfx_fill_rect(solar_os_gfx_t *g,int x,int y,int w,int h)
{assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=(int)g->width && y+h<=(int)g->height);}
void solar_os_gfx_rect(solar_os_gfx_t *g,int x,int y,int w,int h){solar_os_gfx_fill_rect(g,x,y,w,h);}
void solar_os_gfx_pixel(solar_os_gfx_t *g,int x,int y){assert(x>=0 && y>=0 && x<(int)g->width && y<(int)g->height);}
void solar_os_gfx_line(solar_os_gfx_t *g,int x,int y,int a,int b){solar_os_gfx_pixel(g,x,y);solar_os_gfx_pixel(g,a,b);}
void solar_os_gfx_circle(solar_os_gfx_t *g,int x,int y,int r){(void)g;(void)x;(void)y;(void)r;}
void solar_os_gfx_fill_circle(solar_os_gfx_t *g,int x,int y,int r){solar_os_gfx_circle(g,x,y,r);}
void solar_os_gfx_icon(solar_os_gfx_t *g,int x,int y,solar_os_gfx_icon_t i,solar_os_gfx_icon_size_t z)
{(void)i;solar_os_gfx_fill_rect(g,x,y,(int)z,(int)z);}
void solar_os_gfx_bitmap(solar_os_gfx_t *g,int x,int y,int w,int h,const uint8_t *b)
{assert(b);solar_os_gfx_fill_rect(g,x,y,w,h);}
void solar_os_gfx_bitmap_2bpp(solar_os_gfx_t *g,int x,int y,int w,int h,const uint8_t *b,size_t n,const solar_os_gfx_color_t p[4])
{(void)n;(void)p;solar_os_gfx_bitmap(g,x,y,w,h,b);}
void solar_os_gfx_present(solar_os_gfx_t *g){(void)g;synchronous_presents++;}
esp_err_t solar_os_gfx_submit(solar_os_gfx_t *g,uint64_t *n){(void)g;(void)n;submits++;return ESP_OK;}

esp_err_t solar_os_storage_resolve_path(const char *p,char *out,size_t n)
{return strlcpy(out,p,n)<n?ESP_OK:ESP_ERR_INVALID_SIZE;}
const char *solar_os_storage_mount_point(void){return directory;}
esp_err_t solar_os_storage_default_path(const char *p,char *out,size_t n)
{(void)p;return solar_os_storage_resolve_path(directory,out,n);}
esp_err_t solar_os_storage_mkdir(const char *p){return mkdir(p,0700)==0?ESP_OK:ESP_FAIL;}
esp_err_t solar_os_storage_sibling_path(const char *p,const char *suffix,char *out,size_t n)
{return snprintf(out,n,"%s%s",p,suffix)<(int)n?ESP_OK:ESP_ERR_INVALID_SIZE;}
esp_err_t solar_os_storage_sync_file(FILE *f){return fflush(f)==0?ESP_OK:ESP_FAIL;}
esp_err_t solar_os_storage_replace_file(const char *temp,const char *p,const char *backup)
{(void)backup;return rename(temp,p)==0?ESP_OK:ESP_FAIL;}
esp_err_t solar_os_storage_remove(const char *p){return unlink(p)==0?ESP_OK:ESP_FAIL;}
void solar_os_storage_browser_destroy(solar_os_storage_browser_t *b){(void)b;}
esp_err_t solar_os_storage_browser_create(solar_os_storage_browser_filter_t f,void *u,solar_os_storage_browser_t **b)
{(void)f;(void)u;*b=NULL;return ESP_FAIL;}
esp_err_t solar_os_storage_browser_open(solar_os_storage_browser_t *b,const char *p){(void)b;(void)p;return ESP_FAIL;}
const char *solar_os_storage_browser_path(const solar_os_storage_browser_t *b){(void)b;return directory;}
size_t solar_os_storage_browser_count(const solar_os_storage_browser_t *b){(void)b;return 0;}
size_t solar_os_storage_browser_cursor(const solar_os_storage_browser_t *b){(void)b;return 0;}
bool solar_os_storage_browser_entry(const solar_os_storage_browser_t *b,size_t n,solar_os_storage_browser_entry_t *e)
{(void)b;(void)n;(void)e;return false;}
void solar_os_storage_browser_move(solar_os_storage_browser_t *b,int n){(void)b;(void)n;}
esp_err_t solar_os_storage_browser_activate(solar_os_storage_browser_t *b,char *p,size_t n,bool *s)
{(void)b;(void)p;(void)n;(void)s;return ESP_FAIL;}
esp_err_t solar_os_stb_decode_rgb(const uint8_t *p,size_t n,uint32_t max,uint8_t **rgb,uint32_t *w,uint32_t *h)
{int x,y,c;*rgb=stbi_load_from_memory(p,(int)n,&x,&y,&c,3);if(!*rgb)return ESP_FAIL;if((uint64_t)x*y>max){stbi_image_free(*rgb);*rgb=NULL;return ESP_ERR_INVALID_SIZE;}*w=x;*h=y;return ESP_OK;}
void solar_os_stb_image_free(void *p){stbi_image_free(p);}

static solar_os_context_t begin(unsigned failure)
{
    assert(!allocations && !leased);
    sketch_state_storage=calloc(1,sizeof(sketch_state_t)); assert(sketch_state_storage);
    allocation_number=0;fail_at=failure;submits=synchronous_presents=0;
    return (solar_os_context_t){.gfx=&display,.argc=1};
}
static void end(solar_os_context_t *c)
{
    sketch_stop(c); solar_os_sketch_app.state_release_cleanup(); sketch_cleanup();
    assert(!allocations && !leased && !c->graphics_active);
    free(sketch_state_storage);sketch_state_storage=NULL;sketch_cleanup();
}
static void pointer(solar_os_context_t *c,solar_os_input_pointer_action_t a,int x,int y)
{
    solar_os_event_t e={.type=SOLAR_OS_EVENT_POINTER,.data.pointer={.mode=SOLAR_OS_INPUT_POINTER_ABSOLUTE,.action=a,.x=x,.y=y}};
    assert(sketch_event(c,&e));
}
static void tick(solar_os_context_t *c)
{solar_os_event_t e={.type=SOLAR_OS_EVENT_TICK};assert(sketch_event(c,&e));}
int main(void)
{
    strcpy(directory,"/tmp/solar-sketch-XXXXXX");assert(mkdtemp(directory));
    for(unsigned i=1;i<=3;i++){solar_os_context_t c=begin(i);assert(sketch_start(&c)==ESP_ERR_NO_MEM);end(&c);}
    solar_os_context_t c=begin(0);c.argc=3;assert(sketch_start(&c)==ESP_OK && c.exit_requested && c.exit_code==2);end(&c);
    c=begin(0);c.argc=2;strcpy(c.argv[1],"/nonexistent/sketch.png");assert(sketch_start(&c)==ESP_OK && c.exit_code==1);end(&c);
    fail_enable=true;c=begin(0);assert(sketch_start(&c)==ESP_OK && sketch.modal==SKETCH_MODAL_NO_POINTER && !leased);end(&c);
    other_pointer=true;c=begin(0);assert(sketch_start(&c)==ESP_OK && sketch.modal==SKETCH_MODAL_NONE && !leased);end(&c);
    other_pointer=fail_enable=false;c=begin(0);assert(sketch_start(&c)==ESP_OK && leased && sketch.modal==SKETCH_MODAL_NONE);
    assert(submits==1 && !synchronous_presents);
    sketch_layout_t l=sketch_layout(c.gfx);assert(l.canvas_x==64 && l.canvas_width==736 && l.canvas_height==392);
#if SOLAR_OS_BOARD_UI_SCALE_PERCENT == 140
    const sketch_button_symbol_layout_t symbol = sketch_draw_button_symbol_frame(c.gfx, 2, 2, 60, 49, "Import", false);
    assert(symbol.y == 5); /* 54 px label stays visible below its 16 px icon. */
    assert(solar_os_ui_scale_set_percent(100));
    sketch_layout_t original = sketch_layout(c.gfx);
    assert(memcmp(&l, &original, sizeof(l)) == 0);
    assert(solar_os_ui_scale_set_percent(140));
#endif
    pointer(&c,SOLAR_OS_INPUT_POINTER_PRESS,100,100);
    for(int x=101;x<=200;x++)pointer(&c,SOLAR_OS_INPUT_POINTER_MOVE,x,100);
    assert(submits==1);tick(&c);assert(submits==2);
    pointer(&c,SOLAR_OS_INPUT_POINTER_RELEASE,200,100);tick(&c);assert(submits==3 && !sketch.drawing);
    for(int x=100;x<=200;x++)assert(solar_os_sketch_canvas_get(&sketch.canvas,x-l.canvas_x,100-l.canvas_y)==3);
    solar_os_sketch_canvas_set(&sketch.canvas,10,10,1,0);
    solar_os_sketch_canvas_set(&sketch.canvas,11,10,2,0);
    sketch_suspend(&c);assert(!leased && sketch.suspended && allocations==3);
    sketch_resume(&c);sketch_resume(&c);assert(leased && !sketch.suspended && allocations==3);
    assert(solar_os_sketch_canvas_get(&sketch.canvas,10,10)==1);
    assert(sketch_save()==ESP_OK && !sketch.dirty);
    char first[SOLAR_OS_STORAGE_PATH_MAX];strcpy(first,sketch.path);
    solar_os_sketch_canvas_clear(&sketch.canvas,0);assert(sketch_load_image(first,true)==ESP_OK);
    assert(solar_os_sketch_canvas_get(&sketch.canvas,10,10)==1 && solar_os_sketch_canvas_get(&sketch.canvas,11,10)==2);
    assert(sketch_load_image(first,false)==ESP_OK && sketch.dirty && !sketch.path[0]);
    assert(sketch_save()==ESP_OK && strcmp(first,sketch.path));
    char second[SOLAR_OS_STORAGE_PATH_MAX];strcpy(second,sketch.path);
    char gif_path[SOLAR_OS_STORAGE_PATH_MAX];snprintf(gif_path,sizeof(gif_path),"%s/import.gif",directory);
    const unsigned char gif[]={ 'G','I','F','8','9','a',1,0,1,0,0x80,0,0,
        0,0,0,255,255,255,0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,0x3b };
    FILE *f=fopen(gif_path,"wb");assert(f && fwrite(gif,1,sizeof(gif),f)==sizeof(gif));assert(!fclose(f));
    assert(sketch_load_image(gif_path,false)==ESP_OK && sketch.dirty && !sketch.path[0]);
    manual_on=true;end(&c);assert(manual_on);
    manual_on=false;c=begin(0);c.argc=2;strcpy(c.argv[1],first);assert(sketch_start(&c)==ESP_OK && leased && !sketch.dirty);end(&c);
    assert(unlink(first)==0 && unlink(second)==0 && unlink(gif_path)==0 && rmdir(directory)==0);
    puts("sketch lifecycle, coalescing, color PNG save/open/import: PASS");return 0;
}
