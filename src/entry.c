#include "common/common.h"
#include "utils/logger.h"
#include "main.h"
#include <padscore/kpad.h>


int main(int argc, char** argv)
{
	//! *********************************************************************
	//! *                        Initialize Kpad                            *
	//! *********************************************************************
	KPADInit();
	WPADEnableURCC(1);
	
	log_init();
	log_printf("\nStarting WUX Installer %s\n", WUX_INSTALLER_VERSION);

	//! *******************************************************************
	//! *                        Call our Main                            *
	//! *******************************************************************
	Menu_Main();
	

	//! *******************************************************************
	//! *                            Exit                                 *
	//! *******************************************************************
	log_printf("WUX Installer exit...\n");
	log_deinit();

	return 0;
}
