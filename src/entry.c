#include "common/common.h"
#include "utils/logger.h"
#include "main.h"
#include <padscore/kpad.h>


int main(int argc, char** argv)
{
	//! *********************************************************************
	//! *                        Initialize Kpad                            *
	//! *********************************************************************
	log_init();

	KPADInit();
	WPADEnableURCC(1);
	
	log_printf("\nStarting WUX Installer %s\n", WUX_INSTALLER_VERSION);

	//! *******************************************************************
	//! *                        Call our Main                            *
	//! *******************************************************************
	Menu_Main();
	

	//! *******************************************************************
	//! *                            Exit                                 *
	//! *******************************************************************
	log_printf("WUX Installer exit...\n");

	//! KPADShutdown while logging is still alive; ProcUI teardown already
	//! happened inside Menu_Main's Application lifetime.
	KPADShutdown();

	log_deinit();

	return 0;
}
