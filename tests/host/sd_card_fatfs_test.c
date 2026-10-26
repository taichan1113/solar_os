/* Real production SD driver + installed IDF FatFS + IDF DiskIO. Only Host/Card
 * transport and VFS registration are replaced. All media is a sparse temp FILE;
 * no physical SD, device port, mount, or user data is accessed. */
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/drivers/sd_card.c"

static FILE *image;
static bool present=true,powered,fail_read,fail_write,formatting_image;
static unsigned epoch=1,host_inits,host_deinits,card_inits,writes;
static FATFS volumes[FF_VOLUMES];
static const size_t first_lba=8192,partition_sectors=524288,total_sectors=121241600;
size_t strlcpy(char *d,const char *s,size_t n){size_t l=strlen(s);if(n){size_t z=l<n-1?l:n-1;memcpy(d,s,z);d[z]=0;}return l;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "fixture error";}
void *ff_memalloc(unsigned n){return malloc(n);}
void ff_memfree(void *p){free(p);}
uint8_t flash_storage_logical_volume(void){return 0;}
void vTaskDelay(TickType_t t){(void)t;}
esp_err_t gpio_config(const gpio_config_t *c){(void)c;return ESP_OK;}
esp_err_t gpio_set_level(gpio_num_t n,uint32_t v){(void)n;(void)v;return ESP_OK;}
esp_err_t fixture_host_init(void){host_inits++;return ESP_OK;}
esp_err_t fixture_host_deinit(void){host_deinits++;return ESP_OK;}
esp_err_t sdmmc_host_init_slot(int n,const sdmmc_slot_config_t *c){assert(n==1 && c->width==4);return ESP_OK;}
esp_err_t sdmmc_card_init(const sdmmc_host_t *h,sdmmc_card_t *c)
{(void)h;assert(present&&powered);card_inits++;*c=(sdmmc_card_t){.epoch=epoch,.ocr=SD_OCR_SDHC_CAP,.csd={.capacity=121241600,.sector_size=512},.ssr={.cur_bus_width=1}};return ESP_OK;}
esp_err_t sdmmc_get_status(sdmmc_card_t *c){return present&&c->epoch==epoch?ESP_OK:ESP_ERR_TIMEOUT;}
esp_err_t sdmmc_read_sectors(sdmmc_card_t *c,void *b,size_t sector,size_t count)
{if(fail_read||sdmmc_get_status(c)!=ESP_OK)return ESP_ERR_TIMEOUT;assert(sector+count<=total_sectors);assert(!fseeko(image,(off_t)sector*512,SEEK_SET));return fread(b,512,count,image)==count?ESP_OK:ESP_FAIL;}
esp_err_t sdmmc_write_sectors(sdmmc_card_t *c,const void *b,size_t sector,size_t count)
{if(fail_write||sdmmc_get_status(c)!=ESP_OK)return ESP_ERR_TIMEOUT;assert((formatting_image&&sector==0&&count==1)||(sector>=first_lba&&sector+count<=first_lba+partition_sectors));writes++;assert(!fseeko(image,(off_t)sector*512,SEEK_SET));return fwrite(b,512,count,image)==count&&fflush(image)==0?ESP_OK:ESP_FAIL;}
esp_err_t sdmmc_can_trim(sdmmc_card_t *c){(void)c;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t sdmmc_can_discard(sdmmc_card_t *c){(void)c;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t sdmmc_erase_sectors(sdmmc_card_t *c,size_t s,size_t n,sdmmc_erase_arg_t a){(void)c;(void)s;(void)n;(void)a;assert(false);return ESP_FAIL;}
void sdmmc_card_print_info(FILE *f,const sdmmc_card_t *c){(void)f;(void)c;}
static esp_err_t power_media(void *ctx,bool on){(void)ctx;powered=on;return ESP_OK;}
static esp_err_t detect_media(void *ctx,bool *p){(void)ctx;*p=present;return ESP_OK;}
esp_err_t esp_vfs_fat_register_cfg(const esp_vfs_fat_conf_t *c,FATFS **fs)
{assert(!strcmp(c->base_path,"/sdcard"));unsigned v=(unsigned)(c->fat_drive[0]-'0');assert(v==1);memset(&volumes[v],0,sizeof(volumes[v]));*fs=&volumes[v];return ESP_OK;}
esp_err_t esp_vfs_fat_unregister_path(const char *p){assert(!strcmp(p,"/sdcard"));return ESP_OK;}
static void put32le(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static void roundtrip(void)
{
    FIL f;UINT n;char actual[32];const char text[]="SD dedicated host test\n";
    assert(VolToPart[1].pd==2 && VolToPart[1].pt==1);
    assert(f_open(&f,"1:/acceptance-test.txt",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&f,text,sizeof(text),&n)==FR_OK && n==sizeof(text));
    assert(f_sync(&f)==FR_OK && f_close(&f)==FR_OK);
    assert(f_open(&f,"1:/acceptance-test.txt",FA_READ)==FR_OK);
    assert(f_read(&f,actual,sizeof(actual),&n)==FR_OK && n==sizeof(text));
    assert(!memcmp(actual,text,n) && f_close(&f)==FR_OK);
}
int main(int argc,char **argv)
{
    image=tmpfile();assert(image && !ftruncate(fileno(image),(off_t)total_sectors*512));
    uint8_t mbr[512]={0};mbr[510]=0x55;mbr[511]=0xaa;mbr[450]=0x0c;
    assert(get_u16le(mbr+510)==0xaa55);
    put32le(mbr+454,(uint32_t)first_lba);put32le(mbr+458,(uint32_t)partition_sectors);
    assert(fwrite(mbr,1,512,image)==512 && !fflush(image));
    ff_diskio_impl_t dummy={0};ff_diskio_register(0,&dummy);ff_diskio_register(1,&dummy);
    assert(sd_card_configure_sdmmc(13,12,11,10,9,8)==ESP_OK);
    assert(sd_card_configure_sdmmc_media(power_media,detect_media,NULL)==ESP_OK);
    assert(sd_card_rescan()==ESP_OK && physical_pdrv==2);
    VolToPart[1]=(PARTITION){2,1};uint8_t work[4096];MKFS_PARM options={.fmt=FM_FAT32,.n_fat=2,.au_size=512};
    formatting_image=true;assert(f_mkfs("1:",&options,work,sizeof(work))==FR_OK);formatting_image=false;
    assert(sd_card_rescan()==ESP_OK);
    assert(sd_card_mount_volume("sd0p1","/sdcard")==ESP_OK);
    assert(mounts[0].fs->fs_type==FS_FAT32);roundtrip();
    assert(sd_card_unmount_volume("sd0p1")==ESP_OK);
    assert(sd_card_mount_volume("sd0p1","/sdcard")==ESP_OK);roundtrip();
    assert(sd_card_unmount_volume("sd0p1")==ESP_OK);
    if(argc>1 && !strcmp(argv[1],"write")) {
        assert(writes>0 && fclose(image)==0);puts("sd_card_fatfs_test: real FatFS FAT32 partition offset + create/write/close/read/remount PASS");return 0;
    }
    assert(sd_card_rescan()==ESP_OK); /* Unmount -> lsblk -> invisible swap */
    unsigned old=card_inits;++epoch;
    assert(sd_card_rescan()==ESP_OK && card_inits==old+1 && block_count==2);
    assert(sd_card_mount_volume("sd0p1","/sdcard")==ESP_OK);roundtrip();
    /* Mounted owners must not be deinitialized/rebound by a rescan. */
    unsigned initialized=host_inits,deinitialized=host_deinits;
    present=false;assert(sd_card_rescan()==ESP_ERR_NOT_FOUND);
    assert(sd_card_has_active_mounts() && host_inits==initialized && host_deinits==deinitialized);
    present=true;++epoch;assert(sd_card_rescan()==ESP_ERR_TIMEOUT);
    assert(sd_card_has_active_mounts() && host_inits==initialized && host_deinits==deinitialized);
    --epoch;roundtrip();
    assert(sd_card_set_suspended(true)==ESP_OK);
    old=card_inits;assert(sd_card_rescan()==ESP_ERR_INVALID_STATE && card_inits==old);
    assert(sd_card_set_suspended(false)==ESP_OK);roundtrip();
    assert(sd_card_unmount_volume("sd0p1")==ESP_OK);
    assert(sd_card_rescan()==ESP_OK);present=false;
    assert(sd_card_rescan()==ESP_ERR_NOT_FOUND && !card_ready && block_count==0);
    present=true;++epoch;assert(sd_card_rescan()==ESP_OK && block_count==2);
    assert(sd_card_mount_volume("sd0p1","/sdcard")==ESP_OK);roundtrip();
    /* A failed scan must be an error, not a successful empty partition list. */
    assert(sd_card_unmount_volume("sd0p1")==ESP_OK);fail_read=true;
    assert(sd_card_rescan()!=ESP_OK);fail_read=false;
    assert(sd_card_rescan()==ESP_OK);
    assert(host_inits>1 && host_deinits>0 && writes>0 && fclose(image)==0);
    puts("sd_card_fatfs_test: reinsert/absence/scan failure/mapping + actual FatFS PASS");
}
