/*
    alias.c -- Alias service for aliasing URLs to file storage.

    This module supports the alias directives and mapping URLs to physical locations. 

    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

/********************************* Includes ***********************************/

#include    "http.h"

/********************************* Forwards ***********************************/

static void manageAlias(HttpAlias *alias, int flags);

/*********************************** Code *************************************/
/*
    Create an alias for a URI prefix. During processing, a request URI prefix is substituted to the target which
    may be either a physical path or a URI if a non-zero redirect code is supplied.
 */
HttpAlias *httpCreateAlias(cchar *prefix, cchar *target, int code)
{
    HttpAlias   *ap;
    cchar       *seps;
    ssize       len;

    mprAssert(prefix);
    mprAssert(target && *target);

    if ((ap = mprAllocObj(HttpAlias, manageAlias)) == NULL) {
        return 0;
    }
    ap->prefix = sclone(prefix);
    ap->prefixLen = slen(prefix);

#if UNUSED
    /*  
        Always strip trailing "/" from the prefix
     */
    if (ap->prefixLen > 0 && ap->prefix[ap->prefixLen - 1] == '/') {
        ap->prefix[--ap->prefixLen] = '\0';
    }
#endif
    if (code) {
        ap->redirectCode = code;
        ap->uri = sclone(target);
        ap->filename = sclone("");
    } else {
        /*  
            Trim trailing "/" from filename always
         */
        seps = mprGetPathSeparators(target);
        ap->filename = sclone(target);
        len = slen(ap->filename) - 1;
        if (len >= 0 && ap->filename[len] == seps[0]) {
            ap->filename[len] = '\0';
        }
    }
    return ap;
}


static void manageAlias(HttpAlias *alias, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(alias->prefix);
        mprMark(alias->filename);
        mprMark(alias->uri);
    }
}


/*
    @copy   default
    
    Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
    Copyright (c) Michael O'Brien, 1993-2011. All Rights Reserved.
    
    This software is distributed under commercial and open source licenses.
    You may use the GPL open source license described below or you may acquire 
    a commercial license from Embedthis Software. You agree to be fully bound 
    by the terms of either license. Consult the LICENSE.TXT distributed with 
    this software for full details.
    
    This software is open source; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License as published by the 
    Free Software Foundation; either version 2 of the License, or (at your 
    option) any later version. See the GNU General Public License for more 
    details at: http://www.embedthis.com/downloads/gplLicense.html
    
    This program is distributed WITHOUT ANY WARRANTY; without even the 
    implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
    
    This GPL license does NOT permit incorporating this software into 
    proprietary programs. If you are unable to comply with the GPL, you must
    acquire a commercial license to use this software. Commercial licenses 
    for this software and support services are available from Embedthis 
    Software at http://www.embedthis.com 
    
    Local variables:
    tab-width: 4
    c-basic-offset: 4
    End:
    vim: sw=4 ts=4 expandtab

    @end
 */
