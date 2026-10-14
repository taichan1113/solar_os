/* Exercise the actual diagnostic, U8g2 image/font, and SSD1677 SPI payload. */
#include <stdarg.h>
#include <stdlib.h>
#define main existing_epd_test_main
#include "epd_ssd1677_test.c"
#undef main
#include "solar_os_fonts.h"

#define SOLAR_OS_DISPLAY_TARGET_OWNER_MAX 32
#define SOLAR_OS_MEMORY_EXTERNAL_REQUIRED 3
#define SOLAR_OS_DISPLAY_PRESENT_GRAPHICS 1
#define SOLAR_OS_SHELL_IO_KIND_PORT 2
typedef struct { int port; } solar_os_shell_io_t;
typedef struct {
    char name[16], driver[20];
    bool ready, black_is_one;
    u8g2_t *u8g2;
} solar_os_display_target_t;
typedef struct {
    uint64_t completed_id;
    esp_err_t error;
    bool busy, pending;
} solar_os_display_submission_status_t;

static epd_ssd1677_t fixture;
static unsigned submitted, fast_updates, otp_updates, claims, releases, gate_reads, redraws;
static unsigned failure_at;
static bool no_memory;
static const char *keys;
static char output[32768];
static size_t output_len;
static uint8_t immutable[48000], previous[48000], initial[48000];
static esp_err_t last_error;

static int solar_os_shell_io_kind(solar_os_shell_io_t *io) { return io->port; }
static esp_err_t solar_os_shell_io_printf(solar_os_shell_io_t *io, const char *format, ...)
{
    (void)io;
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(output + output_len, sizeof(output) - output_len, format, ap);
    va_end(ap);
    assert(n >= 0 && (size_t)n < sizeof(output) - output_len);
    output_len += (size_t)n;
    return ESP_OK;
}
static void solar_os_shell_io_writeln(solar_os_shell_io_t *io, const char *s)
{ (void)solar_os_shell_io_printf(io, "%s\n", s); }
static void solar_os_shell_io_flush(solar_os_shell_io_t *io) { (void)io; }
static esp_err_t solar_os_port_read(int *port, uint8_t *key, size_t len,
                                   unsigned timeout, size_t *n)
{
    assert(*port == 2 && len == 1 && timeout == 0 && *keys);
    *key = (uint8_t)*keys++;
    *n = 1;
    gate_reads++;
    return ESP_OK;
}
static bool solar_os_display_find_target(const char *name, solar_os_display_target_t *t)
{
    *t = (solar_os_display_target_t){.ready=true, .u8g2=&fixture.u8g2};
    strcpy(t->name, "display0"); strcpy(t->driver, "ssd1677");
    return !strcmp(name, "display0");
}
static esp_err_t solar_os_display_claim(const char *name, const char *owner, char *busy, size_t n)
{ assert(!strcmp(name, "display0") && !strcmp(owner, "shell:quality-ab") && n); *busy=0; claims++; return ESP_OK; }
static esp_err_t solar_os_display_release(const char *name, const char *owner)
{ (void)name; (void)owner; releases++; return ESP_OK; }
static bool solar_os_display_target_is_board_primary(const char *name)
{ return !strcmp(name, "display0"); }
static void solar_os_sessions_mark_foreground_dirty(void)
{ assert(releases == 1 && !fixture.faulted && fixture.cleanup_pending); redraws++; }
static void *solar_os_memory_alloc(size_t n, int cls, const char *tag)
{ assert(n==96000 && cls==3 && tag); return no_memory ? NULL : malloc(n); }
static void solar_os_memory_free(void *p) { free(p); }
static esp_err_t solar_os_display_set_controller_mode(const char *name, const char *mode)
{ assert(!strcmp(name, "display0")); return epd_ssd1677_set_controller_mode(&fixture, mode); }
static esp_err_t solar_os_display_wait(u8g2_t *g, unsigned timeout)
{ assert(g==&fixture.u8g2 && timeout==70000); return last_error; }
static esp_err_t solar_os_display_submission_status(u8g2_t *g, solar_os_display_submission_status_t *s)
{ assert(g==&fixture.u8g2); *s=(solar_os_display_submission_status_t){.completed_id=submitted,.error=last_error}; return ESP_OK; }

static uint8_t native_bit(const uint8_t *p, unsigned x, unsigned y)
{ return (uint8_t)((p[(y/8)*800+x] >> (y%8)) & 1); }

