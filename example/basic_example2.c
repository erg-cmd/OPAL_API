/****************************************************************************

		Example using RT-LAB API.
		Gives the main step in order to load and execute a model.

	// Compilation using g++ (MinGW) on Windows:
	g++ basic_example2.c -o basic_example2.exe -I"C:\OPAL-RT\RT-LAB\2021.3.4\common\include" -L"C:\OPAL-RT\RT-LAB\2021.3.4\common\lib" -lOpalApi

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Header for getcwd() is platform-dependent
#if defined(WIN32)
	#include <direct.h>
	#define GetCurrentDir _getcwd
#elif defined(__linux__)
	#include <unistd.h>
	#define _MAX_PATH 256
	#define GetCurrentDir getcwd
#endif
#include "OpalApi.h"

void PrintError(int rc, char *funcName);


// const char* PROJECT_PATH = "C:\\Archivos_INI_OPAL\\ergs_api_test0\\ergs_api_test0.llp";
const char* PROJECT_PATH = "C:/Archivos_INI_OPAL/ergs_api_test0/ergs_api_test0.llp";

int main(void)
{
	char currentPath[_MAX_PATH];
	char projectPath[_MAX_PATH];
	char projectName[_MAX_PATH] = "";
	char modelName[_MAX_PATH] = "ergs_api_test0.slx";
	char* lastSeg = NULL;
	
	OP_API_INSTANCE_ID instId = 0;
	double				timeFactor;
	int					rc;
	unsigned short		realTimeMode;

	
/* 	GetCurrentDir(currentPath, sizeof(currentPath));
	printf("- Current path: '%s'.\n", currentPath);

	//Find adress of last segment to remove it from current directory and get the llp file path (projectPath)
	lastSeg = strrchr(currentPath, '\\');
	if(lastSeg == NULL)
	{
		printf("- Invalid current directory path: '%s'.\n", currentPath);
		return 1;
	}

	strncpy(projectPath, currentPath, lastSeg - currentPath + 1);
	printf("- projectPath: '%s'.\n", projectPath);
	
	//Find the project name to get the .llp file name
	strncpy(projectName, projectPath, strlen(projectPath) - 1);
	lastSeg = strrchr(projectName, '\\');
	strcpy(projectName, lastSeg +1);
	
	strcat(projectPath, projectName);
	strcat(projectPath, ".llp");

	 */

	strncpy(projectPath, PROJECT_PATH, sizeof(projectPath) - 1);
	projectPath[sizeof(projectPath) - 1] = '\0';

	printf("- Using project path: '%s'.\n", projectPath);
	printf("- Size of projectPath: %d\n", sizeof(projectPath));
	// printf("The type of projectPath is: %s\n", typeof(projectPath));
 
/* 	if (NULL == projectPath)
	{
		printf("- Invalid llp path: '%s'.\n", projectPath);
		return 1;
	} */

	if (projectPath[0] == '\0') {
    	printf("- Invalid llp path: '%s'.\n", projectPath);
    	return 1;
	}
	 
	rc = OpalOpenProject(projectPath, 0, &instId);
	if (EOK != rc)
	{
		PrintError(rc, "OpalOpenProject");
		return(rc);
	}

	printf("The connection with '%s' is completed.\n", projectPath);
	
	// Load the model
	timeFactor = 1.0;
	realTimeMode = 2;
	rc = OpalLoad(realTimeMode, &instId, timeFactor);
	if(EOK != rc)
	{
		PrintError(rc, "OpalLoad");
		return(rc);
	}
	printf("- Model %s is loaded.\n", modelName);
	
	// Execute the model
	rc = OpalExecute(timeFactor);
	if(EOK != rc)
	{
		PrintError(rc, "OpalExecute");
		return(rc);
	}
	printf("- Model %s is executed.\n", modelName);

	// Reset the model
	rc = OpalReset();
	if(EOK != rc)
	{
		PrintError(rc, "OpalReset");
		return(rc);
	}
	printf("- Model %s is reset.\n", modelName);
	
	// Disconnect from the model
	OpalDisconnect();
	printf("- Model %s is disconnected.\n", modelName);

	system("pause");

	return 0;
}


void PrintError(int rc, char *funcName)
{
	char			buf[512];
	unsigned short	len;

	OpalGetLastErrMsg(buf, sizeof(buf), &len);
	printf("Error %s (code %d) from %s\n", buf, rc, funcName);
	system("pause");
}
