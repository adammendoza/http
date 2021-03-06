/*
    bit.h -- Build It Configuration Header for vxworks-x86-static

    This header is created by Bit during configuration. To change settings, re-run
    configure or define variables in your Makefile to override these default values.
 */


/* Settings */
#ifndef BIT_BIT
    #define BIT_BIT "0.8.6"
#endif
#ifndef BIT_BUILD_NUMBER
    #define BIT_BUILD_NUMBER "0"
#endif
#ifndef BIT_COMPANY
    #define BIT_COMPANY "Embedthis"
#endif
#ifndef BIT_DEBUG
    #define BIT_DEBUG 1
#endif
#ifndef BIT_DEPTH
    #define BIT_DEPTH 1
#endif
#ifndef BIT_DISCOVER
    #define BIT_DISCOVER "doxygen,dsi,man,man2html,ssl,utest"
#endif
#ifndef BIT_EST_CAMELLIA
    #define BIT_EST_CAMELLIA 0
#endif
#ifndef BIT_EST_DES
    #define BIT_EST_DES 0
#endif
#ifndef BIT_EST_GEN_PRIME
    #define BIT_EST_GEN_PRIME 0
#endif
#ifndef BIT_EST_PADLOCK
    #define BIT_EST_PADLOCK 0
#endif
#ifndef BIT_EST_ROM_TABLES
    #define BIT_EST_ROM_TABLES 0
#endif
#ifndef BIT_EST_SSL_SERVER
    #define BIT_EST_SSL_SERVER 0
#endif
#ifndef BIT_EST_TEST_CERTS
    #define BIT_EST_TEST_CERTS 0
#endif
#ifndef BIT_EST_XTEA
    #define BIT_EST_XTEA 0
#endif
#ifndef BIT_HAS_ATOMIC
    #define BIT_HAS_ATOMIC 0
#endif
#ifndef BIT_HAS_ATOMIC64
    #define BIT_HAS_ATOMIC64 0
#endif
#ifndef BIT_HAS_DOUBLE_BRACES
    #define BIT_HAS_DOUBLE_BRACES 0
#endif
#ifndef BIT_HAS_DYN_LOAD
    #define BIT_HAS_DYN_LOAD 1
#endif
#ifndef BIT_HAS_LIB_EDIT
    #define BIT_HAS_LIB_EDIT 0
#endif
#ifndef BIT_HAS_LIB_RT
    #define BIT_HAS_LIB_RT 0
#endif
#ifndef BIT_HAS_MMU
    #define BIT_HAS_MMU 1
#endif
#ifndef BIT_HAS_MTUNE
    #define BIT_HAS_MTUNE 0
#endif
#ifndef BIT_HAS_PAM
    #define BIT_HAS_PAM 0
#endif
#ifndef BIT_HAS_STACK_PROTECTOR
    #define BIT_HAS_STACK_PROTECTOR 1
#endif
#ifndef BIT_HAS_SYNC
    #define BIT_HAS_SYNC 0
#endif
#ifndef BIT_HAS_SYNC64
    #define BIT_HAS_SYNC64 0
#endif
#ifndef BIT_HAS_SYNC_CAS
    #define BIT_HAS_SYNC_CAS 0
#endif
#ifndef BIT_HAS_UNNAMED_UNIONS
    #define BIT_HAS_UNNAMED_UNIONS 1
#endif
#ifndef BIT_HTTP_PAM
    #define BIT_HTTP_PAM 1
#endif
#ifndef BIT_HTTP_WEB_SOCKETS
    #define BIT_HTTP_WEB_SOCKETS 1
#endif
#ifndef BIT_PREFIXES
    #define BIT_PREFIXES "embedthis-prefixes"
#endif
#ifndef BIT_PRODUCT
    #define BIT_PRODUCT "http"
#endif
#ifndef BIT_REQUIRES
    #define BIT_REQUIRES "vxworks,compiler,lib,link,pcre"
#endif
#ifndef BIT_STATIC
    #define BIT_STATIC 1
#endif
#ifndef BIT_SYNC
    #define BIT_SYNC "bitos,est,mpr,pcre"
#endif
#ifndef BIT_TITLE
    #define BIT_TITLE "Http Library"
#endif
#ifndef BIT_VERSION
    #define BIT_VERSION "1.4.0"
#endif
#ifndef BIT_WARN64TO32
    #define BIT_WARN64TO32 0
#endif
#ifndef BIT_WARN_UNUSED
    #define BIT_WARN_UNUSED 0
#endif
#ifndef BIT_WITHOUT_ALL
    #define BIT_WITHOUT_ALL "doxygen,dsi,man,man2html,pmaker,ssl"
#endif

/* Prefixes */
#ifndef BIT_ROOT_PREFIX
    #define BIT_ROOT_PREFIX "deploy"
#endif
#ifndef BIT_BASE_PREFIX
    #define BIT_BASE_PREFIX "deploy"
