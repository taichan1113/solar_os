#define main legacy_storage_main
#include "storage_mounts_test.c"
#undef main
static esp_err_t sd_result, flash_result, docs_result;
static unsigned media_ops, reloads, flash_mounts;
esp_err_t solar_os_docs_storage_begin(void){return docs_result;}
void solar_os_docs_storage_end(void){reloads++;}
esp_err_t solar_os_board_storage_mount(void){media_ops++;sd_test_mounted=sd_result==ESP_OK;return sd_result;}
esp_err_t solar_os_board_storage_mount_volume(const char *n,const char *p){assert(!strcmp(n,"sd0p1")&&!strcmp(p,"/sdcard"));return solar_os_board_storage_mount();}
esp_err_t solar_os_board_storage_unmount(void){media_ops++;sd_test_mounted=false;return sd_result;}
esp_err_t solar_os_board_storage_unmount_volume(const char *t){assert(!strcmp(t,"/sdcard"));return solar_os_board_storage_unmount();}
esp_err_t solar_os_board_storage_rescan(void){media_ops++;return sd_result;}
esp_err_t flash_storage_mount(const char *p){assert(!strcmp(p,"/"));flash_mounts++;return flash_result;}
esp_err_t flash_storage_unmount(void){assert(false);return ESP_FAIL;}
int main(void){
 sd_result=ESP_ERR_NOT_FOUND;assert(solar_os_storage_init()==ESP_OK&&flash_mounts==1&&!sd_test_mounted&&reloads==0);
 sd_result=ESP_ERR_TIMEOUT;assert(solar_os_storage_init()==ESP_OK&&flash_mounts==2&&reloads==0);
 sd_result=ESP_OK;flash_result=ESP_FAIL;assert(solar_os_storage_init()==ESP_FAIL&&reloads==0);flash_result=ESP_OK;
 unsigned before=media_ops;
 docs_result=ESP_ERR_INVALID_STATE;assert(solar_os_storage_mount()==ESP_ERR_INVALID_STATE&&media_ops==before&&reloads==0);
 docs_result=ESP_OK;assert(solar_os_storage_mount_volume("sd0p1","/sdcard")==ESP_OK&&reloads==1&&sd_test_mounted);
 assert(solar_os_storage_unmount_volume("/sdcard")==ESP_OK&&reloads==2&&!sd_test_mounted);
 assert(solar_os_storage_mount()==ESP_OK&&reloads==3&&sd_test_mounted);
 assert(solar_os_storage_rescan()==ESP_OK&&reloads==4);
 assert(solar_os_storage_unmount()==ESP_OK&&reloads==5);
 sd_result=ESP_ERR_NOT_FOUND;assert(solar_os_storage_mount()==ESP_ERR_NOT_FOUND&&reloads==6);
 assert(solar_os_storage_mount_volume("flash",NULL)==ESP_OK&&reloads==6);
 assert(!strcmp(solar_os_storage_mount_point(),"/")&&!strcmp(solar_os_storage_sd_mount_point(),"/sdcard"));
 puts("storage_media_test: empty/error SD boot keeps Flash, Flash failure not hidden, docs hooks on mount/unmount/rescan/error and update exclusion PASS");
}
