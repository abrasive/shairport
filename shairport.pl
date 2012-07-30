#!/usr/bin/env perl

#   ShairPort - Airtunes compatible server
#   Copyright (c) 2011 James Laird
#   All rights reserved.
#        Permission is hereby granted, free of charge, to any person
#        obtaining a copy of this software and associated documentation
#        files (the "Software"), to deal in the Software without
#        restriction, including without limitation the rights to use,
#        copy, modify, merge, publish, distribute, sublicense, and/or
#        sell copies of the Software, and to permit persons to whom the
#        Software is furnished to do so, subject to the following conditions:
#
#        The above copyright notice and this permission notice shall be
#        included in all copies or substantial portions of the Software.
#
#        THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
#        EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
#        OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#        NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
#        HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
#        WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#        FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
#        OTHER DEALINGS IN THE SOFTWARE.

use strict;
use warnings;

use 5.10.0;
# For given() { when() { } ... }
use feature ":5.10";

use Getopt::Long;
use FindBin;
use File::Basename;

use IO::Select;
use IO::Socket;
use MIME::Base64;
use HTTP::Request;
use HTTP::Response;
use URI::Escape;
use IPC::Open2;
use Crypt::OpenSSL::RSA;
use Digest::MD5 qw(md5 md5_hex);
use POSIX qw(:sys_wait_h setsid);
eval "use IO::Socket::INET6;";
use Net::SDP;

my $shairportversion = "0.05";

my $apname = "ShairPort $$ on " . `hostname`;
my $port = 5002;
# password - required to connect
# for no password, set:
my $password = '';
# output to a pipe?
my $pipepath;
# detach
my $daemon;
# ao options
my $libao_driver;
my $libao_devicename;
my $libao_deviceid;
# suppose hairtunes is under same directory
my $hairtunes_cli = $FindBin::Bin . '/hairtunes';
# Integrate with Squeezebox Server
my $squeeze;
# SBS CLI port
my $cliport;
# SB target
my $mac;
# SB volume
my $volume;
# custom play and stop program
my $play_prog;
my $stop_prog;
# output debugging information
my $verbose;
# where to write PID
my $writepid;
# show help
my $help;

unless (-x $hairtunes_cli) {
    say "Can't find the 'hairtunes' decoder binary, you need to build this before using ShairPort.";
    say "Read the INSTALL instructions!";
    exit(1);
}

GetOptions("a|apname=s" => \$apname,
          "p|password=s"  => \$password,
          "o|server_port=s" => \$port,
          "i|pipe=s"  => \$pipepath,
          "d" => \$daemon,
          "ao_driver=s" => \$libao_driver,
          "ao_devicename=s" => \$libao_devicename,
          "ao_deviceid=s" => \$libao_deviceid,
          "v|verbose" => \$verbose,
          "w|writepid=s" => \$writepid,
          "s|squeezebox" => \$squeeze,
          "c|cliport=s" => \$cliport,
          "m|mac=s" => \$mac,
          "play_prog=s" => \$play_prog,
          "stop_prog=s" => \$stop_prog,
          "l|volume=s" => \$volume,
          "h|help" => \$help);

sub usage {
    print "ShairPort version $shairportversion - Airport Express emulator\n".
          "Usage:\n".
          basename($0) . " [OPTION...]\n".
          "\n".
          "Options:\n".
          "  -a, --apname=AirPort            Sets AirPort name\n".
          "  -p, --password=secret           Sets password\n",
          "  -o, --server_port=5002          Sets Port for Avahi/dns-sd/howl\n",
          "  -i, --pipe=pipepath             Sets the path to a named pipe for output\n",
          "      --ao_driver=driver          Sets the ao driver (optional)\n",
          "      --ao_devicename=devicename  Sets the ao device name (optional)\n",
          "      --ao_deviceid=id            Sets the ao device id (optional)\n",
          "  -s  --squeezebox                Enables local Squeezebox Server integration\n",
          "  -c  --cliport=port              Sets the SBS CLI port\n",
          "  -m  --mac=address               Sets the SB target device\n",
          "  -l  --volume=level              Sets the SB volume level (in %)\n",
          "      --play_prog=cmdline         Program to start on 1st connection\n",
          "      --stop_prog=cmdline         Program to start on last disconnection\n",
          "  -d                              Daemon mode\n",
          "  -w  --writepid=path             Write PID to this location\n",
          "  -v  --verbose                   Print debugging messages\n",
          "  -h, --help                      This help\n",
          "\n";
    exit;
}

