
#include <stdio.h>
#include <stdlib.h>

#include <OMX_Core.h>

#include <bcm_host.h>

OMX_ERRORTYPE err;

void listroles(char *name) {
    int n;
    OMX_U32 numRoles;
    OMX_U8 *roles[32];

    /* get the number of roles by passing in a NULL roles param */
    err = OMX_GetRolesOfComponent(name, &numRoles, NULL);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "Getting roles failed\n", 0);
	exit(1);
    }
    printf("  Num roles is %d\n", numRoles);
    if (numRoles > 32) {
	printf("Too many roles to list\n");
	return;
    }

    /* now get the roles */
    for (n = 0; n < numRoles; n++) {
	roles[n] = malloc(OMX_MAX_STRINGNAME_SIZE);
    }
    err = OMX_GetRolesOfComponent(name, &numRoles, roles);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "Getting roles failed\n", 0);
	exit(1);
    }
    for (n = 0; n < numRoles; n++) {
	printf("    role: %s\n", roles[n]);
	free(roles[n]);
    }

    /* This is in version 1.2
    for (i = 0; OMX_ErrorNoMore != err; i++) {
	err = OMX_RoleOfComponentEnum(role, name, i);
	if (OMX_ErrorNone == err) {
	    printf("   Role of omponent is %s\n", role);
	}
    } 
    */   
}

int main(int argc, char** argv) {

    int i;
    unsigned char name[OMX_MAX_STRINGNAME_SIZE];

    bcm_host_init();

    err = OMX_Init();
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_Init() failed\n", 0);
	exit(1);
    }

    err = OMX_ErrorNone;
    for (i = 0; OMX_ErrorNoMore != err; i++) {
	err = OMX_ComponentNameEnum(name, OMX_MAX_STRINGNAME_SIZE, i);
	if (OMX_ErrorNone == err) {
	    printf("Component is %s\n", name);
	    listroles(name);
	}
    }
    printf("No more components\n");

    exit(0);
}
