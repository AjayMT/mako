
// libintl.h
//
// Internationalization.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _LIBINTL_H_
#define _LIBINTL_H_

char *gettext(const char *msgid);
char *dgettext(const char *domainname, const char *msgid);

#endif /* _LIBINTL_H_ */
