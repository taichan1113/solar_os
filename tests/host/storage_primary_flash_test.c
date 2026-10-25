#define main legacy_storage_main
#include "storage_mounts_test.c"
#undef main
int main(void)
{
    char path[128];
    for (int mounted=0; mounted<=1; ++mounted) {
        sd_test_mounted=mounted;
        assert(!strcmp(solar_os_storage_mount_point(), "/"));
        assert(!strcmp(solar_os_storage_flash_mount_point(), "/"));
        assert(!strcmp(solar_os_storage_sd_mount_point(), "/sdcard"));
        assert(solar_os_storage_default_path(".shell/startup",path,sizeof(path))==ESP_OK);
        assert(!strcmp(path,"/.shell/startup"));
        assert(solar_os_storage_default_path("Sketches/sketch001.png",path,sizeof(path))==ESP_OK);
        assert(!strcmp(path,"/Sketches/sketch001.png"));
    }
    puts("PaperMono primary Flash paths stay stable with SD absent/present");
    return 0;
}
