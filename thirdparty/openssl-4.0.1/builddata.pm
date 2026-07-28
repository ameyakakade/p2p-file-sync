package OpenSSL::safe::installdata;

use strict;
use warnings;
use Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(
    @PREFIX
    @libdir
    @BINDIR @BINDIR_REL_PREFIX
    @LIBDIR @LIBDIR_REL_PREFIX
    @INCLUDEDIR @INCLUDEDIR_REL_PREFIX
    @APPLINKDIR @APPLINKDIR_REL_PREFIX
    @MODULESDIR @MODULESDIR_REL_LIBDIR
    @PKGCONFIGDIR @PKGCONFIGDIR_REL_LIBDIR
    @CMAKECONFIGDIR @CMAKECONFIGDIR_REL_LIBDIR
    $COMMENT $VERSION @LDLIBS
);

our $COMMENT                    = 'This file should be used when building against this OpenSSL build, and should never be installed';
our @PREFIX                     = ( '/Users/ameya/Programming/p2p-file-transfer/thirdparty/openssl-4.0.1' );
our @libdir                     = ( '/Users/ameya/Programming/p2p-file-transfer/thirdparty/openssl-4.0.1' );
our @BINDIR                     = ( '/Users/ameya/Programming/p2p-file-transfer/thirdparty/openssl-4.0.1/apps' );
our @BINDIR_REL_PREFIX          = ( 'apps' );
our @LIBDIR                     = ( '/Users/ameya/Programming/p2p-file-transfer/thirdparty/openssl-4.0.1' );
our @LIBDIR_REL_PREFIX          = ( '' );
our @INCLUDEDIR                 = ( '/Users/ameya/Programming/p2p-file-transfer/thirdparty/openssl-4.0.1/include', '/Users/ameya/Programming/p2p-file-transfer/thirdparty/openssl-4.0.1/include' );
our @INCLUDEDIR_REL_PREFIX      = ( 'include', './include' );
our @APPLINKDIR                 = ( '/Users/ameya/Programming/p2p-file-transfer/thirdparty/openssl-4.0.1/ms' );
our @APPLINKDIR_REL_PREFIX      = ( 'ms' );
our @MODULESDIR                 = ( '/Users/ameya/Programming/p2p-file-transfer/thirdparty/openssl-4.0.1/providers' );
our @MODULESDIR_REL_LIBDIR      = ( 'providers' );
our @PKGCONFIGDIR               = ( '/Users/ameya/Programming/p2p-file-transfer/thirdparty/openssl-4.0.1' );
our @PKGCONFIGDIR_REL_LIBDIR    = ( '' );
our @CMAKECONFIGDIR             = ( '/Users/ameya/Programming/p2p-file-transfer/thirdparty/openssl-4.0.1' );
our @CMAKECONFIGDIR_REL_LIBDIR  = ( '' );
our $VERSION                    = '4.0.1';
our @LDLIBS                     =
    # Unix and Windows use space separation, VMS uses comma separation
    $^O eq 'VMS'
    ? split(/ *, */, ' ')
    : split(/ +/, ' ');

1;