static esp_err_t solar_os_display_submit(u8g2_t *g, int mode, uint64_t *id)
{
    assert(g==&fixture.u8g2 && mode==1);
    ++submitted; *id=submitted;
    memcpy(immutable, frame, sizeof(frame));
    memcpy(previous, shadow, sizeof(shadow));
    command_count=0;
    if (submitted==failure_at) busy_on_activation=true;
    last_error=epd_ssd1677_present_snapshot(&fixture, immutable, sizeof(immutable));
    if (last_error!=ESP_OK) {
        assert(!memcmp(shadow, previous, sizeof(shadow)) && !fixture.shadow_valid && fixture.faulted);
        return last_error;
    }
    assert(!memcmp(shadow, immutable, sizeof(shadow)) && fixture.snapshot==NULL);
    if (fixture.timing.activations==1 && fixture.timing.writes==2) {
        ++fast_updates;
        const captured_command_t *now=find_command(0x24,0), *old=find_command(0x26,0);
        assert(now->length==48000 && old->length==48000);
        unsigned changes=0;
        for (unsigned y=0; y<480; ++y) for (unsigned x=0; x<800; ++x) {
            const unsigned pos=y*100+x/8, bit=7-x%8;
            const unsigned a=(old->data[pos]>>bit)&1, b=(now->data[pos]>>bit)&1;
            /* Independent scalar address mapping, not the driver's transpose. */
            const unsigned expected=fixture.timing.partial ? native_bit(immutable,x,y) :
                                                       !native_bit(immutable,x,y);
            assert(b==expected);
            assert(a==(fixture.timing.partial ? native_bit(previous,x,y) : b));
            if (!fixture.timing.partial) continue; /* absolute fast recovery */
            const bool in_marker = ((x>=400 && x<416) || (x>=512 && x<528)) && y>=160 && y<176;
            if (a!=b) { changes++; if (fast_updates>1 || submitted>2) assert(in_marker); }
            if (fast_updates>1 || submitted>2) {
                if (!native_bit(initial,x,y)) assert(a==0 && b==0); /* all fixed black: class 00 */
                else if (!in_marker) assert(a==1 && b==1);
            }
        }
        if (fixture.timing.partial) {
            if (fast_updates>1 || submitted>2) assert(changes==512);
            else assert(changes>512); /* A's initial blackening */
        } else assert(changes==0);
    } else {
        ++otp_updates;
        assert(fixture.timing.activations==2); /* actual OTP full, not RAM-only setup */
    }
    if (submitted==2) memcpy(initial, immutable, sizeof(initial));
    return ESP_OK;
}

#include "../../src/shell/solar_os_shell_quality_ab.inc"

static void setup(void)
{
    fixture=make_display();
    fixture.papermono_otp=true; fixture.band_buffer=band;
    fixture.fastest_tiles=true; fixture.fastest_spi_hz=40000000;
    fixture.refresh_mode=EPD_SSD1677_REFRESH_FASTEST;
    fixture.waveform_trial=EPD_SSD1677_WAVEFORM_DOSE;
    fixture.configured_spi_hz=40000000;
    u8g2_SetupDisplay(&fixture.u8g2, ssd1677_u8x8_display_cb, u8x8_dummy_cb, u8x8_dummy_cb, u8x8_dummy_cb);
    u8g2_SetupBuffer(&fixture.u8g2, frame, 60, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
    submitted=fast_updates=otp_updates=claims=releases=gate_reads=failure_at=redraws=0;
    no_memory=false; output_len=0; output[0]=0; last_error=ESP_OK;
    keys="gggggggggggq"; stuck_busy=false; actual_spi_hz=40000000;
}

static void run(const char *batch, const char *waveform)
{
    solar_os_shell_io_t io={.port=2};
    char *argv[]={"display","test","display0",(char *)batch,(char *)waveform};
    display_cmd_quality_ab(&io, 5, argv);
}

int main(void)
{
    for (unsigned b=0; b<2; ++b) for (unsigned w=0; w<2; ++w) {
        setup();
        keys=b ? "gggggggggggq" : "ggggggggggq";
        run(b ? "ab-b" : "ab-a", w ? "dose" : "baseline");
        assert(fast_updates==10 && otp_updates==(b ? 3U : 2U));
        assert(claims==1 && releases==1 && redraws==1 && !*keys);
        assert(fixture.waveform_trial==EPD_SSD1677_WAVEFORM_DOSE &&
               fixture.refresh_mode==EPD_SSD1677_REFRESH_FASTEST && !fixture.timing_enabled);
        assert(strstr(output,"AB finished fast_count=10") && strstr(output,"fixed_black_00=1"));
        assert(!strstr(output,"outside_changes=1"));
    }
    setup(); keys="ggggggggggq"; run("ab-a","fast6");
    assert(fast_updates==10 && otp_updates==2 && fixture.fastest_recover_interval==0);
    assert(strstr(output,"AB finished fast_count=10"));
    setup(); quality_ab_image(&fixture.u8g2); memcpy(initial,frame,sizeof(frame));
    const uint32_t hash=quality_ab_image_hash(frame,sizeof(frame));
    u8g2_SetClipWindow(&fixture.u8g2, 0, 0, 20, 20);
    quality_ab_image(&fixture.u8g2);
    assert(!memcmp(initial,frame,sizeof(frame)) && hash==quality_ab_image_hash(frame,sizeof(frame)));
    printf("AB reference native image FNV1a=%08x bytes=%zu\n", hash, sizeof(frame));
    setup(); keys="q"; run("ab-a","baseline");
    assert(fast_updates==0 && otp_updates==2 && releases==1 && redraws==1); /* abort at white */
    for (unsigned fail=1; fail<=4; ++fail) {
        setup(); failure_at=fail; run("ab-b","dose");
        assert(submitted==fail && fixture.faulted && releases==1 && redraws==0); /* no cleanup/retry after BUSY fault */
        assert(!strstr(output,"AB finished"));
    }
    setup(); failure_at=13; run("ab-b","dose");
    assert(submitted==13 && fixture.faulted && releases==1 && redraws==0); /* cleanup fault cannot redraw */
    setup(); no_memory=true; run("ab-a","baseline");
    assert(submitted==0 && claims==1 && releases==1);
    setup(); run("ab-a","hold"); assert(claims==0 && submitted==0);
    setup(); fixture.fastest_spi_hz=20000000; run("ab-a","dose"); assert(claims==0);
    setup(); u8g2_SetDisplayRotation(&fixture.u8g2,U8G2_R1); run("ab-b","dose"); assert(claims==0);
    puts("quality AB: real image, planes, fixed classes, bounded steps, abort and BUSY fault PASS");
    return 0;
}
