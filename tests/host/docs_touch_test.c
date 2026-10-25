#include <assert.h>
#include <stdlib.h>
#include "../../src/apps/solar_os_docs_app.c"
static unsigned renders, leases, releases, launches;
static solar_os_manual_page_t pages[12];
const solar_os_app_t solar_os_reader_app={.name="reader"}, solar_os_less_app={.name="less"};
size_t strlcpy(char *d,const char *s,size_t n) { size_t l=strlen(s); if(n){size_t c=l<n-1?l:n-1;memcpy(d,s,c);d[c]=0;}return l; }
size_t solar_os_manual_count(void) {return 12;}
const solar_os_manual_page_t *solar_os_manual_get(size_t i){return i<12?&pages[i]:NULL;}
const solar_os_manual_page_t *solar_os_manual_find(const char *id){(void)id;return NULL;}
size_t solar_os_tui_rows(const solar_os_tui_t *t){(void)t;return 8;}
size_t solar_os_tui_cols(const solar_os_tui_t *t){(void)t;return 50;}
size_t solar_os_tui_screen_content_rows(const solar_os_tui_t *t,size_t a,size_t b){(void)t;return 8-a-b;}
void solar_os_tui_clear(solar_os_tui_t *t){(void)t;}
void solar_os_tui_refresh(solar_os_tui_t *t){(void)t;renders++;}
esp_err_t solar_os_tui_set_cursor_visible(solar_os_tui_t *t,bool b){(void)t;(void)b;return ESP_OK;}
esp_err_t solar_os_tui_draw_title(solar_os_tui_t *t,const char *a,const char *b){(void)t;(void)a;(void)b;return ESP_OK;}
esp_err_t solar_os_tui_draw_help(solar_os_tui_t *t,const char *a){(void)t;(void)a;return ESP_OK;}
esp_err_t solar_os_tui_draw_too_small(solar_os_tui_t *t,const char *a){(void)t;(void)a;return ESP_OK;}
esp_err_t solar_os_tui_write_cell(solar_os_tui_t *t,size_t r,size_t c,size_t w,const char *s,uint8_t a){(void)t;(void)r;(void)c;(void)w;(void)s;(void)a;return ESP_OK;}
bool solar_os_terminal_hit_cell(const solar_os_terminal_t *t,int x,int y,size_t *r,size_t *c){(void)t;if(x<0||y<0||y>=160)return false;*r=y/20;*c=x/10;return true;}
solar_os_shell_io_t *solar_os_context_shell_io(solar_os_context_t *c){(void)c;return NULL;}
solar_os_terminal_t *solar_os_context_terminal(solar_os_context_t *c){(void)c;return (solar_os_terminal_t*)1;}
solar_os_terminal_t *solar_os_shell_io_terminal(solar_os_shell_io_t *io){(void)io;return (solar_os_terminal_t*)1;}
solar_os_gfx_t *solar_os_context_gfx(solar_os_context_t *c){(void)c;return (solar_os_gfx_t*)1;}
esp_err_t solar_os_context_request_launch_ex(solar_os_context_t *c,const solar_os_app_t *a,int n,char **v,solar_os_launch_policy_t p){(void)c;assert(a==&solar_os_reader_app&&n==2&&!strcmp(v[0],"reader")&&!strcmp(v[1],"man:page")&&p==SOLAR_OS_LAUNCH_CHILD_RETURN);launches++;return ESP_OK;}
void solar_os_context_finish(solar_os_context_t *c,int n,const char *s){(void)s;c->exit_requested=true;c->exit_code=n;}
esp_err_t solar_os_expansion_enable_acquire(const char *n,const void *o){assert(!strcmp(n,"fixture-pointer")&&o==&docs_app.touch);leases++;return ESP_OK;}
esp_err_t solar_os_expansion_enable_release(const char *n,const void *o){assert(!strcmp(n,"fixture-pointer")&&o==&docs_app.touch);releases++;return ESP_OK;}
void vTaskDelay(TickType_t t){(void)t;assert(false);}
static void pointer(solar_os_context_t *ctx,solar_os_input_pointer_action_t a,int y){solar_os_event_t e={.type=SOLAR_OS_EVENT_POINTER,.data.pointer={.mode=SOLAR_OS_INPUT_POINTER_ABSOLUTE,.action=a,.x=60,.y=y}};docs_app_event(ctx,&e);}
int main(void){
 docs_app_state_t state={.tui.terminal=(solar_os_terminal_t*)1};docs_app_state=&state;solar_os_context_t ctx={0};
 for(unsigned i=0;i<12;i++) pages[i]=(solar_os_manual_page_t){.id="page",.title="Page",.summary="Summary",.section=i<10?"first":"second",.section_title=i<10?"First":"Second"};
 assert(docs_section_count()==2);
 assert(solar_os_app_touch_acquire(&state.touch)==ESP_OK&&leases==1);
 pointer(&ctx,SOLAR_OS_INPUT_POINTER_PRESS,30);pointer(&ctx,SOLAR_OS_INPUT_POINTER_RELEASE,30);
 assert(docs_section_collapsed(0)&&launches==0&&state.render_pending);
 docs_app_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_TICK});assert(renders==1);
 pointer(&ctx,SOLAR_OS_INPUT_POINTER_PRESS,30);pointer(&ctx,SOLAR_OS_INPUT_POINTER_RELEASE,30);
 assert(!docs_section_collapsed(0));
 docs_app_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_TICK});
 unsigned old=renders;
 pointer(&ctx,SOLAR_OS_INPUT_POINTER_PRESS,110);pointer(&ctx,SOLAR_OS_INPUT_POINTER_MOVE,30);pointer(&ctx,SOLAR_OS_INPUT_POINTER_RELEASE,30);
 assert(state.top==4&&launches==0&&renders==old&&state.render_pending);
 docs_app_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_TICK});assert(renders==old+1);
 pointer(&ctx,SOLAR_OS_INPUT_POINTER_PRESS,50);pointer(&ctx,SOLAR_OS_INPUT_POINTER_RELEASE,50);assert(launches==1);
 size_t cursor=state.cursor,top=state.top;uint64_t folds=state.collapsed_sections;
 docs_app_suspend(&ctx);assert(releases==1&&!state.touch.leased);docs_app_resume(&ctx);
 assert(leases==2&&state.cursor==cursor&&state.top==top&&state.collapsed_sections==folds);
 old=renders;
 pointer(&ctx,SOLAR_OS_INPUT_POINTER_PRESS,50);docs_app_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_TOUCH_CANCEL});
 docs_app_suspend(&ctx);docs_app_resume(&ctx);pointer(&ctx,SOLAR_OS_INPUT_POINTER_RELEASE,50);
 assert(launches==1&&leases==2&&releases==1&&renders==old);
 docs_app_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_CHAR,.data.ch=SOLAR_OS_KEY_DOWN});assert(state.cursor==cursor+1);
 docs_app_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_CHAR,.data.ch=SOLAR_OS_KEY_ESCAPE});assert(ctx.exit_requested);
 docs_app_touch_cleanup();assert(releases==2);
 puts("docs_touch_test: fold/tap/Reader, swipe coalescing, return position, CANCEL/Sleep leases, keyboard PASS");
}
