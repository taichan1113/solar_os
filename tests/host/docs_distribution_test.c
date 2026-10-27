/* Actual Docs/JSON/ZIP install code; host HTTPS transport, OpenSSL and zlib
 * adapters. This is NOT ESP TLS, ROM inflate or mbedTLS hardware acceptance. */
#define SOLAR_OS_PACKAGE_ID_LIST "app.help app.reader"
#define SOLAR_OS_VERSION "4.16.1"
#include <assert.h>
#include <stdarg.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <zlib.h>
#include "miniz.h"
#include "../../src/services/solar_os_docs.c"

static const char *root, *server, *mode;
static bool sd_present=true;
static unsigned requests, ota_reads;
static unsigned last_stage;
static void progress(const solar_os_docs_progress_t *p,void *u){(void)u;last_stage=p->stage;}
size_t strlcpy(char *d,const char *s,size_t n)
{size_t l=strlen(s);if(n){size_t c=l<n-1?l:n-1;memcpy(d,s,c);d[c]=0;}return l;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "host failure";}
esp_err_t solar_os_log_write(solar_os_log_level_t l,const char *t,const char *f,...)
{(void)l;(void)t;(void)f;return ESP_OK;}
void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t c,const char *t){(void)c;(void)t;return malloc(n);}
void *solar_os_memory_calloc(size_t n,size_t s,solar_os_memory_class_t c,const char *t){(void)c;(void)t;return calloc(n,s);}
void solar_os_memory_free(void *p){free(p);}
void vTaskDelay(TickType_t t){(void)t;}
bool solar_os_storage_sd_is_mounted(void){return sd_present;}
const char *solar_os_storage_sd_mount_point(void){return root;}
esp_err_t solar_os_storage_path_mount_point(const char *p,char *o,size_t n)
{assert(!strncmp(p,root,strlen(root)));return strlcpy(o,root,n)<n?ESP_OK:ESP_ERR_INVALID_SIZE;}
esp_err_t solar_os_storage_join_path(const char *a,const char *b,char *o,size_t n)
{int l=snprintf(o,n,"%s/%s",a,b);return l>=0&&(size_t)l<n?ESP_OK:ESP_ERR_INVALID_SIZE;}
size_t solar_os_manual_embedded_count(void){return 0;}
const solar_os_manual_page_t *solar_os_manual_embedded_get(size_t n){(void)n;return NULL;}
void solar_os_ota_get_url(char *o,size_t n)
{ota_reads++;assert(strlcpy(o,"https://ota.invalid/releases/latest",n)<n);}
bool solar_os_crypto_sha256_hex_is_valid(const char *s)
{if(!s||strlen(s)!=64)return false;for(unsigned i=0;i<64;i++)if(!isxdigit((unsigned char)s[i]))return false;return true;}
esp_err_t solar_os_crypto_sha256_once(const void *p,size_t n,uint8_t out[32])
{size_t z=32;return EVP_Q_digest(NULL,"SHA256",NULL,p,n,out,&z)&&z==32?ESP_OK:ESP_FAIL;}
bool solar_os_crypto_sha256_matches_hex(const uint8_t d[32],const char *s)
{char h[65];for(unsigned i=0;i<32;i++)snprintf(h+2*i,3,"%02x",d[i]);return !strcasecmp(h,s);}
esp_err_t solar_os_crypto_base64_decode(const char *s,uint8_t *o,size_t cap,size_t *len)
{
    char b[512];size_t n=0;
    for(;*s;s++)if(!isspace((unsigned char)*s)){if(n>=sizeof(b)-1)return ESP_ERR_INVALID_SIZE;b[n++]=*s;}
    b[n]=0;if(!n||n%4||n/4*3>cap)return ESP_ERR_INVALID_ARG;
    int z=EVP_DecodeBlock(o,(unsigned char *)b,(int)n);if(z<0)return ESP_ERR_INVALID_ARG;
    *len=(size_t)z-(b[n-1]=='=')-(b[n-2]=='=');return ESP_OK;
}
esp_err_t solar_os_crypto_ecdsa_p256_sha256_verify_pem(const char *k,const void *p,size_t n,const uint8_t *s,size_t z)
{
    BIO *b=BIO_new_mem_buf(k,-1);assert(b);EVP_PKEY *key=PEM_read_bio_PUBKEY(b,NULL,NULL,NULL);BIO_free(b);
    if(!key)return ESP_ERR_INVALID_ARG;
    EVP_MD_CTX *ctx=EVP_MD_CTX_new();assert(ctx);
    int ok=EVP_DigestVerifyInit(ctx,NULL,EVP_sha256(),NULL,key)==1 && EVP_DigestVerify(ctx,s,z,p,n)==1;
    EVP_MD_CTX_free(ctx);EVP_PKEY_free(key);return ok?ESP_OK:ESP_ERR_INVALID_CRC;
}