#endif
#ifndef BIT_DATA_PREFIX
    #define BIT_DATA_PREFIX "deploy"
#endif
#ifndef BIT_STATE_PREFIX
    #define BIT_STATE_PREFIX "deploy"
#endif
#ifndef BIT_BIN_PREFIX
    #define BIT_BIN_PREFIX "deploy"
#endif
#ifndef BIT_INC_PREFIX
    #define BIT_INC_PREFIX "deploy/inc"
#endif
#ifndef BIT_LIB_PREFIX
    #define BIT_LIB_PREFIX "deploy"
#endif
#ifndef BIT_MAN_PREFIX
    #define BIT_MAN_PREFIX "deploy"
#endif
#ifndef BIT_SBIN_PREFIX
    #define BIT_SBIN_PREFIX "deploy"
#endif
#ifndef BIT_ETC_PREFIX
    #define BIT_ETC_PREFIX "deploy"
#endif
#ifndef BIT_WEB_PREFIX
    #define BIT_WEB_PREFIX "deploy/web"
#endif
#ifndef BIT_LOG_PREFIX
    #define BIT_LOG_PREFIX "deploy"
#endif
#ifndef BIT_SPOOL_PREFIX
    #define BIT_SPOOL_PREFIX "deploy"
#endif
#ifndef BIT_CACHE_PREFIX
    #define BIT_CACHE_PREFIX "deploy"
#endif
#ifndef BIT_APP_PREFIX
    #define BIT_APP_PREFIX "deploy"
#endif
#ifndef BIT_VAPP_PREFIX
    #define BIT_VAPP_PREFIX "deploy"
#endif
#ifndef BIT_SRC_PREFIX
    #define BIT_SRC_PREFIX "/usr/src/http-1.4.0"
#endif

/* Suffixes */
#ifndef BIT_EXE
    #define BIT_EXE ".out"
#endif
#ifndef BIT_SHLIB
    #define BIT_SHLIB ".out"
#endif
#ifndef BIT_SHOBJ
    #define BIT_SHOBJ ".out"
#endif
#ifndef BIT_LIB
    #define BIT_LIB ".a"
#endif
#ifndef BIT_OBJ
    #define BIT_OBJ ".o"
#endif

/* Profile */
#ifndef BIT_CONFIG_CMD
    #define BIT_CONFIG_CMD "bit -d -q -platform vxworks-x86-static -static -configure . -gen make"
#endif
#ifndef BIT_HTTP_PRODUCT
    #define BIT_HTTP_PRODUCT 1
#endif
#ifndef BIT_PROFILE
    #define BIT_PROFILE "static"
#endif
#ifndef BIT_TUNE_SIZE
    #define BIT_TUNE_SIZE 1
#endif

/* Miscellaneous */
#ifndef BIT_MAJOR_VERSION
    #define BIT_MAJOR_VERSION 1
#endif
#ifndef BIT_MINOR_VERSION
    #define BIT_MINOR_VERSION 4
#endif
#ifndef BIT_PATCH_VERSION
    #define BIT_PATCH_VERSION 0
#endif
#ifndef BIT_VNUM
    #define BIT_VNUM 100040000
#endif

/* Packs */
#ifndef BIT_PACK_CC
    #define BIT_PACK_CC 1
#endif
#ifndef BIT_PACK_DOXYGEN
    #define BIT_PACK_DOXYGEN 1
#endif
#ifndef BIT_PACK_DSI
    #define BIT_PACK_DSI 1
#endif
#ifndef BIT_PACK_EST
    #define BIT_PACK_EST 1
#endif
#ifndef BIT_PACK_LIB
    #define BIT_PACK_LIB 1
#endif
#ifndef BIT_PACK_LINK
    #define BIT_PACK_LINK 1
#endif
#ifndef BIT_PACK_MAN
    #define BIT_PACK_MAN 1
#endif
#ifndef BIT_PACK_MAN2HTML
    #define BIT_PACK_MAN2HTML 1
#endif
#ifndef BIT_PACK_MATRIXSSL
    #define BIT_PACK_MATRIXSSL 0
#endif
#ifndef BIT_PACK_NANOSSL
    #define BIT_PACK_NANOSSL 0
#endif
#ifndef BIT_PACK_OPENSSL
    #define BIT_PACK_OPENSSL 0
#endif
#ifndef BIT_PACK_PCRE
    #define BIT_PACK_PCRE 1
#endif
#ifndef BIT_PACK_SSL
    #define BIT_PACK_SSL 1
#endif
#ifndef BIT_PACK_UTEST
    #define BIT_PACK_UTEST 1
#endif
#ifndef BIT_PACK_VXWORKS
    #define BIT_PACK_VXWORKS 1
#endif
#ifndef BIT_PACK_WINSDK
    #define BIT_PACK_WINSDK 0
#endif
