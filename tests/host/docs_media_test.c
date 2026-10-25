/* Real Docs index/read paths; crypto transport mocked, not a cryptographic test. */
#define SOLAR_OS_PACKAGE_ID_LIST "app.help app.reader"
#define SOLAR_OS_VERSION "4.16.1"
#include <assert.h>
#include "../../src/services/solar_os_docs.c"
static bool sd_present, valid_signature=true, valid_hash=true;
static unsigned signatures, hashes;
static char root[]="/tmp/solaros-docs-XXXXXX";
size_t strlcpy(char *d,const char *s,size_t n){size_t l=strlen(s);if(n){size_t c=l<n-1?l:n-1;memcpy(d,s,c);d[c]=0;}return l;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "fixture";}
esp_err_t solar_os_log_write(solar_os_log_level_t l,const char *t,const char *f,...){(void)l;(void)t;va_list a;va_start(a,f);vfprintf(stderr,f,a);fputc(10,stderr);va_end(a);return ESP_OK;}
void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t c,const char *t){(void)c;(void)t;return malloc(n);}
void *solar_os_memory_calloc(size_t n,size_t s,solar_os_memory_class_t c,const char *t){(void)c;(void)t;return calloc(n,s);}
void solar_os_memory_free(void *p){free(p);}
bool solar_os_storage_sd_is_mounted(void){return sd_present;}
const char *solar_os_storage_sd_mount_point(void){return root;}
esp_err_t solar_os_storage_join_path(const char *a,const char *b,char *o,size_t n){int l=snprintf(o,n,"%s/%s",a,b);return l>=0&&(size_t)l<n?ESP_OK:ESP_ERR_INVALID_SIZE;}
size_t solar_os_manual_embedded_count(void){return 0;}
const solar_os_manual_page_t *solar_os_manual_embedded_get(size_t i){(void)i;return NULL;}
bool solar_os_crypto_sha256_hex_is_valid(const char *s){return strlen(s)==64;}
esp_err_t solar_os_crypto_base64_decode(const char *s,uint8_t *o,size_t n,size_t *l){(void)s;assert(n);o[0]=1;*l=1;return ESP_OK;}
esp_err_t solar_os_crypto_ecdsa_p256_sha256_verify_pem(const char *k,const void *d,size_t n,const uint8_t *s,size_t z){assert(k&&d&&n&&s&&z);signatures++;return valid_signature?ESP_OK:ESP_ERR_INVALID_CRC;}
esp_err_t solar_os_crypto_sha256_once(const void *d,size_t n,uint8_t o[32]){(void)d;(void)n;memset(o,0,32);hashes++;return ESP_OK;}
bool solar_os_crypto_sha256_matches_hex(const uint8_t d[32],const char *s){(void)d;(void)s;return valid_hash;}
static void directory(const char *name){char p[512];snprintf(p,sizeof(p),"%s/%s",root,name);assert(mkdir(p,0700)==0);}
static void file(const char *name,const char *data){char p[512];snprintf(p,sizeof(p),"%s/%s",root,name);FILE *f=fopen(p,"wb");assert(f);assert(fwrite(data,1,strlen(data),f)==strlen(data));assert(!fclose(f));}
static void remove_path(const char *name,bool dir){char p[512];snprintf(p,sizeof(p),"%s/%s",root,name);assert((dir?rmdir(p):unlink(p))==0);}
int main(void){
 assert(mkdtemp(root));
 directory(".solar");directory(".solar/docs");directory(".solar/docs/0123456789abcdef");directory(".solar/docs/0123456789abcdef/manual");
 file(".solar/docs/active","0123456789abcdef\n");
 const char *catalog="{\"schema\":\"solaros.manual_catalog\",\"schema_version\":2,\"firmware_version\":\"4.0.0\",\"revision\":\"0123456789abcdef\",\"archive\":{\"path\":\"manual.zip\",\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"size\":4},\"pages\":[{\"id\":\"overview\",\"path\":\"manual/overview.md\",\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"size\":4,\"title\":\"SD overview\",\"section\":\"intro\",\"section_title\":\"Intro\",\"summary\":\"offline\",\"aliases\":[],\"keywords\":\"sd\",\"reference\":\"intro\",\"packages_any\":[]}]}";
 file(".solar/docs/0123456789abcdef/catalog.json",catalog);file(".solar/docs/0123456789abcdef/catalog.sig","fixture");file(".solar/docs/0123456789abcdef/manual/overview.md","page");
 assert(solar_os_docs_init()==ESP_OK&&!solar_os_docs_manual_index_available());
 assert(solar_os_docs_storage_begin()==ESP_OK);sd_present=true;solar_os_docs_storage_end();
 assert(solar_os_docs_manual_index_available()&&solar_os_docs_manual_count()==1&&signatures==1);
 solar_os_docs_status_t status;solar_os_docs_get_status(&status);assert(!strcmp(status.manual_version,"4.0.0"));
 char *body=NULL;size_t n=0;assert(solar_os_docs_load_page("overview",&body,&n)==ESP_OK&&n==4&&signatures==2&&hashes==1);free(body);
 docs_status.updating=true;assert(solar_os_docs_storage_begin()==ESP_ERR_INVALID_STATE);docs_status.updating=false;
 docs_page_readers=1;assert(solar_os_docs_storage_begin()==ESP_ERR_INVALID_STATE);docs_page_readers=0;
 assert(solar_os_docs_storage_begin()==ESP_OK);assert(!solar_os_docs_manual_index_available());assert(solar_os_docs_storage_begin()==ESP_ERR_INVALID_STATE);sd_present=false;solar_os_docs_storage_end();assert(!solar_os_docs_manual_index_available());
 assert(solar_os_docs_storage_begin()==ESP_OK);sd_present=true;solar_os_docs_storage_end();assert(solar_os_docs_manual_index_available());
 valid_hash=false;assert(solar_os_docs_load_page("overview",&body,&n)==ESP_ERR_INVALID_CRC&&!body&&!solar_os_docs_manual_index_available());
 valid_signature=false;assert(solar_os_docs_storage_begin()==ESP_OK);solar_os_docs_storage_end();assert(!solar_os_docs_manual_index_available());
 remove_path(".solar/docs/0123456789abcdef/manual/overview.md",false);remove_path(".solar/docs/0123456789abcdef/manual",true);remove_path(".solar/docs/0123456789abcdef/catalog.json",false);remove_path(".solar/docs/0123456789abcdef/catalog.sig",false);remove_path(".solar/docs/0123456789abcdef",true);remove_path(".solar/docs/active",false);remove_path(".solar/docs",true);remove_path(".solar",true);assert(!rmdir(root));
 docs_manual_index_replace(NULL);docs_manual_index_replace(NULL);
 puts("docs_media_test: SD absent/present/reinsert index, offline read, old-version metadata, verification rejection, update/read exclusion PASS");
}