if (defined($help) && $help == 1) { usage(); }
# ensure that $verbose is set, one way or another...
if (defined($verbose) && $verbose) {
    $verbose = 1;
} else {
    $verbose = 0;
}

$cliport = ( (defined( $cliport ) && $cliport && $cliport =~ m/^[0-9]+/ && $cliport > 1023 && $cliport < 65536 ) ? $cliport : 9090 );

$volume = ( (defined( $volume ) && $volume && $volume =~ m/^[0-9]+/ && $volume >= 0 && $volume <= 101 ) ? $volume : undef );

if (defined($squeeze) && $squeeze) {
    my $players;
    my @details;

    my $response;
    my $socket = IO::Socket::INET -> new (
          PeerAddr => "127.0.0.1"
        , PeerPort => $cliport
        , Proto    => 'tcp'
        , Timeout  => 1
    );
    if( !( $socket ) ) {
        print "ERROR: Could not create socket to interface with SqueezeBox Server on port $cliport: $!\n";
        print "WARN:  Disabling Squeezebox Server integration\n";
        undef $squeeze;
    }
    if( $squeeze ) {
        print $socket "player count ?\n";
        $response = <$socket>;
        if( !( defined( $response ) ) ) {
            print "ERROR: Could not communicate with SqueezeBox Server on port $cliport\n";
            print "WARN:  Disabling Squeezebox Server integration\n";
            undef $squeeze;
        } else {
            ( $players ) = ( $response =~ m/^player count ([0-9]+)$/ );
            print $socket "players 0 $players\n";
            $response = <$socket>;
            @details = split( /playerindex%3A/ , $response );
            close( $socket );
            shift( @details );
            for( my $n = 0; $n <= scalar( @details ); $n++ ) {
                if( defined( $details[ $n ] ) ) {
                    my $address = $details[ $n ];
                    $address =~ s/^.*playerid%3A([[:xdigit:]%]+)\s.*$/$1/;
                    $address =~ s/%3A/:/g;
                    chomp $address;
                    $details[ $n ] = $address;
                }
            }
            print "Discovered players: $players\n" if $verbose;
            print "Player MAC addresses:\n" if $verbose;
            foreach my $address (@details) {
                print "\t$address\n" if( ( defined( $address ) && $address ) && $verbose );
            }
        
            if( defined( $mac ) && $mac ) {
                chomp $mac;
                if( !( grep { lc( $_ ) eq lc( $mac ) } @details ) ) {
                    print "ERROR: Invalid or non-present MAC specified.\n\n";
                    print "Please select a target MAC address from:\n";
                    foreach my $address (@details) {
                        print "\t$address\n" if( defined( $address ) && $address );
                    }
                    exit(1);
                }
            } else {
                if( 1 == $players ) {
                    $mac = $details[ 0 ];
                    print "WARN: No Squeezebox player specified, using $mac.\n";
                } else {
                    print "ERROR: No Squeezebox player specified, please select a target MAC address with the '--mac' option with a value from:\n";
                    foreach my $address (@details) {
                        print "\t$address\n" if( defined( $address ) && $address );
                    }
                    exit(1);
                }
            }
            $mac = uri_escape( $mac ) if( defined( $mac ) && $mac );
        }
    }
};
chomp $apname;