struct solar_os_http_request { solar_os_http_request_options_t options; };
esp_err_t solar_os_http_request_create(const solar_os_http_request_options_t *o,solar_os_http_request_t **out)
{
    const char *prefix="https://docs.invalid/releases/4.16.1-papermono.1/doc/";
    assert(!strncmp(o->url,prefix,strlen(prefix)));
    assert(!o->follow_redirects);requests++;
    *out=malloc(sizeof(**out));assert(*out);(*out)->options=*o;return ESP_OK;
}
esp_err_t solar_os_http_request_destroy(solar_os_http_request_t *r){free(r);return ESP_OK;}
esp_err_t solar_os_http_request_perform(solar_os_http_request_t *r,solar_os_http_response_t *response)
{
    const char *name=strrchr(r->options.url,'/');assert(name);char path[512];
    snprintf(path,sizeof(path),"%s/doc/%s",server,name+1);FILE *f=fopen(path,"rb");if(!f)return ESP_ERR_NOT_FOUND;
    response->status_code=200;response->content_length=-1;
    unsigned char bytes[509];size_t n;esp_err_t err=ESP_OK;
    while(!err&&(n=fread(bytes,1,sizeof(bytes),f))){
        solar_os_http_event_t e={.type=SOLAR_OS_HTTP_EVENT_DATA,.status_code=200,.data=bytes,.data_len=n};
        err=r->options.event_handler(&e,r->options.user_data);
        if(!strcmp(mode,"interrupt")&&strstr(name,".zip")){err=ESP_ERR_TIMEOUT;break;}
    }
    fclose(f);return err;
}

/* Existing real ZIP parser and path/CRC checks, raw-deflate primitive via zlib. */
mz_ulong mz_crc32(mz_ulong c,const unsigned char *p,size_t n){return crc32(c,p,n);}
static z_stream inflate_stream;
static bool inflate_active;
tinfl_status tinfl_decompress(tinfl_decompressor *r,const mz_uint8 *in,size_t *in_len,
    mz_uint8 *base,mz_uint8 *out,size_t *out_len,mz_uint32 flags)
{
    (void)base;(void)flags;
    if(!r->m_state){if(inflate_active)inflateEnd(&inflate_stream);memset(&inflate_stream,0,sizeof(inflate_stream));assert(inflateInit2(&inflate_stream,-15)==Z_OK);inflate_active=true;r->m_state=1;}
    inflate_stream.next_in=(Bytef *)in;inflate_stream.avail_in=(uInt)*in_len;
    inflate_stream.next_out=out;inflate_stream.avail_out=(uInt)*out_len;
    int status=inflate(&inflate_stream,Z_NO_FLUSH);
    *in_len-=inflate_stream.avail_in;*out_len-=inflate_stream.avail_out;
    if(status==Z_STREAM_END){inflateEnd(&inflate_stream);inflate_active=false;return TINFL_STATUS_DONE;}
    if(status!=Z_OK&&status!=Z_BUF_ERROR){inflateEnd(&inflate_stream);inflate_active=false;return TINFL_STATUS_FAILED;}
    return inflate_stream.avail_out?TINFL_STATUS_NEEDS_MORE_INPUT:TINFL_STATUS_HAS_MORE_OUTPUT;
}
size_t __real_fwrite(const void *,size_t,size_t,FILE *);
size_t __wrap_fwrite(const void *p,size_t size,size_t count,FILE *f)
{
    if(!strcmp(mode,"disconnect")||!strcmp(mode,"page-corrupt")){
        char link[64],path[1024];snprintf(link,sizeof(link),"/proc/self/fd/%d",fileno(f));
        ssize_t n=readlink(link,path,sizeof(path)-1);assert(n>=0);path[n]=0;
        if(strstr(path,".stage-")&&strstr(path,"/manual/")){
            if(!strcmp(mode,"disconnect")){sd_present=false;errno=EIO;return 0;}
            if(size&&count){size_t z=size*count;uint8_t *copy=malloc(z);assert(copy);memcpy(copy,p,z);copy[0]^=1;
                size_t result=__real_fwrite(copy,size,count,f);free(copy);return result;}
        }
    }
    return __real_fwrite(p,size,count,f);
}

int main(int argc,char **argv)
{
    assert(argc==4);root=argv[1];server=argv[2];mode=argv[3];
    assert(solar_os_json_init()==ESP_OK);esp_err_t initial=solar_os_docs_init();
    char resolved[256];esp_err_t config=solar_os_docs_release_base(resolved,sizeof(resolved));
    if(!strcmp(mode,"config")){
        printf("{\"config\":%d,\"ota_reads\":%u,\"url\":\"%s\",\"state\":\"%s\"}\n",config,ota_reads,resolved,solar_os_docs_distribution_state());return 0;
    }
    esp_err_t err=!strcmp(mode,"offline")?initial:solar_os_docs_update(progress,NULL);
    solar_os_docs_status_t status;assert(solar_os_docs_get_status(&status)==ESP_OK);
    char *body=NULL;size_t bytes=0;
    esp_err_t read=solar_os_docs_load_page("overview",&body,&bytes);free(body);
    assert(solar_os_docs_get_status(&status)==ESP_OK);
    for(size_t i=0;i<solar_os_docs_manual_count();i++)assert(strcmp(solar_os_docs_manual_get(i)->id,"python.gfx"));
    printf("{\"error\":%d,\"active\":%s,\"revision\":\"%s\",\"read\":%d,\"bytes\":%zu,\"pages\":%zu,\"requests\":%u,\"ota_reads\":%u,\"stage\":%u}\n",
        err,status.available?"true":"false",status.revision,read,bytes,status.page_count,requests,ota_reads,last_stage);
    docs_manual_index_replace(NULL);docs_manual_index_replace(NULL);
    if(inflate_active)inflateEnd(&inflate_stream);
    return 0;
}
