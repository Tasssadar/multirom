//Populate by-name symlinks on HTC One M7

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../lib/util.h"

int Populate_ByName_using_emmc(void)
{
	#define EMMC "/proc/emmc"
	#define DEV_BLOCKS "/dev/block"
	#define DEV_BYNAME MR_POPULATE_BY_NAME_PATH

    INFO("nkk71: check by-name support");

    if (mkdir_recursive(DEV_BYNAME, 0755) == 0)
	{
		FILE * fp;
		char strDev[20], strSize[20], strErasesize[20], strName[20];
		char strHardlink[80], strSoftlink[80];

        INFO("nkk71: begin populate");

		fp = fopen (EMMC, "r");
		
		if (fp == NULL)
			ERROR("nkk71: Error opening EMMC %s", EMMC);
		else
		{
			while (!feof(fp)) {
				fscanf(fp, "%[^:]: %s %s \"%[^\"]\" ", strDev, strSize, strErasesize, strName);

                if (strncmp("mmcblk0p", strDev, 8) == 0)
                {
    				sprintf (strHardlink, "%s/%s", DEV_BLOCKS, strDev);
	    			sprintf (strSoftlink, "%s/%s", DEV_BYNAME, strName);

                    INFO("nkk71: link %s to %s - result=%i\n", strSoftlink, strHardlink, symlink(strHardlink, strSoftlink));
                }
			}
			fclose(fp);
		}
        INFO("nkk71: populate done");
	}
    else
        INFO("nkk71: by-name already found, aborting populate");
	return(0);
}
