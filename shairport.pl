#!/usr/bin/perl

# Configure the following two options:
# AP name - as will be shown in iTunes' menu
# example:
#  my $apname = "SteePort";
my $apname = "HairPort $$ on " . `hostname`;

# password - required to connect
# for no password, set:
#  my $password = '';
my $password = '';

# that's all!

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

use IO::Select;
use IO::Socket;
use MIME::Base64;
use HTTP::Request;
use HTTP::Response;
use IPC::Open2;
use Crypt::OpenSSL::RSA;
use Digest::MD5 qw/md5_hex/;
use POSIX ":sys_wait_h";


chomp $apname;

my @hw_addr = (0, map { int rand 256 } 1..5);

sub POPE {
    print "broken pipe\n";
    $SIG{PIPE} = \&POPE;
}
$SIG{PIPE} = \&POPE;


my $avahi_publish = fork();
if ($avahi_publish==0) {
    exec 'avahi-publish-service',
        join('', map { sprintf "%02X", $_ } @hw_addr) . "\@$apname",
        "_raop._tcp",
        "5000",
        "tp=UDP","sm=false","sv=false","ek=1","et=0,1","cn=0,1","ch=2","ss=16","sr=44100","pw=false","vn=3","txtvers=1"
	or die "could not run avahi-publish-service";
}        

sub REAP {
    if ($avahi_publish == waitpid(-1, WNOHANG)) {
        die("Avahi publishing failed!");
    }
    printf("***CHILD EXITED***\n");
    $SIG{CHLD} = \&REAP;
};
$SIG{CHLD} = \&REAP;

my %conns;

$SIG{TERM} = $SIG{INT} = $SIG{__DIE__} = sub {
    map { eval { kill $_->{decoder_pid} } } keys %conns;
    kill 9, $avahi_publish;
    exit(0);
};

my $airport_pem = join '', <DATA>;
my $rsa = Crypt::OpenSSL::RSA->new_private_key($airport_pem) || die;

my $listen = new IO::Socket::INET(Listen => 1,
                                LocalPort => 5000,
                                ReuseAddr => 1,
                                Proto => 'tcp');
die "Can't listen on port 5000: $!" unless $listen;

my $sel = new IO::Select($listen);

print "listening...\n";

while (1) {
    my @waiting = $sel->can_read;
    foreach $fh (@waiting) {
        if ($fh==$listen) {
            my $new = $listen->accept;
            print "new connection\n";
            $sel->add($new);
            $new->blocking(0);
            $conns{$new} = {fh => $fh};
        } else {
            if (eof($fh)) {
                print "closed: $fh\n";
                $sel->remove($fh);
                close $fh;
                eval { kill $conns{$fh}{decoder_pid} };
                delete $conns{$fh};
                next;
            }
            if (exists $conns{$fh}) {
                conn_handle_data($fh);
            }
        }
    }
}

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
        printf "REQ: %s\n", $conn->{req}->method;
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
        $data .= join '', map { chr } split(/\./, $fh->sockhost);
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
            my $sdptext = $req->content;
            my @sdplines = split /[\r\n]+/, $sdptext;
            my %sdp = map { ($1, $2) if /^a=([^:]+):(.+)/ } @sdplines;
            die("no AESIV") unless my $aesiv = decode_base64($sdp{aesiv});
            die("no AESKEY") unless my $rsaaeskey = decode_base64($sdp{rsaaeskey});
            $rsa->use_pkcs1_oaep_padding;
            my $aeskey = $rsa->decrypt($rsaaeskey) || die;

            $conn->{aesiv} = $aesiv;
            $conn->{aeskey} = $aeskey;
            $conn->{fmtp} = $sdp{fmtp};
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


            my $dec = sprintf("./hairtunes iv %s key %s fmtp %s cport %s tport %s dport %s host %s",
                    map { "'$_'" } (
                        unpack('H*', $conn->{aesiv}),
                        unpack('H*', $conn->{aeskey}),
                        $conn->{fmtp},
                        $cport, $tport, $dport,
                        'unused'
                    ));
            #    print "decode command: $dec\n";
            my $decoder = open2(my $dec_out, my $dec_in, $dec);
            
            $conn->{decoder_pid} = $decoder;
            $conn->{decoder_fh} = $dec_in;
            my $portdesc = <$dec_out>;
            die("Expected port number from decoder; got $portdesc") unless $portdesc =~ /^port: (\d+)/;
            my $port = $1;
            print "launched decoder: $decoder on port: $port\n";
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
2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKaXTyY=
-----END RSA PRIVATE KEY-----