my @hw_addr = +(map(ord, split(//, md5($apname))))[0..5];

sub POPE {
    print "Broken pipe\n" if $verbose;
    $SIG{PIPE} = \&POPE;
}
$SIG{PIPE} = \&POPE;


our $avahi_publish;
our $squeezebox_setup;

sub REAP {
    my $pid = waitpid( -1, WNOHANG );
    given( $pid ) {
        when( $avahi_publish ) {
            die( "avahi daemon terminated or 'avahi-publish-service' binary not found" );
        }
        when( $squeezebox_setup ) {
            print( "Squeezebox configuration routine completed\n" ) if $verbose;
        }
    }
    print("Child exited\n") if $verbose;
    $SIG{CHLD} = \&REAP;
};
$SIG{CHLD} = \&REAP;

my %conns;

$SIG{TERM} = $SIG{INT} = sub {
    print basename($0) . " killed\n";
    map { eval { kill $_->{decoder_pid} } } keys %conns;
    kill 9, $avahi_publish if $avahi_publish;
    # Clean up any running squeezebox_setup processes...
    my $child;
    do {
        $child = waitpid( -1, WNOHANG );
    } while $child > 0;
    exit 0;
};
$SIG{__DIE__} = sub {
    map { eval { kill $_->{decoder_pid} } } keys %conns;
    kill 9, $avahi_publish if $avahi_publish;
    # Clean up any running squeezebox_setup processes...
    my $child;
    do {
        $child = waitpid( -1, WNOHANG );
    } while $child > 0;
};

$avahi_publish = fork();
my $pw_clause = (length $password) ? "pw=true" : "pw=false";
if ($avahi_publish==0) {
    { exec 'avahi-publish-service',
        join('', map { sprintf "%02X", $_ } @hw_addr) . "\@$apname",
        "_raop._tcp",
         $port,
        "tp=UDP","sm=false","sv=false","ek=1","et=0,1","cn=0,1","ch=2","ss=16","sr=44100",$pw_clause,"vn=3","txtvers=1"; };
    { exec 'dns-sd', '-R',
        join('', map { sprintf "%02X", $_ } @hw_addr) . "\@$apname",
        "_raop._tcp",
        ".",
         $port,
        "tp=UDP","sm=false","sv=false","ek=1","et=0,1","cn=0,1","ch=2","ss=16","sr=44100",$pw_clause,"vn=3","txtvers=1"; };
    { exec 'mDNSPublish',
        join('', map { sprintf "%02X", $_ } @hw_addr) . "\@$apname",
        "_raop._tcp",
         $port,
        "tp=UDP","sm=false","sv=false","ek=1","et=0,1","cn=0,1","ch=2","ss=16","sr=44100",$pw_clause,"vn=3","txtvers=1"; };
    die "could not run avahi-publish-service nor dns-sd nor mDNSPublish";
}

my $airport_pem = join '', <DATA>;
my $rsa = Crypt::OpenSSL::RSA->new_private_key($airport_pem) || die "RSA private key import failed";

my $listen;
{
    eval {
        local $SIG{__DIE__};
        $listen = new IO::Socket::INET6(Listen => 1,
                            Domain => AF_INET6,
                            LocalPort => $port,
                            ReuseAddr => 1,
                            Proto => 'tcp');
    };
    if ($@) {
        print "**************************************\n",
              "* IO::Socket::INET6 not present!     *\n",
              "* Install this if iTunes won't play. *\n",
              "**************************************\n\n";
    }

    $listen ||= new IO::Socket::INET(Listen => 1,
            LocalPort => $port,
            ReuseAddr => 1,
            Proto => 'tcp');
}
die "Can't listen on port " . $port . ": $!" unless $listen;

sub ip6bin {
    my $ip = shift;
    $ip =~ /((.*)::)?(.+)/;
    my @left = split /:/, $2;
    my @right = split /:/, $3;
    my @mid;
    my $pad = 8 - ($#left + $#right + 2);
    if ($pad > 0) {
        @mid = (0) x $pad;
    }

    pack('S>*', map { hex } (@left, @mid, @right));
}

my $sel = new IO::Select($listen);

if ($daemon) {
    chdir "/" or die "Could not chdir to '/': $!";
    umask 0;
    open STDIN, "/dev/null" or die "Could not redirect /dev/null to STDIN(0): $!";
    open STDOUT, ">/dev/null" or die "Could not redirect STDOUT(1) to /dev/null: $!";
    defined( my $pid = fork() ) or die "Could not fork: $!";
    exit 0 if $pid;
    setsid() or die "Could not start new session: $!";
    open STDERR, ">&STDOUT" or die "Could not dup STDOUT(1)";
}
if (defined($writepid) && $writepid) {
    open PID, ">$writepid" or die "Could not create PID file '$writepid': $!";
    print PID $$;
    close PID;
}

print "Listening...\n" if $verbose;

sub performSqueezeboxSetup {
    $squeezebox_setup = fork();
    if( 0 == $squeezebox_setup ) {
        my $items;
        my @favourites;
        my @ids;
        my $index;

        my $response;

        my $findFavourites = sub {
            my ( $socket ) = @_;
            print $socket "favorites items\n";
            $response = <$socket>;
            $response =~ s/^\s*favorites\s+items\s+count%3A([0-9]+)\s*$/$1/;
            $items = $response;
            print "Found $items favourites...\n" if $verbose;

            print $socket "favorites items 0 $items want_url%3A1\n";
            $response = <$socket>;
            undef( @favourites );
            @favourites = split( /id%3A/ , $response );
            @ids = split( /id%3A/ , $response );
            shift( @favourites );
            shift( @ids );
            for( my $n = 0; $n <= scalar( @favourites ); $n++ ) {
                if( defined( $favourites[ $n ] ) ) {
                    my $url = $favourites[ $n ];
                    $url =~ s/^.*url%3A([^ ]+)\s.*$/$1/;
                    $url = uri_unescape( $url );
                    chomp $url;
                    print "\tFavourite with URL '$url' " if $verbose;
                    $favourites[ $n ] = $url;
                }
                if( defined( $ids[ $n ] ) ) {
                    my $id = $ids[ $n ];
                    $id =~ s/^([^ ]+)\s.*$/$1/;
                    chomp $id;
                    my @components = split( /\./, $id );
                    shift ( @components );
                    $id = join( '.', @components );
                    print "and ID '$id'\n" if $verbose;
                    $ids[ $n ] = $id;
                }
            }
        };

        my $socket = IO::Socket::INET -> new (
              PeerAddr => "127.0.0.1"
            , PeerPort => ( (defined( $cliport ) and $cliport ) ? $cliport : 9090 )
            , Proto    => 'tcp'
            , Timeout  => 1
        ) or die "Could not create socket: $!";

        &$findFavourites( $socket );

        print "Favourites URLs:\n" if $verbose;
        foreach my $url (@favourites) {
            print "\t$url\n" if( ( defined( $url ) && $url ) && $verbose );
        }
        my $okay = 1;
        if( !( grep { $_ =~ m/^wavin:/ } @favourites ) ) {
            $okay = 0;
            print "INFO: AirPlay 'wavin' Favourite does not exist - creating... ";
            print $socket "favorites add url%3Awavin%3Aairplay title%3AAirPlay\n";
            $response = <$socket>;
            if( $response =~ m/\scount%3A1/ ) {
                print "done\nINFO: AirPlay 'wavin' favourite successfully created.\n";
                $okay = 1;
            } else {
                print "failed\nWARN: Could not create AirPlay favourite\n";
                print "      Server response was $response\n";
            }
            if( $okay ) {
                &$findFavourites( $socket );

                print "Updated Favourites URLs:\n" if $verbose;
                foreach my $url (@favourites) {
                    print "\t$url\n" if( ( defined( $url ) && $url ) && $verbose );
                }
                if( !( grep { $_ =~ m/^wavin:/ } @favourites ) ) {
                    print "WARN: Cloud not identify AirPlay Favourite, even after creating it - disabling SqueezeBox integration\n";
                    $squeeze = 0;
                    $okay = 0;
                }
            }
        }
        if( $okay ) {
            for ( my $n = 0 ; !( defined( $index ) ) && $n <= scalar( @favourites ) ; $n++ ) {
                if( $favourites[ $n ] =~ m/^wavin:/ ) {
                    $index = $n;
                    print "Found favourite '" . $favourites[ $index ] . "' with ID '" . $ids[ $index ] . "' at position $index.\n" if $verbose;
                }
            }

            print "Turning on player (if off)... " if $verbose;
            print $socket "$mac power 1\n";
            $response = <$socket>;
            print "$response\n" if $verbose;

            print "Stopping player (if playing)... " if $verbose;
            print $socket "$mac stop\n";
            $response = <$socket>;
            print "$response\n" if $verbose;

            print "Unmuting player (if muted)... " if $verbose;
            print $socket "$mac mixer muting 0\n";
            $response = <$socket>;
            print "$response\n" if $verbose;

            if( defined( $volume ) ) {
                print "Setting player volume to $volume... " if $verbose;
                print $socket "$mac mixer volume $volume\n";
                $response = <$socket>;
                print "$response\n" if $verbose;
            }

            print "Showing message... " if $verbose;
            print $socket "$mac show line2%3AStarting%20AirPlay duration%3A5 brightness%3ApowerOn font%3Ahuge\n";
            $response = <$socket>;
            print "$response\n" if $verbose;

            if( defined( $index ) ) {
                print "Playing favourite... " if $verbose;
                my $id = uri_escape( $ids[ $index ] );
                print $socket "$mac favorites playlist play item_id%3A$id\n";
            } else {
                print "Resuming play... " if $verbose;
                print $socket "$mac play\n";
            }
            $response = <$socket>;
            print "$response\n" if $verbose;
        }
        close( $socket );

        exit(0);
    }
};

while (1) {
    my @waiting = $sel->can_read;
    foreach my $fh (@waiting) {
        if ($fh==$listen) {
            my $new = $listen->accept;
            printf "New connection from %s\n", $new->sockhost if $verbose;

            $sel->add($new);
            $new->blocking(0);
            $conns{$new} = {fh => $fh};

            if (defined($squeeze) && $squeeze) {
                &performSqueezeboxSetup();
            }

            # the 2nd connection is a player connection
            if (defined($play_prog) && $sel->count() == 2) {
                system($play_prog);
            }
        } else {
            if (eof($fh)) {
                print "Closed: $fh\n" if $verbose;
                $sel->remove($fh);
                close $fh;
                # Prevent warnings when decoder_pid isn't defined
                # (e.g. client connected, but playback not started)
                if (defined($conns{$fh}{decoder_pid})) {
                    eval { kill $conns{$fh}{decoder_pid} };
                }
                delete $conns{$fh};

                # 1 connection means no connection
                if (defined($stop_prog) && $sel->count() == 1) {
                    system($stop_prog);
                }
                next;
            }
            if (exists $conns{$fh}) {
                conn_handle_data($fh);
            }
        }
    }
}

exit(1); # Unreachable


sub conn_handle_data {
    my $fh = shift;
    my $conn = $conns{$fh};

    if ($conn->{req_need}) {
        if (length($conn->{data}) >= $conn->{req_need}) {
            $conn->{req}->content(substr($conn->{data}, 0, $conn->{req_need}, ''));
            conn_handle_request($fh, $conn);
        }
        undef $conn->{req_need};
        return;
    }

    read $fh, my $data, 4096;
    $conn->{data} .= $data;

    if ($conn->{data} =~ /(\r\n\r\n|\n\n|\r\r)/) {
        my $req_data = substr($conn->{data}, 0, $+[0], '');
        $conn->{req} = HTTP::Request->parse($req_data);
        printf "REQ: %s\n", $conn->{req}->method if $verbose;
        conn_handle_request($fh, $conn);
        conn_handle_data($fh) if length($conn->{data});
    }
}

sub digest_ok {
    my ($req, $conn) = @_;
    my $authz = $req->header('Authorization');
    return 0 unless $authz =~ s/^Digest\s+//i;
    return 0 unless length $conn->{nonce};
    my @authz = split /,\s*/, $authz;
    my %authz = map { /(.+)="(.+)"/; ($1, $2) } @authz;

    # not a standard digest - uses capital hex digits, in conflict with the RFC
    my $digest = uc md5_hex (
        uc(md5_hex($authz{username} . ':' . $authz{realm} . ':' . $password))
        . ':' . $authz{nonce} . ':' .
        uc(md5_hex($req->method . ':' . $authz{uri}))
    );

    return $digest eq $authz{response};
}

sub conn_handle_request {
    my ($fh, $conn) = @_;

    my $req = $conn->{req};;
    my $clen = $req->header('content-length') // 0;
    if ($clen > 0 && !length($req->content)) {
        $conn->{req_need} = $clen;
        return; # need more!
    }

    my $resp = HTTP::Response->new(200);
    $resp->request($req);
    $resp->protocol($req->protocol);

    $resp->header('CSeq', $req->header('CSeq'));
    $resp->header('Audio-Jack-Status', 'connected; type=analog');

    if (my $chall = $req->header('Apple-Challenge')) {
        my $data = decode_base64($chall);
        my $ip = $fh->sockhost;
        if ($ip =~ /((\d+\.){3}\d+)$/) { # IPv4
            $data .= join '', map { chr } split(/\./, $1);
        } else {
            $data .= ip6bin($ip);
        }

        $data .= join '', map { chr } @hw_addr;
        $data .= chr(0) x (0x20-length($data));

        $rsa->use_pkcs1_padding;    # this isn't hashed before signing
        my $signature = encode_base64 $rsa->private_encrypt($data), '';
        $signature =~ s/=*$//;
        $resp->header('Apple-Response', $signature);
    }

    if (length $password) {
        if (!digest_ok($req, $conn)) {
            my $nonce = md5_hex(map { rand } 1..20);
            $conn->{nonce} = $nonce;
            $resp->header('WWW-Authenticate', "Digest realm=\"$apname\", nonce=\"$nonce\"");
            $resp->code(401);
            $req->method('DENIED');
        }
    }

    for ($req->method) {
        /^OPTIONS$/ && do {
            $resp->header('Public', 'ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER');
            last;
        };

                /^ANNOUNCE$/ && do {
                        my $sdp = Net::SDP->new($req->content);
                        my $audio = $sdp->media_desc_of_type('audio');

                        print $audio->as_string();
                        print $audio->attribute('aesiv');

                        die("no AESIV") unless my $aesiv = decode_base64($audio->attribute('aesiv'));
                        die("no AESKEY") unless my $rsaaeskey = decode_base64($audio->attribute('rsaaeskey'));
                        $rsa->use_pkcs1_oaep_padding;
                        my $aeskey = $rsa->decrypt($rsaaeskey) || die "RSA decrypt failed";

                        $conn->{aesiv} = $aesiv;
                        $conn->{aeskey} = $aeskey;
                        $conn->{fmtp} = $audio->attribute('fmtp');
                        last;
                };

        /^SETUP$/ && do {
            my $transport = $req->header('Transport');
            $transport =~ s/;control_port=(\d+)//;
            my $cport = $1;
            $transport =~ s/;timing_port=(\d+)//;
            my $tport = $1;
            $transport =~ s/;server_port=(\d+)//;
            my $dport = $1;
            $resp->header('Session', 'DEADBEEF');

            my %dec_args = (
                iv      =>  unpack('H*', $conn->{aesiv}),
                key     =>  unpack('H*', $conn->{aeskey}),
                fmtp    => $conn->{fmtp},
                cport   => $cport,
                tport   => $tport,
                dport   => $dport,
#                host    => 'unused',
            );
            $dec_args{pipe} = $pipepath if defined $pipepath;
            $dec_args{ao_driver} = $libao_driver if defined $libao_driver;
            $dec_args{ao_devicename} = $libao_devicename if defined $libao_devicename;
            $dec_args{ao_deviceid} = $libao_deviceid if defined $libao_deviceid;

            my $dec = $hairtunes_cli . join(' ', '', map { sprintf "%s '%s'", $_, $dec_args{$_} } keys(%dec_args));

            #    print "decode command: $dec\n";
            my $decoder = open2(my $dec_out, my $dec_in, $dec);

            $conn->{decoder_pid} = $decoder;
            $conn->{decoder_fh} = $dec_in;
            my $portdesc = <$dec_out>;
            die("Expected port number from decoder; got $portdesc") unless $portdesc =~ /^port: (\d+)/;
            my $port = $1;
            print "launched decoder: $decoder on port: $port\n" if $verbose;
            $resp->header('Transport', $req->header('Transport') . ";server_port=$port");
            last;
        };

        /^RECORD$/ && last;
        /^FLUSH$/ && do {
            my $dfh = $conn->{decoder_fh};
            print $dfh "flush\n";
            last;
        };
        /^TEARDOWN$/ && do {
            $resp->header('Connection', 'close');
            close $conn->{decoder_fh};
            last;
        };
        /^SET_PARAMETER$/ && do {
            my @lines = split /[\r\n]+/, $req->content;
            my %content = map { /^(\S+): (.+)/; (lc $1, $2) } @lines;
            my $cfh = $conn->{decoder_fh};
            if (exists $content{volume}) {
                printf $cfh "vol: %f\n", $content{volume};
            }
            last;
        };
        /^GET_PARAMETER$/ && last;
        /^DENIED$/ && last;
        die("Unknown method: $_");
    }

    print $fh $resp->as_string("\r\n");
    $fh->flush;
}

__DATA__
-----BEGIN RSA PRIVATE KEY-----
MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt
wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U
wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf
/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/
UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW
BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa
LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5
NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm
lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz
aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu
a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM
oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z
oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+
k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL
AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA
cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf
54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov
17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc
1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI
LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ
2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=
-----END RSA PRIVATE KEY-----
