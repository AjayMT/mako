
// libintl.c
//
// Internationalization.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <libintl.h>

char *gettext(const char *msgid)
{ return (char *)msgid; }
char *dgettext(const char *domainname, const char *msgid)
{ return (char *)msgid; }
